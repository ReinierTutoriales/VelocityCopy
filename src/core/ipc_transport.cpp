#include "velocitycopy/ipc_transport.hpp"

#include "velocitycopy/ipc_protocol.hpp"

#include <windows.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace velocitycopy {
namespace {

constexpr wchar_t kMutexName[] = L"Local\\VelocityCopy.Instance.v1";

class UniqueHandle final {
public:
    explicit UniqueHandle(HANDLE handle = nullptr) noexcept : handle_(handle) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    void reset(HANDLE replacement = nullptr) noexcept {
        if (valid()) CloseHandle(handle_);
        handle_ = replacement;
    }

private:
    HANDLE handle_{};
};

bool write_all(HANDLE handle, const void* data, std::uint32_t bytes) noexcept {
    const auto* cursor = static_cast<const std::uint8_t*>(data);
    std::uint32_t written_total = 0;
    while (written_total < bytes) {
        DWORD written = 0;
        if (!WriteFile(handle, cursor + written_total, bytes - written_total, &written, nullptr) || written == 0) {
            return false;
        }
        written_total += written;
    }
    return true;
}

bool read_all(HANDLE handle, void* data, std::uint32_t bytes) noexcept {
    auto* cursor = static_cast<std::uint8_t*>(data);
    std::uint32_t read_total = 0;
    while (read_total < bytes) {
        DWORD read = 0;
        if (!ReadFile(handle, cursor + read_total, bytes - read_total, &read, nullptr) || read == 0) {
            return false;
        }
        read_total += read;
    }
    return true;
}

} // namespace

std::wstring shell_pipe_name() noexcept {
    try {
        DWORD session_id = 0;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id)) session_id = 0;
        return L"\\\\.\\pipe\\VelocityCopy.Session." + std::to_wstring(session_id) + L".v1";
    } catch (...) {
        return {};
    }
}

SingleInstance::SingleInstance() noexcept {
    HANDLE handle = CreateMutexW(nullptr, FALSE, kMutexName);
    mutex_ = handle;
    primary_ = handle != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
}

SingleInstance::~SingleInstance() {
    if (mutex_ != nullptr) CloseHandle(static_cast<HANDLE>(mutex_));
}

bool SingleInstance::valid() const noexcept { return mutex_ != nullptr; }
bool SingleInstance::primary() const noexcept { return primary_; }

ShellIpcServer::ShellIpcServer() noexcept { create_pipe(); }

ShellIpcServer::~ShellIpcServer() {
    stop();
    close_pipe();
}

bool ShellIpcServer::valid() const noexcept {
    return pipe_ != nullptr && pipe_ != INVALID_HANDLE_VALUE;
}

bool ShellIpcServer::stopping() const noexcept {
    return stopping_.load(std::memory_order_acquire);
}

bool ShellIpcServer::create_pipe() noexcept {
    close_pipe();
    if (stopping()) return false;

    const auto name = shell_pipe_name();
    if (name.empty()) return false;

    HANDLE pipe = CreateNamedPipeW(
        name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
        static_cast<DWORD>(kMaxShellMessageBytes + sizeof(std::uint32_t)),
        static_cast<DWORD>(kMaxShellMessageBytes + sizeof(std::uint32_t)), 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        pipe_ = nullptr;
        return false;
    }
    pipe_ = pipe;
    return true;
}

bool ShellIpcServer::recreate_pipe_or_stop() noexcept {
    if (stopping()) return false;
    if (create_pipe()) return true;
    stopping_.store(true, std::memory_order_release);
    return false;
}

void ShellIpcServer::close_pipe() noexcept {
    if (valid()) CloseHandle(static_cast<HANDLE>(pipe_));
    pipe_ = nullptr;
}

void ShellIpcServer::wake_receiver() noexcept {
    const auto name = shell_pipe_name();
    if (name.empty() || !WaitNamedPipeW(name.c_str(), 250)) return;

    UniqueHandle pipe{CreateFileW(
        name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!pipe.valid()) return;

    const std::uint32_t wake_size = 0;
    (void)write_all(pipe.get(), &wake_size, sizeof(wake_size));
}

void ShellIpcServer::stop() noexcept {
    bool expected = false;
    if (!stopping_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
    wake_receiver();
}

std::optional<ShellRequest> ShellIpcServer::receive() noexcept {
    if (!valid() || stopping()) return std::nullopt;

    HANDLE pipe = static_cast<HANDLE>(pipe_);
    const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
    if (!connected) {
        (void)recreate_pipe_or_stop();
        return std::nullopt;
    }

    std::uint32_t size = 0;
    std::optional<ShellRequest> result;
    if (read_all(pipe, &size, sizeof(size)) && size > 0 && size <= kMaxShellMessageBytes) {
        try {
            std::vector<std::uint8_t> payload(size);
            if (read_all(pipe, payload.data(), size)) result = deserialize_shell_request(payload);
        } catch (...) {
            result.reset();
        }
    }

    if (size != 0) (void)FlushFileBuffers(pipe);
    (void)DisconnectNamedPipe(pipe);

    // Invalid client data is recoverable. Failure to recreate the transport is
    // not: mark the server stopped so the receiver loop cannot hot-spin.
    (void)recreate_pipe_or_stop();
    return result;
}

bool send_shell_request(const ShellRequest& request, const std::uint32_t timeout_ms) noexcept {
    try {
        const auto payload = serialize_shell_request(request);
        if (!payload) return false;

        const auto name = shell_pipe_name();
        if (name.empty() || !WaitNamedPipeW(name.c_str(), timeout_ms)) return false;

        UniqueHandle pipe{CreateFileW(
            name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (!pipe.valid()) return false;

        const auto size = static_cast<std::uint32_t>(payload->size());
        const bool ok = write_all(pipe.get(), &size, sizeof(size)) &&
            write_all(pipe.get(), payload->data(), size);
        (void)FlushFileBuffers(pipe.get());
        return ok;
    } catch (...) {
        return false;
    }
}

} // namespace velocitycopy
