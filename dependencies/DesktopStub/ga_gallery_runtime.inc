#pragma once
// Gallery source coordinator. The injected core is portable; the host wrappers
// below perform explicit native INI/ledger work only when called by the host.
#include "../gallery_engine.h"
#include "../gallery_transaction.h"
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
inline SaveResult PrepareLedgerReset(const Ledger& expected, const Ledger& fresh, const std::wstring& newEpoch,
    bool allQueuesClearedConfirmed, std::string& bytes, std::wstring& error) {
    std::string before, actual, after; Ledger candidate;
    if (!SerializeLedger(expected, before, error) || !SerializeLedger(fresh, actual, error)) return SaveResult::Error;
    if (before != actual) { error = L"Gallery history changed before redistribution. The newer ledger was kept."; return SaveResult::Conflict; }
    if (!ResetLedger(fresh, newEpoch, allQueuesClearedConfirmed, candidate, error) || !SerializeLedger(candidate, after, error)) return SaveResult::Error;
    bytes = std::move(after); error.clear(); return SaveResult::Saved;
}
struct Input {
    std::wstring profile;
    Configuration configuration;
    bool contentEnabled = false, tilesEnabled = false, directAvailable = false;
    bool pinnedKnown = false;
    std::set<std::wstring> pinnedIds; // Native IDs, copied once from the host cache.
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
struct StatusWindow {
    std::vector<Frame> frames;
    bool submitted = false; // Status only; never a source of delivery work.
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
        Pack(tile.appearance.background) + Pack(tile.appearance.branding) + Pack(tile.appearance.backgroundColor) + Pack(tile.folder) + Pack(tile.galleryName);
    return key;
}
inline std::wstring InventoryFolderKey(std::wstring folder) {
    folder = detail::Lower(std::move(folder)); std::replace(folder.begin(), folder.end(), L'/', L'\\');
    while (folder.size() > 3 && folder.back() == L'\\') folder.pop_back();
    return folder;
}
class Coordinator {
    struct Work {
        std::thread thread;
        std::atomic<bool> canceled{false}, done{false};
        bool failed = false;
        std::wstring context, folderKey;
        InventoryResult result;
    };
    struct Flight {
        QueueWindow window;
        std::vector<bool> acknowledged;
        std::uint64_t attempt = 1, retryAfter = 0;
        bool readyToCommit = false;
    };
    struct FolderState {
        std::wstring path;
        InventoryIndex index;
        std::map<std::wstring, std::wstring> paths;
        std::shared_ptr<const std::set<std::wstring>> imageIds;
        std::uint64_t nextScan = 0;
        bool ready = false;
    };
    Services services_;
    std::mutex operation_;
    mutable std::mutex published_;
    std::atomic<bool> stopped_{false}, suspended_{false}, refreshRequested_{false}, reloadRequested_{false};
    std::wstring context_, profile_, configKey_, poolKey_, cacheDirectory_, message_, inventoryMessage_;
    Configuration cfg_;
    Ledger ledger_;
    bool ledgerReady_ = false, inventoryReady_ = false, poolsReady_ = false, eligible_ = false;
    std::uint64_t inventoryRevision_ = 0, poolRevision_ = 0, retryAfter_ = 0, nextScan_ = 0, attemptSequence_ = 0;
    std::uint64_t flightRevision_ = 0, plannedLedger_ = 0, plannedInventory_ = 0, plannedFlights_ = 0;
    std::wstring plannedConfig_;
    std::set<std::wstring> plannedDue_;
    bool planCached_ = false;
    std::map<std::wstring, FolderState> folders_;
    std::set<std::wstring> activeTiles_, activeFolders_;
    std::wstring lastScannedFolder_, selectionConfig_;
    std::set<std::wstring> selectionPins_;
    bool selectionKnown_ = false;
    EligibilityMap eligibility_;
    std::map<std::wstring, std::wstring> paths_;
    PoolSnapshot pools_;
    std::shared_ptr<Work> work_;
    std::map<std::wstring, std::uint64_t> lastAccepted_;
    std::map<std::wstring, std::size_t> lastQueueCount_;
    // The following data is protected by published_; delivery callbacks never
    // touch the ledger, inventory, worker, or filesystem.
    std::map<std::wstring, Flight> flights_;
    std::vector<Frame> frames_;
    std::map<std::wstring, std::vector<Frame>> submittedStatus_;
    std::wstring statusConfigurationKey_;
    std::wstring viewProfile_, viewGroup_;
    std::map<std::wstring, MenuState> menu_;
    void Wake() const { if (services_.wake) services_.wake(); }
    std::uint64_t Now() const { return services_.now ? services_.now() : 0; }
    static bool Same(const Receipt& left, const Receipt& right) {
        return left.groupId == right.groupId && left.tileId == right.tileId && left.epoch == right.epoch &&
            left.ticket == right.ticket && left.slot == right.slot && left.count == right.count && left.frameKey == right.frameKey;
    }
    bool MatchesUnlocked(const Receipt& receipt) const {
        if (suspended_.load()) return false;
        return std::any_of(frames_.begin(), frames_.end(), [&](const auto& frame) { return Same(frame.receipt, receipt); });
    }
    void RemoveFramesUnlocked(const std::wstring& tile) {
        frames_.erase(std::remove_if(frames_.begin(), frames_.end(), [&](const auto& frame) { return frame.receipt.tileId == tile; }), frames_.end());
    }
    static bool CompleteStatusWindow(const std::vector<Frame>& frames) {
        if (frames.empty() || frames.size() > MaxQueue || frames.front().receipt.count != frames.size()) return false;
        const auto& first = frames.front().receipt;
        std::vector<bool> slots(frames.size(), false);
        for (const auto& frame : frames) {
            const auto& receipt = frame.receipt;
            if (receipt.groupId != first.groupId || receipt.tileId != first.tileId || receipt.epoch != first.epoch || receipt.ticket != first.ticket ||
                receipt.count != first.count || receipt.slot >= slots.size() || slots[receipt.slot]) return false;
            slots[receipt.slot] = true;
        }
        return true;
    }
    void PublishStatus(const std::wstring& message, bool clearFrames = false) {
        std::lock_guard<std::mutex> lock(published_);
        std::map<std::wstring, const TilePool*> poolById;
        if (poolsReady_) for (const auto& pool : pools_.pools) poolById.emplace(pool.tileId, &pool);
        message_ = message; viewProfile_ = profile_; viewGroup_ = cfg_.groupId;
        if (clearFrames) frames_.clear();
        menu_.clear();
        for (const auto& tile : cfg_.tiles) {
            MenuState state; state.id = tile.id; state.nativeId = NativeTileId(cfg_.groupId, tile.id); state.name = tile.name;
            state.enabled = cfg_.enabled && tile.enabled; state.ledgerReady = ledgerReady_;
            if (poolsReady_) {
                const auto pool = poolById.find(tile.id);
                if (pool != poolById.end()) { state.available = pool->second->images.size(); state.reserved = pool->second->reservedCount; }
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
    void InvalidateFlights(bool clearSubmitted = false) {
        std::lock_guard<std::mutex> lock(published_); frames_.clear(); flights_.clear();
        if (clearSubmitted) submittedStatus_.clear();
        ++flightRevision_;
    }
    bool Save(const Ledger& candidate, std::uint64_t now, std::wstring& error) {
        const auto saved = services_.saveLedger(LedgerPath(profile_), ledger_, candidate, error);
        if (saved == SaveResult::Saved) { ledger_ = candidate; return true; }
        if (error.empty()) error = saved == SaveResult::Conflict ? L"Gallery ownership changed in another writer. Waiting to reload it safely." : L"The gallery ledger could not be saved. No new gallery frames were published.";
        retryAfter_ = now + 5000;
        if (saved == SaveResult::Conflict) { ledgerReady_ = false; poolsReady_ = false; InvalidateFlights(); }
        PublishStatus(error, true); return false;
    }
    void UpdateInventoryView() {
        std::map<std::wstring, std::wstring> usable; EligibilityMap eligibility;
        for (const auto& folder : folders_) if (folder.second.ready)
            usable.insert(folder.second.paths.begin(), folder.second.paths.end());
        for (const auto& tile : cfg_.tiles) {
            const auto folder = folders_.find(InventoryFolderKey(EffectiveFolder(cfg_, tile)));
            if (folder != folders_.end() && folder->second.ready) eligibility.emplace(tile.id, folder->second.imageIds);
        }
        if (usable.size() > MaxImages) { inventoryReady_ = false; inventoryMessage_ = L"The combined gallery inventory exceeds its supported image limit."; return; }
        if (usable != paths_ || eligibility != eligibility_) ++inventoryRevision_;
        paths_ = std::move(usable); eligibility_ = std::move(eligibility);
        inventoryReady_ = std::any_of(activeFolders_.begin(), activeFolders_.end(), [&](const auto& key) {
            const auto found = folders_.find(key); return found != folders_.end() && found->second.ready;
        });
    }
    bool SelectFolders(const Input& input) {
        if (selectionConfig_ == configKey_ && selectionKnown_ == input.pinnedKnown && selectionPins_ == input.pinnedIds) return false;
        selectionConfig_ = configKey_; selectionKnown_ = input.pinnedKnown; selectionPins_ = input.pinnedIds;
        std::set<std::wstring> desired; activeTiles_.clear(); activeFolders_.clear();
        for (const auto& tile : cfg_.tiles) {
            const auto& path = EffectiveFolder(cfg_, tile); if (path.empty()) continue;
            const auto key = InventoryFolderKey(path); desired.insert(key); folders_[key].path = path;
            if (input.pinnedKnown && input.pinnedIds.find(NativeTileId(cfg_.groupId, tile.id)) != input.pinnedIds.end()) {
                activeTiles_.insert(tile.id); activeFolders_.insert(key);
            }
        }
        for (auto folder = folders_.begin(); folder != folders_.end();) {
            if (desired.find(folder->first) == desired.end()) folder = folders_.erase(folder); else ++folder;
        }
        if (work_ && activeFolders_.find(work_->folderKey) == activeFolders_.end()) CancelWork();
        {
            std::lock_guard<std::mutex> lock(published_);
            for (auto item = submittedStatus_.begin(); item != submittedStatus_.end();) {
                if (activeTiles_.find(item->first) == activeTiles_.end()) item = submittedStatus_.erase(item); else ++item;
            }
        }
        UpdateInventoryView(); return true;
    }
    bool Reap(std::uint64_t now) {
        if (!work_ || !work_->done.load(std::memory_order_acquire)) return false;
        const auto completed = work_;
        if (completed->thread.joinable()) completed->thread.join();
        work_.reset();
        if (completed->canceled.load() || completed->context != context_) return false;
        const auto found = folders_.find(completed->folderKey); if (found == folders_.end()) return false;
        auto& folder = found->second;
        if (completed->failed || completed->result.cancelled) { folder.nextScan = now + 5000; inventoryMessage_ = L"Gallery inventory work failed or was canceled. Retrying without changing ownership."; return false; }
        inventoryMessage_ = completed->result.diagnostics.empty() ? L"" : completed->result.diagnostics.front().substr(0, 256);
        const bool progressed = completed->result.deferredFiles && completed->result.index.cursor != folder.index.cursor;
        folder.index = std::move(completed->result.index);
        if (!completed->result.complete) { folder.nextScan = now + (progressed ? 250 : 30000); return false; }
        std::map<std::wstring, std::wstring> usable;
        for (const auto& image : completed->result.images) {
            if (!OpaqueImageId(image.id) || image.thumbnailPath.empty()) { folder.nextScan = now + 5000; return false; }
            const auto inserted = usable.emplace(image.id, image.thumbnailPath);
            if (!inserted.second && inserted.first->second != image.thumbnailPath) { folder.nextScan = now + 5000; return false; }
        }
        if (!folder.ready || usable != folder.paths) {
            auto ids = std::make_shared<std::set<std::wstring>>(); for (const auto& image : usable) ids->insert(image.first);
            folder.paths = std::move(usable); folder.imageIds = std::move(ids);
        }
        folder.ready = true; folder.nextScan = now + 30000; UpdateInventoryView(); return true;
    }
    bool StartScan(std::uint64_t now) {
        if (work_ || activeFolders_.empty() || now < nextScan_) return false;
        auto selected = activeFolders_.end();
        for (const auto& key : activeFolders_) if (folders_.at(key).nextScan <= now) {
            if (selected == activeFolders_.end()) selected = activeFolders_.find(key);
            if (key > lastScannedFolder_) { selected = activeFolders_.find(key); break; }
        }
        if (selected == activeFolders_.end()) return false;
        if (cacheDirectory_.empty()) cacheDirectory_ = services_.cacheDirectory(profile_);
        if (cacheDirectory_.empty()) { nextScan_ = now + 5000; return false; }
        const auto current = std::make_shared<Work>(); current->context = context_; current->folderKey = *selected;
        const auto scan = services_.scan; const auto wake = services_.wake;
        auto& selectedFolder = folders_.at(*selected);
        const auto folder = selectedFolder.path, cache = cacheDirectory_; const auto previous = selectedFolder.index;
        lastScannedFolder_ = *selected;
        work_ = current;
        try {
            current->thread = std::thread([current, scan, wake, folder, cache, previous] {
                try { current->result = scan(folder, cache, previous, [current] { return current->canceled.load(); }); }
                catch (...) { current->failed = true; }
                current->done.store(true, std::memory_order_release);
                try { if (wake) wake(); } catch (...) {}
            });
        } catch (...) { current->canceled = true; work_.reset(); selectedFolder.nextScan = now + 5000; return false; }
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
        if (!ReconcileOwnership(cfg_, ids, ledger_, ownership, error, &eligibility_)) { PublishStatus(error, true); return false; }
        if (ownership.requiresLedgerCommit && !Save(ownership.ledger, now, error)) return false;
        pools_ = std::move(ownership.pools); poolsReady_ = true; poolRevision_ = inventoryRevision_; poolKey_ = configKey_; return true;
    }
    bool Prepare(std::uint64_t now, bool force, std::wstring& error) {
        std::set<std::wstring> due;
        for (const auto& tile : cfg_.tiles) if (activeTiles_.find(tile.id) != activeTiles_.end()) {
            const auto folder = folders_.find(InventoryFolderKey(EffectiveFolder(cfg_, tile)));
            // An unscanned/unavailable different folder must neither stall good
            // galleries nor invalidate its own previously persisted window.
            if (folder == folders_.end() || !folder->second.ready) continue;
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
            currentMembers = activeTiles_;
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
                    frame.activationArguments = L"--ini " + Quote(profile_) + L" --open-folder " + Quote(EffectiveFolder(cfg_, *tile));
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
        if (stopped_ || suspended_) return;
        try {
        const auto now = Now(); std::wstring error;
        const auto context = Pack(input.profile) + Pack(input.configuration.groupId);
        if (context != context_) {
            CancelWork(); context_ = context; profile_ = input.profile; cacheDirectory_.clear(); inventoryMessage_.clear(); folders_.clear(); paths_.clear();
            eligibility_.clear(); selectionConfig_.clear(); selectionPins_.clear(); activeTiles_.clear(); activeFolders_.clear(); lastScannedFolder_.clear();
            ledger_ = {}; ledgerReady_ = inventoryReady_ = poolsReady_ = false; retryAfter_ = nextScan_ = 0;
            lastAccepted_.clear(); lastQueueCount_.clear(); InvalidateFlights(true);
        }
        auto effective = input.configuration;
        effective.enabled = !effective.groupId.empty();
        for (auto& tile : effective.tiles) tile.enabled = true; // Configured pools survive legacy publication switches.
        const auto newKey = ConfigKey(effective);
        const bool configChanged = !configKey_.empty() && newKey != configKey_;
        cfg_ = std::move(effective); configKey_ = newKey;
        {
            std::lock_guard<std::mutex> lock(published_);
            statusConfigurationKey_ = configKey_;
            if (configChanged) {
                for (auto& item : flights_) {
                    item.second.attempt = ++attemptSequence_; item.second.readyToCommit = false;
                    std::fill(item.second.acknowledged.begin(), item.second.acknowledged.end(), false);
                }
                frames_.clear();
                submittedStatus_.clear();
                ++flightRevision_;
            }
        }
        Reap(now);
        bool force = refreshRequested_.exchange(false) || configChanged;
        if (reloadRequested_.exchange(false)) { ledgerReady_ = poolsReady_ = false; retryAfter_ = nextScan_ = 0; InvalidateFlights(); }
        if (!ValidateConfiguration(cfg_, error) || profile_.empty()) {
            CancelWork(); { std::lock_guard<std::mutex> lock(published_); submittedStatus_.clear(); }
            PublishStatus(error.empty() ? L"Gallery profile is unavailable." : error, true); return;
        }
        const bool selectionChanged = SelectFolders(input);
        if (selectionChanged) { InvalidateFlights(); force = true; }
        if (!cfg_.enabled || !input.directAvailable) {
            CancelWork(); ledgerReady_ = poolsReady_ = eligible_ = false; InvalidateFlights(true);
            PublishStatus(!cfg_.enabled ? L"Set up a gallery group to begin. Historical ownership is kept." :
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
            if (!ledger_.epochId.empty() && fresh.epochId != ledger_.epochId) {
                std::lock_guard<std::mutex> lock(published_); submittedStatus_.clear();
            }
            ledger_ = std::move(fresh); ledgerReady_ = true; poolsReady_ = false;
        }
        if (!CommitAcknowledgements(now, error)) return;
        if (!input.pinnedKnown || activeTiles_.empty()) {
            CancelWork(); InvalidateFlights(); eligible_ = false;
            PublishStatus(input.pinnedKnown ? L"Pin a gallery tile to begin updates. Configured tile pools and historical ownership are kept." :
                L"Waiting for Windows' pinned-tile inventory. No photo scan has started.", true); return;
        }
        if (force) for (const auto& key : activeFolders_) folders_.at(key).nextScan = 0;
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
    void Suspend() {
        suspended_ = true; std::lock_guard<std::mutex> operation(operation_); CancelWork(); InvalidateFlights(true);
    }
    void Resume(bool reloadLedger = true) {
        refreshRequested_ = true; if (reloadLedger) reloadRequested_ = true; suspended_ = false; Wake();
    }
    std::vector<Frame> CurrentFrames() const { std::lock_guard<std::mutex> lock(published_); return suspended_ ? std::vector<Frame>() : frames_; }
    std::vector<StatusWindow> StatusWindows(const std::wstring& configurationKey = {}) const {
        std::lock_guard<std::mutex> lock(published_); std::vector<StatusWindow> result;
        if (suspended_ || (!configurationKey.empty() && configurationKey != statusConfigurationKey_)) return result;
        std::map<std::wstring, std::vector<Frame>> prepared;
        for (const auto& frame : frames_) prepared[frame.receipt.tileId].push_back(frame);
        std::set<std::wstring> included;
        for (auto& item : prepared) if (CompleteStatusWindow(item.second)) {
            std::sort(item.second.begin(), item.second.end(), [](const auto& left, const auto& right) { return left.receipt.slot < right.receipt.slot; });
            result.push_back({std::move(item.second), false}); included.insert(item.first);
        }
        for (const auto& item : submittedStatus_) if (included.find(item.first) == included.end()) result.push_back({item.second, true});
        return result;
    }
    std::wstring Status() const { std::lock_guard<std::mutex> lock(published_); return message_; }
    bool IsProfile(const std::wstring& profile) const { std::lock_guard<std::mutex> lock(published_); return profile == viewProfile_; }
    bool Validate(const Receipt& receipt) const { std::lock_guard<std::mutex> lock(published_); return MatchesUnlocked(receipt); }
    bool ValidateFrame(const Frame& expected) const {
        std::lock_guard<std::mutex> lock(published_);
        if (suspended_.load()) return false;
        return std::any_of(frames_.begin(), frames_.end(), [&](const auto& frame) {
            return Same(frame.receipt, expected.receipt) && frame.nativeId == expected.nativeId && frame.path == expected.path &&
                frame.name == expected.name && frame.activationArguments == expected.activationArguments && frame.layout == expected.layout &&
                frame.tag == expected.tag && frame.branding == expected.branding && frame.reset == expected.reset && frame.contain == expected.contain;
        });
    }
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
                std::vector<Frame> submitted;
                for (const auto& frame : frames_) if (frame.receipt.tileId == receipt.tileId && frame.receipt.epoch == receipt.epoch && frame.receipt.ticket == receipt.ticket)
                    submitted.push_back(frame);
                if (CompleteStatusWindow(submitted)) {
                    std::sort(submitted.begin(), submitted.end(), [](const auto& left, const auto& right) { return left.receipt.slot < right.receipt.slot; });
                    submittedStatus_[receipt.tileId] = std::move(submitted);
                }
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
            state.id = tile.id; state.name = tile.name; state.nativeId = NativeTileId(cfg.groupId, tile.id); state.enabled = !cfg.groupId.empty() && !tile.id.empty();
            if (!state.enabled) state.status = L"Paused; existing reservations are kept.";
            else if (state.status.empty()) state.status = L"Waiting for the gallery source. No inventory scan is started by this menu.";
            result.push_back(std::move(state));
        }
        return result;
    }
    void Stop() {
        std::lock_guard<std::mutex> operation(operation_); stopped_ = true; CancelWork(); InvalidateFlights(true);
        if (work_ && work_->thread.joinable()) work_->thread.join();
        work_.reset();
    }
};
} } }

#ifndef AIP_GALLERY_RUNTIME_NO_HOST
static bool GalleryTransactionPending(const std::wstring& profile)
{
    const auto attributes = GetFileAttributesW(aip::gallery::transaction::Path(profile).c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) return true;
    const auto code = GetLastError();
    return code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND;
}
// Include after ga_live_tile/ga_rss_feed and before ga_content_runtime. Root
// declares the Snapshot hooks before generation code consumes them.
struct GalleryConfigurationView {
    std::wstring profile;
    aip::gallery::Configuration configuration;
    unsigned long long revision = 0;
    bool contentEnabled = false, tilesEnabled = false;
};
static std::mutex g_galleryConfigurationViewMutex;
static std::shared_ptr<const GalleryConfigurationView> g_galleryConfigurationView;
static std::shared_ptr<const GalleryConfigurationView> CaptureGalleryConfigurationView(std::wstring& error, bool requireFresh)
{
    std::wstring profile;
    { std::lock_guard<std::mutex> lock(g_pathMutex); profile = aip::MakeAbsolutePath(g_iniPath); }
    const auto transactionError = [&] {
        error = L"An interrupted gallery change needs recovery. Open Photo galleries > Recover interrupted change, or restart DesktopStub. Existing ownership is preserved.";
    };
    if (GalleryTransactionPending(profile)) { transactionError(); return {}; }
    aip::IniWriteMutexGuard guard(profile, CurrentIniWriteLockWaitMs());
    if (!guard.IsLocked()) { error = L"Gallery configuration is temporarily locked."; return {}; }
    if (requireFresh) {
        // Publication fences observe actual bytes, including edits that retain
        // the old timestamp. Identical bytes reuse the parsed INI document.
        std::wstring text;
        if (!aip::IniConfigStore(profile).ReadFreshText(text)) { error = L"Gallery configuration could not be refreshed."; return {}; }
        std::lock_guard<std::mutex> pathLock(g_pathMutex);
        if (!IEquals(profile, aip::MakeAbsolutePath(g_iniPath))) { error = L"The active gallery profile changed."; return {}; }
        std::lock_guard<std::mutex> cacheLock(g_iniCacheMutex); SetIniCacheUnlocked(text);
    } else {
        std::vector<IniEntry> section;
        bool cacheReady = false;
        ReadIniSectionCached(L"Gallery", section, &cacheReady);
        if (!cacheReady) { error = L"The current gallery configuration could not be refreshed."; return {}; }
    }
    std::unique_lock<std::mutex> viewLock(g_galleryConfigurationViewMutex);
    std::vector<aip::IniSectionData> document;
    unsigned long long revision = 0;
    std::shared_ptr<const GalleryConfigurationView> view;
    {
        std::lock_guard<std::mutex> lock(g_iniCacheMutex);
        if (!g_iniCacheLoaded || !IEquals(profile, g_iniCacheProfile)) { error = L"Gallery configuration is unavailable."; return {}; }
        revision = g_iniCacheRevision;
        if (g_galleryConfigurationView && g_galleryConfigurationView->profile == profile && g_galleryConfigurationView->revision == revision)
            view = g_galleryConfigurationView;
        else document = g_iniCacheDoc;
    }
    if (!view) {
#ifdef DESKTOPSTUB_TEST_GALLERY_CONFIGURATION_PARSED
        DESKTOPSTUB_TEST_GALLERY_CONFIGURATION_PARSED();
#endif
        const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            std::wstring value; return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
        };
        auto candidate = std::make_shared<GalleryConfigurationView>(); candidate->profile = profile; candidate->revision = revision;
        if (!aip::gallery::ReadConfiguration(read, candidate->configuration, error) ||
            !aip::content::ParseBool(read(L"Content", L"Enabled", L"0"), candidate->contentEnabled) ||
            !aip::content::ParseBool(read(L"Tiles", L"Enabled", L"0"), candidate->tilesEnabled)) {
            if (error.empty()) error = L"Gallery Content/Tiles enabled flags are invalid."; return {};
        }
        const auto absoluteFolder = [&](std::wstring& folder) {
            if (folder.empty()) return;
            if (PathIsRelativeW(folder.c_str())) folder = ParentDirectoryOfPath(profile) + L"\\" + folder;
            folder = aip::MakeAbsolutePath(folder);
        };
        absoluteFolder(candidate->configuration.folder);
        for (auto& tile : candidate->configuration.tiles) absoluteFolder(tile.folder);
        view = std::move(candidate); g_galleryConfigurationView = view;
    }
    viewLock.unlock();
    { std::lock_guard<std::mutex> lock(g_pathMutex); if (!IEquals(profile, aip::MakeAbsolutePath(g_iniPath))) { error = L"The active gallery profile changed."; return {}; } }
    if (GalleryTransactionPending(profile)) { transactionError(); return {}; }
    error.clear(); return view;
}
static bool CaptureGalleryConfiguration(aip::gallery::runtime::Input& input, std::wstring& error)
{
    const auto view = CaptureGalleryConfigurationView(error, false); if (!view) return false;
    aip::gallery::runtime::Input candidate; candidate.profile = view->profile; candidate.configuration = view->configuration;
    candidate.contentEnabled = view->contentEnabled; candidate.tilesEnabled = view->tilesEnabled;
    candidate.pinnedKnown = SecondaryTileInventoryReady(); candidate.pinnedIds = CachedSecondaryTileIds();
    candidate.directAvailable = ContentDirectLiveDeliveryAvailable();
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
static aip::content::Snapshot GallerySnapshotForFrame(const aip::gallery::runtime::Frame& source)
{
        aip::content::Snapshot frame; frame.active = true; frame.name = source.name; frame.backgroundPath = source.path;
        frame.backgroundId = aip::content::BackgroundId::Image; frame.textMode = aip::content::TextMode::Off;
        frame.activationArguments = source.activationArguments; frame.nativeQueue = true; frame.nativeQueueReset = source.reset;
        frame.nativeQueueTag = source.tag; frame.nativeQueueLayout = source.layout; frame.nativeQueueFrameKey = source.receipt.frameKey;
        frame.secondaryTileId = source.nativeId; frame.secondaryFrameKey = source.receipt.frameKey; frame.secondaryBranding = source.branding;
        frame.galleryFrame = true; frame.galleryContain = source.contain; frame.galleryGroupId = source.receipt.groupId;
        frame.galleryTileId = source.receipt.tileId; frame.galleryEpoch = source.receipt.epoch; frame.galleryTicket = source.receipt.ticket;
        frame.gallerySlot = source.receipt.slot; frame.gallerySlotCount = source.receipt.count;
        return frame;
}
static std::vector<aip::content::Snapshot> CurrentGalleryFrames()
{
    std::vector<aip::content::Snapshot> result;
    for (const auto& source : g_galleryRuntime.CurrentFrames()) result.push_back(GallerySnapshotForFrame(source));
    return result;
}
struct GalleryStatusSnapshots {
    std::vector<aip::content::Snapshot> frames;
    bool submitted = false;
};
static std::vector<GalleryStatusSnapshots> CurrentGalleryStatusWindows()
{
    std::vector<GalleryStatusSnapshots> result;
    aip::gallery::runtime::Input input; std::wstring error;
    if (!CaptureGalleryConfiguration(input, error) || !g_galleryRuntime.IsProfile(input.profile) || !input.pinnedKnown || !input.directAvailable) return result;
    auto effective = input.configuration; effective.enabled = !effective.groupId.empty();
    for (auto& tile : effective.tiles) tile.enabled = true;
    for (const auto& window : g_galleryRuntime.StatusWindows(aip::gallery::runtime::ConfigKey(effective))) {
        if (window.frames.empty() || input.pinnedIds.find(window.frames.front().nativeId) == input.pinnedIds.end()) continue;
        GalleryStatusSnapshots item; item.submitted = window.submitted;
        for (const auto& source : window.frames) {
            auto frame = GallerySnapshotForFrame(source);
            // Status records cannot accidentally enter the generation queue.
            frame.secondaryFrameKey.clear(); frame.nativeQueueFrameKey.clear(); frame.nativeQueue = false; frame.nativeQueueReset = false;
            item.frames.push_back(std::move(frame));
        }
        result.push_back(std::move(item));
    }
    return result;
}
static bool ValidateGalleryFrame(const aip::content::Snapshot& frame)
{
    if (!frame.galleryFrame || !frame.nativeQueue) return false;
    aip::gallery::runtime::Frame expected;
    expected.receipt = GalleryReceipt(frame); expected.nativeId = frame.secondaryTileId; expected.path = frame.backgroundPath;
    expected.name = frame.name; expected.activationArguments = frame.activationArguments; expected.layout = frame.nativeQueueLayout;
    expected.tag = frame.nativeQueueTag; expected.branding = frame.secondaryBranding; expected.reset = frame.nativeQueueReset; expected.contain = frame.galleryContain;
    if (!g_galleryRuntime.ValidateFrame(expected)) return false;
    std::wstring error; const auto view = CaptureGalleryConfigurationView(error, true);
    if (!view || !g_galleryRuntime.IsProfile(view->profile) || !ContentDirectLiveDeliveryAvailable() || !SecondaryTileInventoryReady() || !SecondaryTileExists(frame.secondaryTileId) ||
        view->configuration.groupId != frame.galleryGroupId || (view->configuration.fit == aip::gallery::Fit::Fit) != frame.galleryContain ||
        !frame.gallerySlotCount || frame.gallerySlotCount > view->configuration.queueSize || frame.gallerySlot >= frame.gallerySlotCount) return false;
    const auto tile = std::find_if(view->configuration.tiles.begin(), view->configuration.tiles.end(), [&](const auto& value) {
        return value.id == frame.galleryTileId && value.name == frame.name && value.appearance.branding == frame.secondaryBranding &&
            aip::gallery::NativeTileId(view->configuration.groupId, value.id) == frame.secondaryTileId;
    });
    if (tile == view->configuration.tiles.end() || frame.activationArguments != L"--ini " + aip::QuoteCommandLineArg(view->profile) + L" --open-folder " + aip::QuoteCommandLineArg(aip::gallery::EffectiveFolder(view->configuration, *tile))) return false;
    return !GalleryTransactionPending(view->profile) && g_galleryRuntime.ValidateFrame(expected);
}
static void AcknowledgeGalleryFrame(const aip::content::Snapshot& frame) { if (frame.galleryFrame) g_galleryRuntime.Acknowledge(GalleryReceipt(frame)); }
static void FailGalleryFrame(const aip::content::Snapshot& frame) { if (frame.galleryFrame) g_galleryRuntime.Fail(GalleryReceipt(frame)); }
static void StopGallerySource() { g_galleryRuntime.Stop(); }
static void RequestGalleryRefresh(bool reloadLedger = false) { g_galleryRuntime.RequestRefresh(reloadLedger); }
static void SuspendGalleryPublication() { g_galleryRuntime.Suspend(); }
static void ResumeGalleryPublication(bool reloadLedger = true) { g_galleryRuntime.Resume(reloadLedger); }
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
static bool ResetGalleryLedgerForProfile(const std::wstring& profile, const aip::gallery::Configuration& cfg,
    const aip::gallery::Ledger& expectedFresh, const std::wstring& newEpoch, bool allQueuesClearedConfirmed, std::wstring& error)
{
    if (!aip::gallery::ValidateConfiguration(cfg, error) || cfg.groupId != expectedFresh.groupId) {
        if (error.empty()) error = L"Gallery redistribution must keep the existing group identity."; return false;
    }
    const auto path = aip::gallery::runtime::LedgerPath(aip::MakeAbsolutePath(profile));
    aip::IniWriteMutexGuard guard(path, CurrentIniWriteLockWaitMs());
    if (!guard.IsLocked()) { error = L"Gallery ownership ledger is temporarily locked."; return false; }
    aip::gallery::Ledger fresh;
    if (ReadGalleryLedgerNative(path, fresh, error) != aip::gallery::runtime::ReadResult::Ok) return false;
    std::string bytes;
    if (aip::gallery::runtime::PrepareLedgerReset(expectedFresh, fresh, newEpoch, allQueuesClearedConfirmed, bytes, error) != aip::gallery::runtime::SaveResult::Saved) return false;
    if (!WriteWholeFileBytes(path, std::vector<BYTE>(bytes.begin(), bytes.end()))) { error = L"The cleared gallery could not save its redistributed ownership epoch. Existing ledger bytes were kept."; return false; }
    error.clear(); return true;
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
        changes.push_back({section, L"Folder", tile.folder}); changes.push_back({section, L"GalleryName", tile.galleryName});
        changes.push_back({section, L"Background", tile.appearance.background}); changes.push_back({section, L"Branding", tile.appearance.branding});
        changes.push_back({section, L"BackgroundColor", tile.appearance.backgroundColor});
    }
    output = std::move(changes); error.clear(); return true;
}
#endif
