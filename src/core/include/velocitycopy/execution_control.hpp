#pragma once

#include <condition_variable>
#include <mutex>

namespace velocitycopy {

enum class ExecutionDirective {
    Run,
    Pause,
    Stop,
    Cancel,
};

class ExecutionControl final {
public:
    [[nodiscard]] ExecutionDirective directive() const noexcept;

    void request_pause() noexcept;
    void resume() noexcept;
    void request_stop() noexcept;
    void request_cancel() noexcept;
    void reset() noexcept;

    [[nodiscard]] ExecutionDirective wait_while_paused() noexcept;

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    ExecutionDirective directive_{ExecutionDirective::Run};
};

} // namespace velocitycopy
