#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace velocitycopy::detail {

inline std::int32_t win32_hr(const DWORD error) noexcept {
    return static_cast<std::int32_t>(HRESULT_FROM_WIN32(error));
}

inline bool is_name_surrogate_reparse(const FILE_ATTRIBUTE_TAG_INFO& info) noexcept {
    return (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
        IsReparseTagNameSurrogate(info.ReparseTag) != 0;
}

inline std::int32_t inspect_path_without_following(
    const std::filesystem::path& path,
    const bool require_directory,
    HANDLE* retained_handle = nullptr) noexcept {
    const DWORD flags = FILE_FLAG_OPEN_REPARSE_POINT |
        (require_directory ? FILE_FLAG_BACKUP_SEMANTICS : 0);
    const HANDLE handle = CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        flags,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return win32_hr(GetLastError());
    }

    FILE_ATTRIBUTE_TAG_INFO info{};
    if (GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info)) == 0) {
        const auto result = win32_hr(GetLastError());
        CloseHandle(handle);
        return result;
    }
    if (require_directory && (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        CloseHandle(handle);
        return win32_hr(ERROR_DIRECTORY);
    }
    if (is_name_surrogate_reparse(info)) {
        CloseHandle(handle);
        return win32_hr(ERROR_CANT_ACCESS_FILE);
    }

    if (retained_handle != nullptr) {
        *retained_handle = handle;
    } else {
        CloseHandle(handle);
    }
    return S_OK;
}

inline std::int32_t source_is_safe_to_follow(
    const std::filesystem::path& source) noexcept {
    return inspect_path_without_following(source, false);
}

inline std::int32_t destination_leaf_is_safe(
    const std::filesystem::path& destination) noexcept {
    const DWORD attributes = GetFileAttributesW(destination.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return S_OK;
        }
        return win32_hr(error);
    }
    return inspect_path_without_following(
        destination,
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

class DestinationDirectoryGuard final {
public:
    DestinationDirectoryGuard() = default;
    DestinationDirectoryGuard(const DestinationDirectoryGuard&) = delete;
    DestinationDirectoryGuard& operator=(const DestinationDirectoryGuard&) = delete;

    ~DestinationDirectoryGuard() noexcept {
        for (const HANDLE handle : handles_) {
            CloseHandle(handle);
        }
    }

    [[nodiscard]] std::int32_t prepare(
        const std::filesystem::path& directory) noexcept {
        if (directory.empty()) return S_OK;

        std::error_code ec;
        const auto absolute = std::filesystem::absolute(directory, ec);
        if (ec) return win32_hr(static_cast<DWORD>(ec.value()));

        auto current = absolute.root_path();
        if (current.empty()) return win32_hr(ERROR_INVALID_NAME);

        auto result = retain_directory(current);
        if (result != S_OK) return result;

        for (const auto& component : absolute.relative_path()) {
            current /= component;
            result = retain_directory(current);
            if (result == win32_hr(ERROR_FILE_NOT_FOUND) ||
                result == win32_hr(ERROR_PATH_NOT_FOUND)) {
                if (!CreateDirectoryW(current.c_str(), nullptr)) {
                    const auto create_error = GetLastError();
                    if (create_error != ERROR_ALREADY_EXISTS) {
                        return win32_hr(create_error);
                    }
                }
                result = retain_directory(current);
            }
            if (result != S_OK) return result;
        }
        return S_OK;
    }

private:
    [[nodiscard]] std::int32_t retain_directory(
        const std::filesystem::path& path) noexcept {
        HANDLE handle = INVALID_HANDLE_VALUE;
        const auto result = inspect_path_without_following(path, true, &handle);
        if (result == S_OK) handles_.push_back(handle);
        return result;
    }

    std::vector<HANDLE> handles_;
};

} // namespace velocitycopy::detail
