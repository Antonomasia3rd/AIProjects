#pragma once

// A completed failure produces one notice until a healthy result arrives.
// Pending work and normal setup states never become failures on their own.
namespace aip::notifications {
enum class SourceState { Unconfigured, Pending, Healthy, Failed };
enum class SourceNotice { None, Failed, Recovered };
class SourceTransitions {
    bool failed_ = false;
public:
    SourceNotice Observe(SourceState state) {
        if (state == SourceState::Pending) return SourceNotice::None;
        if (state == SourceState::Unconfigured) { failed_ = false; return SourceNotice::None; }
        if (state == SourceState::Failed) {
            if (failed_) return SourceNotice::None;
            failed_ = true; return SourceNotice::Failed;
        }
        if (!failed_) return SourceNotice::None;
        failed_ = false; return SourceNotice::Recovered;
    }
};
}
