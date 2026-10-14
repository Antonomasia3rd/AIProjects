#pragma once
// Inert construction; discovery and all endpoint operations stay on the
// Controller owner worker. Fake discovery/backends exercise this on any OS.
#include "caps_blink_engine.h"
#include <set>
#include <vector>

namespace aip { namespace caps {
using KeyboardDiscovery = std::function<bool(std::vector<std::wstring>&, const Cancel&, std::wstring&)>;
using KeyboardBackendFactory = std::function<std::unique_ptr<Backend>(const std::wstring&, bool)>;
class GroupBackend final : public Backend {
    struct Slot {
        std::unique_ptr<Backend> backend;
        bool available = false, currentKnown = false, originalKnown = false;
        Indicators current, original;
    };
    KeyboardDiscovery discover_;
    KeyboardBackendFactory factory_;
    std::vector<Slot> slots_;
    std::size_t discovered_ = 0;
    std::wstring lastError_;
    static void AddError(std::wstring& target, const std::wstring& error) {
        if (!target.empty()) target += L"; ";
        target += error.empty() ? L"Keyboard operation failed" : error;
    }
public:
    GroupBackend(KeyboardDiscovery discover, KeyboardBackendFactory factory)
        : discover_(std::move(discover)), factory_(std::move(factory)) {}
    bool Open(const std::wstring& target, const Cancel& canceled, std::wstring& error) override {
        if (!Close(error)) return false;
        if (canceled && canceled()) { error = L"Keyboard discovery/open canceled"; return false; }
        Config cfg; cfg.target = target;
        if (!Normalize(cfg, error)) return false;
        std::vector<std::wstring> targets;
        const bool automatic = cfg.target == L"Auto";
        if (automatic) {
            if (!discover_ || !discover_(targets, canceled, error)) return false;
        } else targets.push_back(cfg.target);
        std::set<std::wstring> seen;
        lastError_.clear(); discovered_ = 0;
        for (const auto& path : targets) {
            if (canceled && canceled()) { error = L"Keyboard discovery/open canceled"; return false; }
            auto identity = path;
            for (auto& ch : identity) if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
            if (path.empty() || !seen.insert(identity).second) continue;
            ++discovered_;
            Slot slot;
            slot.backend = factory_ ? factory_(path, automatic) : nullptr;
            std::wstring issue;
            slot.available = slot.backend && slot.backend->Open(path, canceled, issue);
            if (!slot.available) lastError_ = issue.empty() ? L"Keyboard backend unavailable" : issue;
            // Even failed/partial acquisition is kept for ordered Close.
            slots_.push_back(std::move(slot));
        }
        if (std::none_of(slots_.begin(), slots_.end(), [](const Slot& slot) { return slot.available; })) {
            error = lastError_.empty() ? L"No available keyboard class interfaces were found" : lastError_;
            return false;
        }
        error.clear(); return true;
    }
    bool ReadIndicators(Indicators& result, const Cancel& canceled, std::wstring& error) override {
        bool any = false;
        for (auto& slot : slots_) {
            if (!slot.available) continue;
            std::wstring issue;
            Indicators current;
            slot.currentKnown = slot.backend->ReadIndicators(current, canceled, issue);
            if (!slot.currentKnown) { slot.available = false; lastError_ = issue; continue; }
            slot.current = current;
            if (!slot.originalKnown) { slot.original = current; slot.originalKnown = true; }
            if (!any) result = current;
            any = true;
        }
        if (!any) { error = lastError_.empty() ? L"No keyboard indicator query succeeded" : lastError_; return false; }
        error.clear(); return true;
    }
    bool ReadLogicalCaps(bool& enabled, const Cancel& canceled, std::wstring& error) override {
        for (auto& slot : slots_) if (slot.backend && (slot.available || slot.originalKnown))
            return slot.backend->ReadLogicalCaps(enabled, canceled, error);
        error = L"No keyboard is available to sample logical Caps Lock"; return false;
    }
    bool WriteIndicators(Indicators value, const Cancel& canceled, std::wstring& error) override {
        bool any = false;
        for (auto& slot : slots_) {
            if (!slot.available || !slot.currentKnown) continue;
            auto desired = slot.current;
            desired.flags = static_cast<std::uint16_t>((desired.flags & ~CapsMask) | (value.flags & CapsMask));
            std::wstring issue;
            if (desired == slot.current || slot.backend->WriteIndicators(desired, canceled, issue)) {
                slot.current = desired; any = true;
            } else { slot.available = false; lastError_ = issue; }
        }
        if (!any) { error = lastError_.empty() ? L"No keyboard indicator update succeeded" : lastError_; return false; }
        error.clear(); return true;
    }
    bool NeedsWrite(Indicators, Indicators desired) const override {
        for (const auto& slot : slots_) if (slot.available && slot.currentKnown &&
            (slot.current.flags & CapsMask) != (desired.flags & CapsMask)) return true;
        return false;
    }
    bool RestoreIndicators(Indicators, Indicators, bool, bool logicalKnown, bool logical,
        const Cancel& canceled, Indicators& restored, bool& wrote, std::wstring& error) override {
        bool ok = true, any = false; wrote = false; error.clear();
        for (auto& slot : slots_) {
            if (!slot.backend || !slot.originalKnown) continue;
            Indicators current = slot.original, result;
            std::wstring issue;
            const bool fresh = slot.backend->ReadIndicators(current, canceled, issue);
            if (slot.backend->HasPendingOperation()) {
                ok = false; AddError(error, L"Keyboard restoration awaits pending query completion"); continue;
            }
            if (!fresh) current = slot.original;
            bool changed = false;
            const bool success = slot.backend->RestoreIndicators(current, slot.original, fresh, logicalKnown,
                logical, canceled, result, changed, issue);
            if (success) { if (!any) restored = result; any = true; wrote = wrote || changed; }
            else { ok = false; AddError(error, issue); }
        }
        return ok && any;
    }
    bool HasPendingOperation() const noexcept override {
        for (const auto& slot : slots_) if (slot.backend && slot.backend->HasPendingOperation()) return true;
        return false;
    }
    std::wstring Warning() const override {
        const auto available = static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(), [](const Slot& slot) { return slot.available; }));
        if (available == discovered_) return {};
        return std::to_wstring(available) + L" of " + std::to_wstring(discovered_) +
            L" keyboard endpoints available" + (lastError_.empty() ? std::wstring{} : L": " + lastError_);
    }
    bool Close(std::wstring& error) override {
        if (HasPendingOperation()) { error = L"Keyboard cancellation is still pending; all endpoint ownership is retained"; return false; }
        bool ok = true; error.clear();
        for (auto& slot : slots_) if (slot.backend) {
            std::wstring issue;
            if (!slot.backend->Close(issue)) { ok = false; AddError(error, issue); }
        }
        if (ok) { slots_.clear(); discovered_ = 0; lastError_.clear(); }
        return ok;
    }
};
} }
