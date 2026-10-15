#pragma once
// Gallery source coordinator. The injected core is portable; the host wrappers
// below perform explicit native INI/ledger work only when called by the host.
#include "../gallery_engine.h"
#if (defined(AIP_GALLERY_RUNTIME_NO_HOST) || defined(DESKTOPSTUB_INERT_HARDWARE)) && !defined(AIP_GALLERY_INVENTORY_PURE)
#define AIP_GALLERY_INVENTORY_PURE
#define AIP_GALLERY_RUNTIME_LOCAL_PURE
#endif
#include "../gallery_inventory.inc"
#ifdef AIP_GALLERY_RUNTIME_LOCAL_PURE
#undef AIP_GALLERY_RUNTIME_LOCAL_PURE
#undef AIP_GALLERY_INVENTORY_PURE
#endif
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace aip { namespace gallery { namespace runtime {
enum class ReadResult { Ok, Missing, Error };
enum class SaveResult { Saved, Conflict, Error };
struct Services {
    std::function<ReadResult(const std::wstring&, Ledger&, std::wstring&)> readLedger;
    std::function<SaveResult(const std::wstring&, const Ledger&, const Ledger&, std::wstring&)> saveLedger;
    std::function<bool(const std::wstring&, const Ledger&, std::wstring&)> createLedger;
    std::function<InventoryResult(const std::wstring&, const std::wstring&, const InventoryIndex&, const InventoryCancel&)> scan;
    std::function<std::wstring(const std::wstring&)> cacheDirectory;
    std::function<std::uint64_t()> now;
    std::function<void()> wake;
    std::function<void()> planning; // Optional inert work counter; native leaves it empty.
};
// Used inside the native file lock and by injected stores. This decision neither
// writes nor authorizes publication; the caller must atomically persist bytes.
inline SaveResult PrepareLedgerCas(const Ledger& expected, const Ledger& fresh, const Ledger& candidate,
    std::string& bytes, std::wstring& error) {
    std::string before, actual, after;
    if (!SerializeLedger(expected, before, error) || !SerializeLedger(fresh, actual, error) || !SerializeLedger(candidate, after, error)) return SaveResult::Error;
    if (before != actual) { error = L"Gallery ownership changed before this save. The newer ledger was kept."; return SaveResult::Conflict; }
    if (candidate.groupId != expected.groupId || candidate.epochId != expected.epochId || candidate.revision <= expected.revision ||
        candidate.nextOrdinal < expected.nextOrdinal || candidate.nextTicket < expected.nextTicket) {
        error = L"An ordinary gallery save cannot reset its history, epoch, or revision."; return SaveResult::Error;
    }
    for (const auto& image : expected.images) {
        const auto found = candidate.images.find(image.first);
        if (found == candidate.images.end() || found->second.owner != image.second.owner || found->second.ordinal != image.second.ordinal) {
            error = L"An ordinary gallery save cannot release or reassign a historical image reservation."; return SaveResult::Error;
        }
    }
    for (const auto& tile : expected.tiles) if (candidate.tiles.find(tile.first) == candidate.tiles.end()) {
        error = L"An ordinary gallery save cannot discard a historical tile record."; return SaveResult::Error;
    }
    bytes = std::move(after); error.clear(); return SaveResult::Saved;
}
struct Input {
    std::wstring profile;
    Configuration configuration;
    bool contentEnabled = false, tilesEnabled = false, directAvailable = false;
};
struct Receipt {
    std::wstring groupId, tileId, epoch, frameKey;
    std::uint64_t ticket = 0;
    unsigned slot = 0, count = 0; // Zero-based slot, total slots in this window.
};
struct Frame {
    Receipt receipt;
    std::wstring nativeId, path, name, activationArguments, layout, tag, branding;
    bool reset = false, contain = false;
};
struct MenuState {
    std::wstring id, nativeId, name, status;
    bool enabled = false, ledgerReady = false;
    std::size_t available = 0, reserved = 0, pending = 0;
};
inline std::wstring LedgerPath(const std::wstring& absoluteProfile) { return absoluteProfile + L".gallery-ledger"; }
inline bool Initialize(const Services& services, const std::wstring& absoluteProfile, const Configuration& cfg,
    const std::wstring& epoch, bool historyKnownNewOrRetired, std::wstring& error) {
    if (absoluteProfile.empty() || !ValidateConfiguration(cfg, error) || !services.createLedger) {
        if (error.empty()) error = L"Gallery initialization needs a valid profile and create-only ledger store.";
        return false;
    }
    Ledger ledger;
    if (!CreateLedger(cfg.groupId, epoch, historyKnownNewOrRetired, ledger, error)) return false;
    // The injected/native store must atomically refuse replacement of an
    // existing file, including a file created after a caller's earlier read.
    return services.createLedger(LedgerPath(absoluteProfile), ledger, error);
}
inline std::wstring Pack(const std::wstring& text) { return std::to_wstring(text.size()) + L":" + text; }
inline std::wstring ConfigKey(const Configuration& cfg) {
    std::wstring key = Pack(cfg.groupId) + Pack(cfg.folder) + std::to_wstring(cfg.queueSize) + L":" +
        std::to_wstring(cfg.refreshSeconds) + L":" + std::to_wstring(cfg.seed) + L":" +
        std::to_wstring(static_cast<int>(cfg.order)) + L":" + std::to_wstring(static_cast<int>(cfg.fit));
    for (const auto& tile : cfg.tiles) key += Pack(tile.id) + Pack(tile.name) + (tile.enabled ? L"1" : L"0") +
        Pack(tile.appearance.background) + Pack(tile.appearance.branding) + Pack(tile.appearance.backgroundColor);
    return key;
}
class Coordinator {
    struct Work {
        std::thread thread;
        std::atomic<bool> canceled{false}, done{false};
        bool failed = false;
        std::wstring context;
        InventoryResult result;
    };
    struct Flight {
        QueueWindow window;
        std::vector<bool> acknowledged;
        std::uint64_t attempt = 1, retryAfter = 0;
        bool readyToCommit = false;
    };
    Services services_;
    std::mutex operation_;
    mutable std::mutex published_;
    std::atomic<bool> stopped_{false}, refreshRequested_{false}, reloadRequested_{false};
    std::wstring context_, profile_, configKey_, poolKey_, cacheDirectory_, message_, inventoryMessage_;
    Configuration cfg_;
    Ledger ledger_;
    bool ledgerReady_ = false, inventoryReady_ = false, poolsReady_ = false, eligible_ = false;
    std::uint64_t inventoryRevision_ = 0, poolRevision_ = 0, retryAfter_ = 0, nextScan_ = 0, attemptSequence_ = 0;
    std::uint64_t flightRevision_ = 0, plannedLedger_ = 0, plannedInventory_ = 0, plannedFlights_ = 0;
    std::wstring plannedConfig_;
    std::set<std::wstring> plannedDue_;
    bool planCached_ = false;
    InventoryIndex index_;
    std::map<std::wstring, std::wstring> paths_;
    PoolSnapshot pools_;
    std::shared_ptr<Work> work_;
    std::map<std::wstring, std::uint64_t> lastAccepted_;
    std::map<std::wstring, std::size_t> lastQueueCount_;
    // The following data is protected by published_; delivery callbacks never
    // touch the ledger, inventory, worker, or filesystem.
    std::map<std::wstring, Flight> flights_;
    std::vector<Frame> frames_;
    std::wstring viewProfile_, viewGroup_;
    std::map<std::wstring, MenuState> menu_;
    void Wake() const { if (services_.wake) services_.wake(); }
    std::uint64_t Now() const { return services_.now ? services_.now() : 0; }
    static bool Same(const Receipt& left, const Receipt& right) {
        return left.groupId == right.groupId && left.tileId == right.tileId && left.epoch == right.epoch &&
            left.ticket == right.ticket && left.slot == right.slot && left.count == right.count && left.frameKey == right.frameKey;
    }
    bool MatchesUnlocked(const Receipt& receipt) const {
        return std::any_of(frames_.begin(), frames_.end(), [&](const auto& frame) { return Same(frame.receipt, receipt); });
    }
    void RemoveFramesUnlocked(const std::wstring& tile) {
        frames_.erase(std::remove_if(frames_.begin(), frames_.end(), [&](const auto& frame) { return frame.receipt.tileId == tile; }), frames_.end());
    }
    void PublishStatus(const std::wstring& message, bool clearFrames = false) {
        std::lock_guard<std::mutex> lock(published_);
        message_ = message; viewProfile_ = profile_; viewGroup_ = cfg_.groupId;
        if (clearFrames) frames_.clear();
        menu_.clear();
        for (const auto& tile : cfg_.tiles) {
            MenuState state; state.id = tile.id; state.nativeId = NativeTileId(cfg_.groupId, tile.id); state.name = tile.name;
            state.enabled = cfg_.enabled && tile.enabled; state.ledgerReady = ledgerReady_;
            if (poolsReady_) {
                const auto pool = std::find_if(pools_.pools.begin(), pools_.pools.end(), [&](const auto& value) { return value.tileId == tile.id; });
                if (pool != pools_.pools.end()) { state.available = pool->images.size(); state.reserved = pool->reservedCount; }
            }
            const auto pending = ledger_.tiles.find(tile.id);
            if (ledgerReady_ && pending != ledger_.tiles.end()) state.pending = pending->second.pending.images.size();
            state.status = !state.enabled ? L"Paused; existing reservations are kept." : message;
            if (state.enabled && inventoryReady_ && poolsReady_ && !state.available)
                state.status = L"No usable photos in this tile's pool. New tiles wait for unowned photos; existing reservations stay with their owners.";
            menu_.emplace(tile.id, std::move(state));
        }
    }
    void CancelWork() { if (work_) work_->canceled = true; }
    void InvalidateFlights() { std::lock_guard<std::mutex> lock(published_); frames_.clear(); flights_.clear(); ++flightRevision_; }
    bool Save(const Ledger& candidate, std::uint64_t now, std::wstring& error) {
        const auto saved = services_.saveLedger(LedgerPath(profile_), ledger_, candidate, error);
        if (saved == SaveResult::Saved) { ledger_ = candidate; return true; }
        if (error.empty()) error = saved == SaveResult::Conflict ? L"Gallery ownership changed in another writer. Waiting to reload it safely." : L"The gallery ledger could not be saved. No new gallery frames were published.";
        retryAfter_ = now + 5000;
        if (saved == SaveResult::Conflict) { ledgerReady_ = false; poolsReady_ = false; InvalidateFlights(); }
        PublishStatus(error, true); return false;
    }
    bool Reap(std::uint64_t now) {
        if (!work_ || !work_->done.load(std::memory_order_acquire)) return false;
        const auto completed = work_;
        if (completed->thread.joinable()) completed->thread.join();
        work_.reset();
        if (completed->canceled.load() || completed->context != context_) return false;
        if (completed->failed || completed->result.cancelled) { nextScan_ = now + 5000; inventoryMessage_ = L"Gallery inventory work failed or was canceled. Retrying without changing ownership."; return false; }
        inventoryMessage_ = completed->result.diagnostics.empty() ? L"" : completed->result.diagnostics.front().substr(0, 256);
        const bool progressed = completed->result.deferredFiles && completed->result.index.cursor != index_.cursor;
        index_ = std::move(completed->result.index);
        if (!completed->result.complete) { nextScan_ = now + (progressed ? 250 : 30000); return false; }
        std::map<std::wstring, std::wstring> usable;
        for (const auto& image : completed->result.images) {
            if (!OpaqueImageId(image.id) || image.thumbnailPath.empty()) { nextScan_ = now + 5000; return false; }
            const auto inserted = usable.emplace(image.id, image.thumbnailPath);
            if (!inserted.second && inserted.first->second != image.thumbnailPath) { nextScan_ = now + 5000; return false; }
        }
        if (!inventoryReady_ || usable != paths_) ++inventoryRevision_;
        paths_ = std::move(usable); inventoryReady_ = true;
        nextScan_ = now + 30000; return true;
    }
    bool StartScan(std::uint64_t now) {
        if (work_ || now < nextScan_ || cfg_.folder.empty()) return false;
        if (cacheDirectory_.empty()) cacheDirectory_ = services_.cacheDirectory(profile_);
        if (cacheDirectory_.empty()) { nextScan_ = now + 5000; return false; }
        const auto current = std::make_shared<Work>(); current->context = context_;
        const auto scan = services_.scan; const auto wake = services_.wake;
        const auto folder = cfg_.folder, cache = cacheDirectory_; const auto previous = index_;
        work_ = current;
        try {
            current->thread = std::thread([current, scan, wake, folder, cache, previous] {
                try { current->result = scan(folder, cache, previous, [current] { return current->canceled.load(); }); }
                catch (...) { current->failed = true; }
                current->done.store(true, std::memory_order_release);
                try { if (wake) wake(); } catch (...) {}
            });
        } catch (...) { current->canceled = true; work_.reset(); nextScan_ = now + 5000; return false; }
        return true;
    }
    bool CommitAcknowledgements(std::uint64_t now, std::wstring& error) {
        std::vector<QueueWindow> accepted;
        { std::lock_guard<std::mutex> lock(published_); for (const auto& flight : flights_) if (flight.second.readyToCommit) accepted.push_back(flight.second.window); }
        if (accepted.empty()) return true;
        auto candidate = ledger_;
        for (const auto& window : accepted) {
            Ledger next;
            if (!AcknowledgeDelivery(candidate, window, true, next, error)) { ledgerReady_ = false; poolsReady_ = false; InvalidateFlights(); PublishStatus(error, true); return false; }
            candidate = std::move(next);
        }
        if (!Save(candidate, now, error)) return false;
        std::lock_guard<std::mutex> lock(published_);
        for (const auto& window : accepted) {
            const auto found = flights_.find(window.tileId);
            if (found != flights_.end() && found->second.window.ticket == window.ticket) flights_.erase(found);
            lastAccepted_[window.tileId] = now; lastQueueCount_[window.tileId] = window.images.size(); RemoveFramesUnlocked(window.tileId);
        }
        return true;
    }
    bool RebuildPools(std::uint64_t now, std::wstring& error) {
        if (poolsReady_ && poolRevision_ == inventoryRevision_ && poolKey_ == configKey_) return true;
        std::vector<std::wstring> ids; ids.reserve(paths_.size()); for (const auto& image : paths_) ids.push_back(image.first);
        OwnershipPlan ownership;
        if (!ReconcileOwnership(cfg_, ids, ledger_, ownership, error)) { PublishStatus(error, true); return false; }
        if (ownership.requiresLedgerCommit && !Save(ownership.ledger, now, error)) return false;
        pools_ = std::move(ownership.pools); poolsReady_ = true; poolRevision_ = inventoryRevision_; poolKey_ = configKey_; return true;
    }
    bool Prepare(std::uint64_t now, bool force, std::wstring& error) {
        std::set<std::wstring> due;
        for (const auto& tile : cfg_.tiles) if (tile.enabled) {
            const auto progress = ledger_.tiles.find(tile.id);
            const auto last = lastAccepted_.find(tile.id);
            if (force || (progress != ledger_.tiles.end() && progress->second.pending.ticket) || last == lastAccepted_.end() ||
                now < last->second || now - last->second >= static_cast<std::uint64_t>(cfg_.refreshSeconds) * 1000) due.insert(tile.id);
        }
        if (due.empty()) { planCached_ = false; return true; }
        {
            std::lock_guard<std::mutex> lock(published_); bool retryDue = false;
            for (const auto& flight : flights_) if (flight.second.retryAfter && now >= flight.second.retryAfter) retryDue = true;
            if (!force && !retryDue && planCached_ && plannedLedger_ == ledger_.revision && plannedInventory_ == inventoryRevision_ &&
                plannedFlights_ == flightRevision_ && plannedConfig_ == configKey_ && plannedDue_ == due) return true;
        }
        for (unsigned pass = 0; pass < 2; ++pass) {
            if (services_.planning) services_.planning();
            QueueBatch batch;
            if (!PrepareQueueBatch(cfg_, pools_, ledger_, due, batch, error)) { PublishStatus(error, true); return false; }
            if (batch.requiresLedgerCommit && !Save(batch.ledger, now, error)) return false;
            auto candidate = ledger_; bool unavailable = false;
            for (const auto& window : batch.windows) if (window.status == WindowStatus::PendingImagesUnavailable || window.images.size() > cfg_.queueSize) {
                Ledger next;
                if (!AcknowledgeDelivery(candidate, window, false, next, error)) { PublishStatus(error, true); return false; }
                candidate = std::move(next); unavailable = true;
            }
            if (unavailable) { if (!Save(candidate, now, error)) return false; InvalidateFlights(); continue; }
            std::lock_guard<std::mutex> lock(published_);
            std::vector<Frame> frames;
            std::set<std::wstring> currentMembers;
            for (const auto& tile : cfg_.tiles) if (tile.enabled) currentMembers.insert(tile.id);
            for (auto flight = flights_.begin(); flight != flights_.end();) {
                if (currentMembers.find(flight->first) == currentMembers.end()) flight = flights_.erase(flight); else ++flight;
            }
            for (const auto& window : batch.windows) {
                if (window.status != WindowStatus::Ready || !ValidateWindowForPublication(ledger_, window, error)) continue;
                auto& flight = flights_[window.tileId];
                if (flight.window.ticket != window.ticket || flight.window.epochId != window.epochId) {
                    flight = {}; flight.attempt = ++attemptSequence_; flight.window = window; flight.acknowledged.resize(window.images.size(), false); ++flightRevision_;
                }
                if (flight.readyToCommit || now < flight.retryAfter) continue;
                flight.retryAfter = 0;
                const auto tile = std::find_if(cfg_.tiles.begin(), cfg_.tiles.end(), [&](const auto& value) { return value.id == window.tileId; });
                if (tile == cfg_.tiles.end()) continue;
                const auto knownCount = lastQueueCount_.find(window.tileId);
                const bool reset = knownCount == lastQueueCount_.end() || knownCount->second != window.images.size();
                for (std::size_t slot = 0; slot < window.images.size(); ++slot) {
                    const auto path = paths_.find(window.images[slot]); if (path == paths_.end()) continue;
                    Frame frame; frame.nativeId = NativeTileId(cfg_.groupId, window.tileId); frame.path = path->second; frame.name = tile->name; frame.branding = tile->appearance.branding;
                    frame.activationArguments = L"--ini " + Quote(profile_) + L" --open-folder " + Quote(cfg_.folder);
                    frame.contain = cfg_.fit == Fit::Fit; frame.reset = reset && slot == 0;
                    frame.layout = L"Gallery:" + Pack(cfg_.groupId) + Pack(window.tileId) + Pack(window.epochId) + std::to_wstring(window.images.size()) + (frame.contain ? L":Fit" : L":Fill");
                    frame.tag = L"g" + std::to_wstring(slot + 1);
                    frame.receipt = {cfg_.groupId, window.tileId, window.epochId, L"", window.ticket, static_cast<unsigned>(slot), static_cast<unsigned>(window.images.size())};
                    frame.receipt.frameKey = Pack(profile_) + frame.layout + L":" + std::to_wstring(window.ticket) + L":" + std::to_wstring(flight.attempt) +
                        L":" + std::to_wstring(slot) + Pack(window.images[slot]) + Pack(tile->name) + Pack(frame.activationArguments) + Pack(frame.branding);
                    frames.push_back(std::move(frame));
                }
            }
            frames_ = std::move(frames); planCached_ = true; plannedLedger_ = ledger_.revision; plannedInventory_ = inventoryRevision_;
            plannedFlights_ = flightRevision_; plannedConfig_ = configKey_; plannedDue_ = due; return true;
        }
        error = L"Gallery inventory changed while a pending window was being repaired. Waiting for the next poll."; PublishStatus(error, true); return false;
    }
    static std::wstring Quote(const std::wstring& value) {
        std::wstring result = L"\""; std::size_t slashes = 0;
        for (const auto c : value) {
            if (c == L'\\') { ++slashes; continue; }
            if (c == L'\"') { result.append(slashes * 2 + 1, L'\\'); result += c; }
            else { result.append(slashes, L'\\'); result += c; }
            slashes = 0;
        }
        result.append(slashes * 2, L'\\'); return result + L"\"";
    }
public:
    explicit Coordinator(Services services) : services_(std::move(services)) {}
    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;
    ~Coordinator() { Stop(); }
    void Refresh(const Input& input) {
        std::lock_guard<std::mutex> operation(operation_);
        if (stopped_) return;
        try {
        const auto now = Now(); std::wstring error;
        const auto context = Pack(input.profile) + Pack(input.configuration.groupId) + Pack(input.configuration.folder);
        if (context != context_) {
            CancelWork(); context_ = context; profile_ = input.profile; cacheDirectory_.clear(); inventoryMessage_.clear(); index_ = {}; paths_.clear();
            ledger_ = {}; ledgerReady_ = inventoryReady_ = poolsReady_ = false; retryAfter_ = nextScan_ = 0;
            lastAccepted_.clear(); lastQueueCount_.clear(); InvalidateFlights();
        }
        const auto newKey = ConfigKey(input.configuration);
        const bool configChanged = !configKey_.empty() && newKey != configKey_;
        cfg_ = input.configuration; configKey_ = newKey;
        if (configChanged) {
            std::lock_guard<std::mutex> lock(published_);
            for (auto& item : flights_) {
                item.second.attempt = ++attemptSequence_; item.second.readyToCommit = false;
                std::fill(item.second.acknowledged.begin(), item.second.acknowledged.end(), false);
            }
            frames_.clear();
            ++flightRevision_;
        }
        Reap(now);
        bool force = refreshRequested_.exchange(false) || configChanged;
        if (reloadRequested_.exchange(false)) { ledgerReady_ = poolsReady_ = false; retryAfter_ = nextScan_ = 0; InvalidateFlights(); }
        if (!ValidateConfiguration(cfg_, error) || profile_.empty()) { CancelWork(); PublishStatus(error.empty() ? L"Gallery profile is unavailable." : error, true); return; }
        if (!cfg_.enabled || !input.contentEnabled || !input.tilesEnabled || !input.directAvailable) {
            CancelWork(); ledgerReady_ = poolsReady_ = eligible_ = false; InvalidateFlights();
            PublishStatus(!cfg_.enabled ? L"Gallery updates are disabled; historical ownership is kept." :
                !input.contentEnabled || !input.tilesEnabled ? L"Enable Content and separate Tiles to update galleries. Existing ownership is kept." :
                L"Gallery queues need direct Live Tile delivery with package identity. No registration changes were requested.", true); return;
        }
        if (!eligible_) { force = true; nextScan_ = 0; eligible_ = true; }
        if (!services_.readLedger || !services_.saveLedger || !services_.scan || !services_.cacheDirectory) { PublishStatus(L"Gallery runtime services are unavailable.", true); return; }
        if (now < retryAfter_) return;
        if (!ledgerReady_) {
            Ledger fresh; const auto read = services_.readLedger(LedgerPath(profile_), fresh, error);
            if (read != ReadResult::Ok || !ValidateLedger(fresh, error) || fresh.groupId != cfg_.groupId) {
                retryAfter_ = now + 5000;
                PublishStatus(read == ReadResult::Missing ? L"Gallery ownership ledger is missing. Use explicit gallery setup; existing queues are not reset." :
                    error.empty() ? L"Gallery ownership does not match this group. Keep the old ledger and retire historical queues before resetting." : error, true); return;
            }
            ledger_ = std::move(fresh); ledgerReady_ = true; poolsReady_ = false;
        }
        if (!CommitAcknowledgements(now, error)) return;
        if (cfg_.folder.empty()) { CancelWork(); PublishStatus(L"Choose a photo folder to set up this gallery.", true); return; }
        if (force) nextScan_ = 0;
        StartScan(now);
        if (!inventoryReady_) { PublishStatus(inventoryMessage_.empty() ? L"Indexing and caching photos in bounded background passes. Existing ownership is kept." : inventoryMessage_, true); return; }
        if (!RebuildPools(now, error) || !Prepare(now, force, error)) return;
        PublishStatus(paths_.empty() ? L"No usable photos were found. Existing reservations are kept." :
            work_ ? L"Gallery ready; refreshing the cached inventory in the background." : L"Gallery ready. Windows rotates each tile's separate pool.");
        } catch (...) {
            CancelWork(); InvalidateFlights(); PublishStatus(L"Gallery refresh could not complete. Existing ownership was kept and no new frames were published.", true);
        }
    }
    void RequestRefresh(bool reloadLedger = false) { refreshRequested_ = true; if (reloadLedger) reloadRequested_ = true; Wake(); }
    std::vector<Frame> CurrentFrames() const { std::lock_guard<std::mutex> lock(published_); return frames_; }
    std::wstring Status() const { std::lock_guard<std::mutex> lock(published_); return message_; }
    bool IsProfile(const std::wstring& profile) const { std::lock_guard<std::mutex> lock(published_); return profile == viewProfile_; }
    bool Validate(const Receipt& receipt) const { std::lock_guard<std::mutex> lock(published_); return MatchesUnlocked(receipt); }
    void Acknowledge(const Receipt& receipt) {
        bool wake = false;
        {
            std::lock_guard<std::mutex> lock(published_);
            if (!MatchesUnlocked(receipt)) return;
            const auto found = flights_.find(receipt.tileId);
            if (found == flights_.end() || receipt.slot >= found->second.acknowledged.size()) return;
            auto& flight = found->second; flight.acknowledged[receipt.slot] = true;
            if (receipt.slot == 0 && std::any_of(frames_.begin(), frames_.end(), [&](const auto& frame) { return Same(frame.receipt, receipt) && frame.reset; })) {
                std::fill(flight.acknowledged.begin(), flight.acknowledged.end(), false); flight.acknowledged[0] = true;
            }
            if (std::all_of(flight.acknowledged.begin(), flight.acknowledged.end(), [](bool value) { return value; })) {
                flight.readyToCommit = true; RemoveFramesUnlocked(receipt.tileId); ++flightRevision_; wake = true;
            }
        }
        if (wake) Wake();
    }
    void Fail(const Receipt& receipt) {
        const auto now = Now();
        {
            std::lock_guard<std::mutex> lock(published_); if (!MatchesUnlocked(receipt)) return;
            auto& flight = flights_.at(receipt.tileId); flight.attempt = ++attemptSequence_; flight.retryAfter = now + 5000;
            std::fill(flight.acknowledged.begin(), flight.acknowledged.end(), false); flight.readyToCommit = false; RemoveFramesUnlocked(receipt.tileId);
            ++flightRevision_;
            message_ = L"Gallery delivery failed. Retrying the same durable window; existing ownership is kept.";
        }
        Wake();
    }
    std::vector<MenuState> MenuStates(const std::wstring& profile, const Configuration& cfg) const {
        std::lock_guard<std::mutex> lock(published_); std::vector<MenuState> result;
        for (const auto& tile : cfg.tiles) {
            MenuState state; const auto saved = menu_.find(tile.id);
            if (profile == viewProfile_ && cfg.groupId == viewGroup_ && saved != menu_.end()) state = saved->second;
            state.id = tile.id; state.name = tile.name; state.nativeId = NativeTileId(cfg.groupId, tile.id); state.enabled = cfg.enabled && tile.enabled;
            if (!state.enabled) state.status = L"Paused; existing reservations are kept.";
            else if (state.status.empty()) state.status = L"Waiting for the gallery source. No inventory scan is started by this menu.";
            result.push_back(std::move(state));
        }
        return result;
    }
    void Stop() {
        std::lock_guard<std::mutex> operation(operation_); stopped_ = true; CancelWork(); InvalidateFlights();
        if (work_ && work_->thread.joinable()) work_->thread.join();
        work_.reset();
    }
};
} } }

#ifndef AIP_GALLERY_RUNTIME_NO_HOST
// Include after ga_live_tile/ga_rss_feed and before ga_content_runtime. Root
// declares the Snapshot hooks before generation code consumes them.
static bool CaptureGalleryConfiguration(aip::gallery::runtime::Input& input, std::wstring& error)
{
    std::wstring profile;
    { std::lock_guard<std::mutex> lock(g_pathMutex); profile = aip::MakeAbsolutePath(g_iniPath); }
    std::vector<aip::IniSectionData> document;
    {
        aip::IniWriteMutexGuard guard(profile, CurrentIniWriteLockWaitMs());
        if (!guard.IsLocked()) { error = L"Gallery configuration is temporarily locked."; return false; }
        std::vector<IniEntry> section; const bool refreshed = ReadIniSectionCached(L"Gallery", section);
        std::lock_guard<std::mutex> lock(g_iniCacheMutex);
        if (!g_iniCacheLoaded) { error = L"Gallery configuration is unavailable."; return false; }
        document = g_iniCacheDoc;
        if (!refreshed) for (const auto& existing : document) if (aip::IniEquals(existing.name, L"Gallery") && !existing.entries.empty()) {
            error = L"The current gallery configuration could not be refreshed."; return false;
        }
    }
    { std::lock_guard<std::mutex> lock(g_pathMutex); if (!IEquals(profile, aip::MakeAbsolutePath(g_iniPath))) { error = L"The active gallery profile changed."; return false; } }
    const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        std::wstring value; return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
    };
    aip::gallery::runtime::Input candidate; candidate.profile = profile;
    if (!aip::gallery::ReadConfiguration(read, candidate.configuration, error) ||
        !aip::content::ParseBool(read(L"Content", L"Enabled", L"0"), candidate.contentEnabled) ||
        !aip::content::ParseBool(read(L"Tiles", L"Enabled", L"0"), candidate.tilesEnabled)) {
        if (error.empty()) error = L"Gallery Content/Tiles enabled flags are invalid."; return false;
    }
    if (!candidate.configuration.folder.empty()) {
        if (PathIsRelativeW(candidate.configuration.folder.c_str())) candidate.configuration.folder = ParentDirectoryOfPath(profile) + L"\\" + candidate.configuration.folder;
        candidate.configuration.folder = aip::MakeAbsolutePath(candidate.configuration.folder);
    }
    candidate.directAvailable = candidate.configuration.enabled && candidate.contentEnabled && candidate.tilesEnabled && ContentDirectLiveDeliveryAvailable();
    input = std::move(candidate); error.clear(); return true;
}
static bool ReadGalleryConfiguration(aip::gallery::Configuration& cfg, std::wstring& error)
{
    aip::gallery::runtime::Input input; if (!CaptureGalleryConfiguration(input, error)) return false;
    cfg = std::move(input.configuration); return true;
}
static aip::gallery::runtime::ReadResult ReadGalleryLedgerNative(const std::wstring& path, aip::gallery::Ledger& ledger, std::wstring& error)
{
    using aip::gallery::runtime::ReadResult;
    aip::IniWriteMutexGuard guard(path, CurrentIniWriteLockWaitMs());
    if (!guard.IsLocked()) { error = L"Gallery ownership ledger is temporarily locked."; return ReadResult::Error; }
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return ReadResult::Missing;
        error = L"Gallery ownership ledger cannot be inspected."; return ReadResult::Error;
    }
    std::vector<BYTE> bytes;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) || !aip::ReadWholeFileBytes(path, bytes, aip::gallery::MaxLedgerBytes)) {
        error = L"Gallery ownership ledger is unavailable or exceeds its size limit."; return ReadResult::Error;
    }
    return aip::gallery::ParseLedger(std::string(bytes.begin(), bytes.end()), ledger, error) ? ReadResult::Ok : ReadResult::Error;
}
static aip::gallery::runtime::SaveResult SaveGalleryLedgerNative(const std::wstring& path, const aip::gallery::Ledger& expected,
    const aip::gallery::Ledger& candidate, std::wstring& error)
{
    using aip::gallery::runtime::SaveResult;
    aip::IniWriteMutexGuard guard(path, CurrentIniWriteLockWaitMs());
    if (!guard.IsLocked()) { error = L"Gallery ownership ledger is temporarily locked."; return SaveResult::Error; }
    aip::gallery::Ledger fresh;
    if (ReadGalleryLedgerNative(path, fresh, error) != aip::gallery::runtime::ReadResult::Ok) return SaveResult::Conflict;
    std::string after;
    const auto decision = aip::gallery::runtime::PrepareLedgerCas(expected, fresh, candidate, after, error);
    if (decision != SaveResult::Saved) return decision;
    if (!WriteWholeFileBytes(path, std::vector<BYTE>(after.begin(), after.end()))) { error = L"The gallery ownership ledger could not be atomically saved."; return SaveResult::Error; }
    error.clear(); return SaveResult::Saved;
}
static bool CreateGalleryLedgerNative(const std::wstring& path, const aip::gallery::Ledger& ledger, std::wstring& error)
{
    aip::IniWriteMutexGuard guard(path, CurrentIniWriteLockWaitMs());
    if (!guard.IsLocked()) { error = L"Gallery ownership ledger is temporarily locked."; return false; }
    std::string text; if (!aip::gallery::SerializeLedger(ledger, text, error)) return false;
    const auto temporary = MakeTempSiblingPath(path, L"gallery");
    if (!WriteWholeFileBytesDirect(temporary, std::vector<BYTE>(text.begin(), text.end()))) { DeleteFileW(temporary.c_str()); error = L"The initial gallery ledger could not be written."; return false; }
    // No REPLACE_EXISTING: first setup must never overwrite historical data.
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) { DeleteFileW(temporary.c_str()); error = L"Gallery ownership already exists or cannot be created. Existing data was kept."; return false; }
    error.clear(); return true;
}
static aip::gallery::runtime::Services GalleryNativeServices()
{
    aip::gallery::runtime::Services services;
    services.readLedger = ReadGalleryLedgerNative; services.saveLedger = SaveGalleryLedgerNative; services.createLedger = CreateGalleryLedgerNative;
    services.now = [] { return static_cast<std::uint64_t>(GetTickCount64()); }; services.wake = [] { WakePollThread(); };
    services.cacheDirectory = [](const std::wstring& profile) {
        std::wstring identity, hash;
        if (!aip::named_objects::NormalizeFileIdentity(profile, identity) || !aip::named_objects::StableIdentityHash(identity, hash)) return std::wstring();
        const auto base = LocalAppDataPath(); if (base.empty()) return std::wstring();
        const auto path = base + L"\\DesktopStub\\Gallery\\" + hash;
        return EnsureDirectoryTreeExists(path) ? path : std::wstring();
    };
    services.scan = [](const auto& folder, const auto& cache, const auto& index, const auto& cancel) {
#ifndef DESKTOPSTUB_INERT_HARDWARE
        return aip::gallery::ScanGalleryInventory(folder, cache, index, cancel);
#else
        (void)folder; (void)cache; (void)index; (void)cancel;
        aip::gallery::InventoryResult result; result.complete = true; return result;
#endif
    };
    return services;
}
static aip::gallery::runtime::Coordinator g_galleryRuntime(GalleryNativeServices());
static aip::gallery::runtime::Receipt GalleryReceipt(const aip::content::Snapshot& frame)
{
    return {frame.galleryGroupId, frame.galleryTileId, frame.galleryEpoch, frame.secondaryFrameKey,
        frame.galleryTicket, frame.gallerySlot, frame.gallerySlotCount};
}
static void RefreshGallerySource()
{
#ifndef DESKTOPSTUB_INERT_HARDWARE
    aip::gallery::runtime::Input input; std::wstring error;
    if (CaptureGalleryConfiguration(input, error)) g_galleryRuntime.Refresh(input);
    else { input.configuration.enabled = false; g_galleryRuntime.Refresh(input); }
#endif
}
static std::vector<aip::content::Snapshot> CurrentGalleryFrames()
{
    std::vector<aip::content::Snapshot> result;
    for (const auto& source : g_galleryRuntime.CurrentFrames()) {
        aip::content::Snapshot frame; frame.active = true; frame.name = source.name; frame.backgroundPath = source.path;
        frame.backgroundId = aip::content::BackgroundId::Image; frame.textMode = aip::content::TextMode::Off;
        frame.activationArguments = source.activationArguments; frame.nativeQueue = true; frame.nativeQueueReset = source.reset;
        frame.nativeQueueTag = source.tag; frame.nativeQueueLayout = source.layout; frame.nativeQueueFrameKey = source.receipt.frameKey;
        frame.secondaryTileId = source.nativeId; frame.secondaryFrameKey = source.receipt.frameKey; frame.secondaryBranding = source.branding;
        frame.galleryFrame = true; frame.galleryContain = source.contain; frame.galleryGroupId = source.receipt.groupId;
        frame.galleryTileId = source.receipt.tileId; frame.galleryEpoch = source.receipt.epoch; frame.galleryTicket = source.receipt.ticket;
        frame.gallerySlot = source.receipt.slot; frame.gallerySlotCount = source.receipt.count; result.push_back(std::move(frame));
    }
    return result;
}
static bool ValidateGalleryFrame(const aip::content::Snapshot& frame)
{
    if (!frame.galleryFrame || !g_galleryRuntime.Validate(GalleryReceipt(frame))) return false;
    aip::gallery::runtime::Input input; std::wstring error;
    if (!CaptureGalleryConfiguration(input, error) || !g_galleryRuntime.IsProfile(input.profile) || !input.configuration.enabled || !input.contentEnabled || !input.tilesEnabled || !input.directAvailable ||
        input.configuration.groupId != frame.galleryGroupId || (input.configuration.fit == aip::gallery::Fit::Fit) != frame.galleryContain ||
        !frame.gallerySlotCount || frame.gallerySlotCount > input.configuration.queueSize || frame.gallerySlot >= frame.gallerySlotCount) return false;
    const auto tile = std::find_if(input.configuration.tiles.begin(), input.configuration.tiles.end(), [&](const auto& value) {
        return value.enabled && value.id == frame.galleryTileId && value.name == frame.name && value.appearance.branding == frame.secondaryBranding &&
            aip::gallery::NativeTileId(input.configuration.groupId, value.id) == frame.secondaryTileId;
    });
    if (tile == input.configuration.tiles.end() || frame.activationArguments != L"--ini " + aip::QuoteCommandLineArg(input.profile) + L" --open-folder " + aip::QuoteCommandLineArg(input.configuration.folder)) return false;
    const auto approved = g_galleryRuntime.CurrentFrames();
    return std::any_of(approved.begin(), approved.end(), [&](const auto& source) {
        return source.receipt.frameKey == frame.secondaryFrameKey && source.path == frame.backgroundPath && source.nativeId == frame.secondaryTileId &&
            source.tag == frame.nativeQueueTag && source.layout == frame.nativeQueueLayout && source.reset == frame.nativeQueueReset && frame.nativeQueue;
    });
}
static void AcknowledgeGalleryFrame(const aip::content::Snapshot& frame) { if (frame.galleryFrame) g_galleryRuntime.Acknowledge(GalleryReceipt(frame)); }
static void FailGalleryFrame(const aip::content::Snapshot& frame) { if (frame.galleryFrame) g_galleryRuntime.Fail(GalleryReceipt(frame)); }
static void StopGallerySource() { g_galleryRuntime.Stop(); }
static void RequestGalleryRefresh(bool reloadLedger = false) { g_galleryRuntime.RequestRefresh(reloadLedger); }
static std::wstring GallerySourceStatus() { return g_galleryRuntime.Status(); }
static std::vector<aip::gallery::runtime::MenuState> GalleryTileMenuStates()
{
    aip::gallery::runtime::Input input; std::wstring error;
    return CaptureGalleryConfiguration(input, error) ? g_galleryRuntime.MenuStates(input.profile, input.configuration) : std::vector<aip::gallery::runtime::MenuState>();
}
static bool CreateGalleryLedgerForProfile(const std::wstring& profile, const aip::gallery::Configuration& cfg,
    const std::wstring& epoch, bool historyKnownNewOrRetired, std::wstring& error)
{
    return aip::gallery::runtime::Initialize(GalleryNativeServices(), aip::MakeAbsolutePath(profile), cfg, epoch, historyKnownNewOrRetired, error);
}
static bool PrepareGalleryConfigurationSettings(const aip::gallery::Configuration& cfg, std::vector<aip::IniSetting>& output, std::wstring& error)
{
    if (!aip::gallery::ValidateConfiguration(cfg, error)) return false;
    if (cfg.tiles.empty()) { error = L"Gallery configuration needs at least one numbered slot."; return false; }
    std::vector<aip::IniSetting> changes{{L"Gallery", L"Enabled", cfg.enabled ? L"1" : L"0"}, {L"Gallery", L"GroupId", cfg.groupId},
        {L"Gallery", L"Folder", cfg.folder}, {L"Gallery", L"TileCount", std::to_wstring(cfg.tiles.size())},
        {L"Gallery", L"QueueSize", std::to_wstring(cfg.queueSize)}, {L"Gallery", L"RefreshSeconds", std::to_wstring(cfg.refreshSeconds)},
        {L"Gallery", L"Order", cfg.order == aip::gallery::Order::Shuffle ? L"Shuffle" : L"Sequential"},
        {L"Gallery", L"Seed", std::to_wstring(cfg.seed)}, {L"Gallery", L"Fit", cfg.fit == aip::gallery::Fit::Fit ? L"Fit" : L"Fill"}};
    for (std::size_t index = 0; index < cfg.tiles.size(); ++index) {
        const auto section = L"Gallery.Tile." + std::to_wstring(index + 1); const auto& tile = cfg.tiles[index];
        changes.push_back({section, L"Id", tile.id}); changes.push_back({section, L"Name", tile.name}); changes.push_back({section, L"Enabled", tile.enabled ? L"1" : L"0"});
        changes.push_back({section, L"Background", tile.appearance.background}); changes.push_back({section, L"Branding", tile.appearance.branding});
        changes.push_back({section, L"BackgroundColor", tile.appearance.backgroundColor});
    }
    output = std::move(changes); error.clear(); return true;
}
#endif
