#include "velocitycopy/process_activation.hpp"

#include "velocitycopy/ipc_protocol.hpp"

#include <windows.h>

#include <cstring>
#include <limits>
#include <string>

namespace velocitycopy {
namespace {

class AttributeList final {
public:
    AttributeList() noexcept {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (bytes == 0) {
            return;
        }

        memory_ = HeapAlloc(GetProcessHeap(), 0, bytes);
        if (memory_ == nullptr) {
            return;
        }

        list_ = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(memory_);
        if (!InitializeProcThreadAttributeList(list_, 1, 0, &bytes)) {
            HeapFree(GetProcessHeap(), 0, memory_);
            memory_ = nullptr;
            list_ = nullptr;
        }
    }

    ~AttributeList() {
        if (list_ != nullptr) {
            DeleteProcThreadAttributeList(list_);
        }
        if (memory_ != nullptr) {
            HeapFree(GetProcessHeap(), 0, memory_);
        }
    }

    AttributeList(const AttributeList&) = delete;
    AttributeList& operator=(const AttributeList&) = delete;

    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept {
        return list_;
    }

private:
    void* memory_{};
    LPPROC_THREAD_ATTRIBUTE_LIST list_{};
};

} // namespace

bool launch_velocitycopy_with_request(
    const std::filesystem::path& executable,
    const ShellRequest& request) noexcept {
    try {
        const auto payload = serialize_shell_request(request);
        if (!payload || payload->empty() || payload->size() > kMaxShellMessageBytes ||
            payload->size() > std::numeric_limits<DWORD>::max()) {
            return false;
        }

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;

        HANDLE mapping = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            &security,
            PAGE_READWRITE,
            0,
            static_cast<DWORD>(payload->size()),
            nullptr);
        if (mapping == nullptr) {
            return false;
        }

        void* view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, payload->size());
        if (view == nullptr) {
            CloseHandle(mapping);
            return false;
        }
        std::memcpy(view, payload->data(), payload->size());
        UnmapViewOfFile(view);

        AttributeList attributes;
        if (attributes.get() == nullptr) {
            CloseHandle(mapping);
            return false;
        }

        HANDLE allowed_handles[] = {mapping};
        if (!UpdateProcThreadAttribute(
                attributes.get(),
                0,
                PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                allowed_handles,
                sizeof(allowed_handles),
                nullptr,
                nullptr)) {
            CloseHandle(mapping);
            return false;
        }

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.lpAttributeList = attributes.get();

        PROCESS_INFORMATION process{};
        const auto handle_number = reinterpret_cast<std::uintptr_t>(mapping);
        std::wstring command_line = L"\"" + executable.native() +
            L"\" --shell-runtime --activation-handle " + std::to_wstring(handle_number) +
            L" --activation-size " + std::to_wstring(payload->size());
        const auto parent = executable.parent_path();

        const BOOL created = CreateProcessW(
            executable.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
            nullptr,
            parent.empty() ? nullptr : parent.c_str(),
            &startup.StartupInfo,
            &process);

        CloseHandle(mapping);

        if (!created) {
            return false;
        }

        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<ShellRequest> read_inherited_shell_request(
    const std::uintptr_t handle_value,
    const std::size_t payload_size) noexcept {
    if (handle_value == 0 || payload_size == 0 || payload_size > kMaxShellMessageBytes) {
        return std::nullopt;
    }

    HANDLE mapping = reinterpret_cast<HANDLE>(handle_value);
    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, payload_size);
    if (view == nullptr) {
        CloseHandle(mapping);
        return std::nullopt;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(view);
    const auto request = deserialize_shell_request(std::span<const std::uint8_t>(bytes, payload_size));
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return request;
}

} // namespace velocitycopy
