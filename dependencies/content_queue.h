#pragma once

// Pure delivery-delta bookkeeping. The host serializes access, computes visual
// frame keys, owns asset leases, and calls Ack only after a successful tile API.
#include "content_engine.h"
#include "tile_notification_payload.h"
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace aip { namespace content {
class QueueDelta {
    std::map<std::wstring, std::wstring> committed_;
    std::wstring layout_;
    bool initialized_ = false;

    static bool ValidFrame(const Snapshot& snapshot) {
        std::wstring error;
        return snapshot.active && !snapshot.nativeQueueFrameKey.empty() &&
            tile_notification::ValidOptions({true, snapshot.nativeQueueTag, false}, error);
    }
public:
    std::vector<Snapshot> Plan(const std::wstring& layout, const std::vector<Snapshot>& snapshots,
        bool force = false) const {
        if (layout.empty() || snapshots.size() > 5)
            throw std::invalid_argument("A native queue plan requires a layout and at most five ready frames.");
        std::set<std::wstring> tags;
        for (const auto& snapshot : snapshots)
            if (!ValidFrame(snapshot) || !tags.insert(snapshot.nativeQueueTag).second)
                throw std::invalid_argument("A native queue plan requires active frames with unique valid tags and nonempty visual keys.");
        const bool changedLayout = !initialized_ || layout != layout_;
        std::vector<Snapshot> result;
        for (const auto& snapshot : snapshots) {
            const auto found = committed_.find(snapshot.nativeQueueTag);
            if (!force && !changedLayout && found != committed_.end() && found->second == snapshot.nativeQueueFrameKey)
                continue;
            auto copy = snapshot;
            copy.nativeQueue = true;
            copy.nativeQueueLayout = layout;
            copy.nativeQueueReset = changedLayout && result.empty();
            result.push_back(std::move(copy));
        }
        return result;
    }

    bool Ack(const Snapshot& snapshot) {
        if (!snapshot.nativeQueue || snapshot.nativeQueueLayout.empty() || !ValidFrame(snapshot)) return false;
        if (!initialized_ || snapshot.nativeQueueReset || snapshot.nativeQueueLayout != layout_) {
            committed_.clear();
            layout_ = snapshot.nativeQueueLayout;
            initialized_ = true;
        }
        committed_[snapshot.nativeQueueTag] = snapshot.nativeQueueFrameKey;
        return true;
    }

    void LeaveQueue() {
        committed_.clear(); layout_.clear(); initialized_ = false;
    }
    std::size_t CommittedCount() const { return committed_.size(); }
    const std::wstring& Layout() const { return layout_; }
    std::wstring CommittedFrame(const std::wstring& tag) const {
        const auto found = committed_.find(tag);
        return found == committed_.end() ? std::wstring{} : found->second;
    }
};
} }
