#pragma once
#include "service.h"
#include <atomic>

namespace aip { namespace discord {
// The operation owner serializes Begin()/scope destruction. Background readers
// see immutable callback objects and own their relay state independently of the
// transport. An operation deadline is never retained after that operation ends.
class CancellationRelay {
    struct State { std::shared_ptr<const Cancel> callback; };
    std::shared_ptr<State> state_ = std::make_shared<State>();
public:
    class Operation {
        std::shared_ptr<State> state_;
        friend class CancellationRelay;
        Operation(std::shared_ptr<State> state, Cancel callback) : state_(std::move(state)) {
            std::atomic_store_explicit(&state_->callback, std::make_shared<const Cancel>(std::move(callback)), std::memory_order_release);
        }
    public:
        Operation(const Operation&) = delete;
        Operation& operator=(const Operation&) = delete;
        ~Operation() { std::atomic_store_explicit(&state_->callback, std::shared_ptr<const Cancel>{}, std::memory_order_release); }
    };
    Operation Begin(Cancel callback) { return Operation(state_, std::move(callback)); }
    Cancel Callback() const {
        const auto state = state_;
        return [state] {
            const auto callback = std::atomic_load_explicit(&state->callback, std::memory_order_acquire);
            return callback && *callback ? (*callback)() : false;
        };
    }
};
} }
