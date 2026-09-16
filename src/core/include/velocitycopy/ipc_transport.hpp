#pragma once

#include "velocitycopy/shell_request.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

namespace velocitycopy {

[[nodiscard]] std::wstring shell_pipe_name() noexcept;

class SingleInstance final {
public:
    SingleInstance() noexcept;
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool primary() const noexcept;

private:
    void* mutex_{};
    bool primary_{};
};

class ShellIpcServer final {
public:
    ShellIpcServer() noexcept;
    ~ShellIpcServer();

    ShellIpcServer(const ShellIpcServer&) = delete;
    ShellIpcServer& operator=(const ShellIpcServer&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::optional<ShellRequest> receive() noexcept;
    void stop() noexcept;
    [[nodiscard]] bool stopping() const noexcept;

private:
    bool create_pipe() noexcept;
    bool recreate_pipe_or_stop() noexcept;
    void close_pipe() noexcept;
    void wake_receiver() noexcept;

    void* pipe_{};
    std::atomic_bool stopping_{false};
};

[[nodiscard]] bool send_shell_request(
    const ShellRequest& request,
    std::uint32_t timeout_ms = 1000) noexcept;

} // namespace velocitycopy
