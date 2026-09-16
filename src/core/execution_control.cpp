#include "velocitycopy/execution_control.hpp"

namespace velocitycopy {

ExecutionDirective ExecutionControl::directive() const noexcept {
    std::lock_guard lock(mutex_);
    return directive_;
}

void ExecutionControl::request_pause() noexcept {
    std::lock_guard lock(mutex_);
    if (directive_ == ExecutionDirective::Run) {
        directive_ = ExecutionDirective::Pause;
    }
}

void ExecutionControl::resume() noexcept {
    {
        std::lock_guard lock(mutex_);
        if (directive_ != ExecutionDirective::Pause) {
            return;
        }
        directive_ = ExecutionDirective::Run;
    }
    condition_.notify_all();
}

void ExecutionControl::request_stop() noexcept {
    {
        std::lock_guard lock(mutex_);
        if (directive_ == ExecutionDirective::Cancel) {
            return;
        }
        directive_ = ExecutionDirective::Stop;
    }
    condition_.notify_all();
}

void ExecutionControl::request_cancel() noexcept {
    {
        std::lock_guard lock(mutex_);
        directive_ = ExecutionDirective::Cancel;
    }
    condition_.notify_all();
}

void ExecutionControl::reset() noexcept {
    {
        std::lock_guard lock(mutex_);
        directive_ = ExecutionDirective::Run;
    }
    condition_.notify_all();
}

ExecutionDirective ExecutionControl::wait_while_paused() noexcept {
    std::unique_lock lock(mutex_);
    condition_.wait(lock, [this] { return directive_ != ExecutionDirective::Pause; });
    return directive_;
}

} // namespace velocitycopy
