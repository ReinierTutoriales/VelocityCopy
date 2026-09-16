#include "velocitycopy/ipc_transport.hpp"

#include "velocitycopy/ipc_protocol.hpp"

#include <windows.h>

#include <array>
#include <vector>

namespace velocitycopy {
namespace {

constexpr wchar_t kMutexName[] = L"Local\\VelocityCopy.Instance.v1";

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
    DWORD session_id = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id)) {
        session_id = 0;
    }
    return L"\\\\.\\pipe\\VelocityCopy.Session." + std::to_wstring(session_id) + L".v1";
}

SingleInstance::SingleInstance() noexcept {
    HANDLE handle = CreateMutexW(nullptr, FALSE, kMutexName);
    mutex_ = handle;
    primary_ = handle != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
}

SingleInstance::~SingleInstance() {
    if (mutex_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(mutex_));
    }
}

bool SingleInstance::valid() const noexcept {
    return mutex_ != nullptr;
}

bool SingleInstance::primary() const noexcept {
    return primary_;
}

ShellIpcServer::ShellIpcServer() noexcept {
    create_pipe();
}

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
    if (stopping()) {
        return false;
    }

    const auto name = shell_pipe_name();
    HANDLE pipe = CreateNamedPipeW(
        name.c_str(),
        PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,
        static_cast<DWORD>(kMaxShellMessageBytes + sizeof(std::uint32_t)),
        static_cast<DWORD>(kMaxShellMessageBytes + sizeof(std::uint32_t)),
        0,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        pipe_ = nullptr;
        return false;
    }
    pipe_ = pipe;
    return true;
}

void ShellIpcServer::close_pipe() noexcept {
    if (valid()) {
        CloseHandle(static_cast<HANDLE>(pipe_));
    }
    pipe_ = nullptr;
}

void ShellIpcServer::wake_receiver() noexcept {
    const auto name = shell_pipe_name();
    if (!WaitNamedPipeW(name.c_str(), 250)) {
        return;
    }

    HANDLE pipe = CreateFileW(
        name.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return;
    }

    const std::uint32_t wake_size = 0;
    (void)write_all(pipe, &wake_size, sizeof(wake_size));
    CloseHandle(pipe);
}

void ShellIpcServer::stop() noexcept {
    bool expected = false;
    if (!stopping_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }
    wake_receiver();
}

std::optional<ShellRequest> ShellIpcServer::receive() noexcept {
    if (!valid() || stopping()) {
        return std::nullopt;
    }

    HANDLE pipe = static_cast<HANDLE>(pipe_);
    const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
    if (!connected) {
        if (!stopping()) {
            create_pipe();
        }
        return std::nullopt;
    }

    std::uint32_t size = 0;
    std::optional<ShellRequest> result;
    if (read_all(pipe, &size, sizeof(size)) && size > 0 && size <= kMaxShellMessageBytes) {
        std::vector<std::uint8_t> payload(size);
        if (read_all(pipe, payload.data(), size)) {
            result = deserialize_shell_request(payload);
        }
    }

    if (size != 0) {
        FlushFileBuffers(pipe);
    }
    DisconnectNamedPipe(pipe);

    if (!stopping()) {
        create_pipe();
    }
    return result;
}

bool send_shell_request(const ShellRequest& request, const std::uint32_t timeout_ms) noexcept {
    const auto payload = serialize_shell_request(request);
    if (!payload) {
        return false;
    }

    const auto name = shell_pipe_name();
    if (!WaitNamedPipeW(name.c_str(), timeout_ms)) {
        return false;
    }

    HANDLE pipe = CreateFileW(
        name.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    const auto size = static_cast<std::uint32_t>(payload->size());
    const bool ok = write_all(pipe, &size, sizeof(size)) &&
        write_all(pipe, payload->data(), size);
    FlushFileBuffers(pipe);
    CloseHandle(pipe);
    return ok;
}

} // namespace velocitycopy
