#pragma once

#include "velocitycopy/shell_request.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
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
    [[nodiscard]] std::optional<ShellRequest> receive_and_ack(const std::function<bool(const ShellRequest&)>& admit) noexcept;
    void stop() noexcept;
    [[nodiscard]] bool stopping() const noexcept;

private:
    bool create_pipe() noexcept;
    bool recreate_pipe_or_stop() noexcept;
    void close_pipe() noexcept;

    void* pipe_{};
    void* stop_event_{};
    std::atomic_bool stopping_{false};
};

struct IpcSendResult {
    bool success{};
    std::int32_t native_code{};
    // True when a write may have reached the receiver. Never retry such a request.
    bool delivery_ambiguous{};

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

[[nodiscard]] IpcSendResult send_shell_request(
    const ShellRequest& request,
    std::uint32_t timeout_ms = 1000) noexcept;

} // namespace velocitycopy
