#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <unordered_set>

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

    // Skip is intentionally per-file rather than a session directive. With
    // multiple copy workers active, a global Skip would be ambiguous and could
    // cancel a different file from the one currently presented by the UI.
    void request_skip(std::uint64_t file_id) noexcept;
    [[nodiscard]] bool consume_skip(std::uint64_t file_id) noexcept;

    [[nodiscard]] ExecutionDirective wait_while_paused() noexcept;

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    ExecutionDirective directive_{ExecutionDirective::Run};
    std::unordered_set<std::uint64_t> skip_file_ids_;
};

} // namespace velocitycopy
