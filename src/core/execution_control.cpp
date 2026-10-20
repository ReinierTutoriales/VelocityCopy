#include "velocitycopy/execution_control.hpp"

namespace velocitycopy {

ExecutionDirective ExecutionControl::directive() const noexcept {
    return directive_.load();
}

void ExecutionControl::request_pause() noexcept {
    {
        std::lock_guard lock(mutex_);
        if (directive_ == ExecutionDirective::Run) {
            directive_ = ExecutionDirective::Pause;
        }
    }
    condition_.notify_all();
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
        skip_file_ids_.clear();
    }
    condition_.notify_all();
}

void ExecutionControl::reset() noexcept {
    {
        std::lock_guard lock(mutex_);
        directive_ = ExecutionDirective::Run;
        skip_file_ids_.clear();
    }
    condition_.notify_all();
}

void ExecutionControl::request_skip(const std::uint64_t file_id) noexcept {
    if (file_id == 0) {
        return;
    }
    try {
        std::lock_guard lock(mutex_);
        if (directive_ == ExecutionDirective::Run || directive_ == ExecutionDirective::Pause) {
            skip_file_ids_.insert(file_id);
        }
    } catch (...) {
        // A skip request is advisory user input. Allocation failure must never
        // destabilize the copy session or escalate into Cancel.
    }
}

bool ExecutionControl::consume_skip(const std::uint64_t file_id) noexcept {
    std::lock_guard lock(mutex_);
    const auto it = skip_file_ids_.find(file_id);
    if (it == skip_file_ids_.end()) {
        return false;
    }
    skip_file_ids_.erase(it);
    return true;
}

ExecutionDirective ExecutionControl::wait_while_paused() noexcept {
    std::unique_lock lock(mutex_);
    condition_.wait(lock, [this] { return directive_ != ExecutionDirective::Pause; });
    return directive_;
}

ExecutionDirective ExecutionControl::wait_for_retry(const std::chrono::milliseconds delay) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + delay;
    std::unique_lock lock(mutex_);
    for (;;) {
        condition_.wait(lock, [this] { return directive_ != ExecutionDirective::Pause; });
        const auto current = directive_.load();
        if (current != ExecutionDirective::Run || std::chrono::steady_clock::now() >= deadline) {
            return current;
        }
        condition_.wait_until(lock, deadline, [this] { return directive_ != ExecutionDirective::Run; });
    }
}

} // namespace velocitycopy
