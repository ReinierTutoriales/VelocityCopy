#pragma once

#include <windows.h>

#include <filesystem>
#include <system_error>
#include <vector>

namespace velocitycopy::detail {

inline bool name_surrogate_reparse(const HANDLE handle) noexcept {
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info)) == 0) {
        return true;
    }
    if ((info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        return false;
    }
    return IsReparseTagNameSurrogate(info.ReparseTag) != FALSE;
}

inline bool safe_directory_handle(HANDLE handle, std::error_code& error) noexcept {
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info)) == 0) {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    if ((info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        error = std::error_code(ERROR_DIRECTORY, std::system_category());
        return false;
    }
    if ((info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
        IsReparseTagNameSurrogate(info.ReparseTag) != FALSE) {
        error = std::error_code(ERROR_CANT_ACCESS_FILE, std::system_category());
        return false;
    }
    return true;
}

struct DestinationPathHandle final {
    HANDLE handle{INVALID_HANDLE_VALUE};
    bool created{};
};

struct DestinationPathGuard final {
    std::vector<DestinationPathHandle> handles;
    std::vector<std::filesystem::path> missing;

    DestinationPathGuard() = default;
    DestinationPathGuard(const DestinationPathGuard&) = delete;
    DestinationPathGuard& operator=(const DestinationPathGuard&) = delete;

    ~DestinationPathGuard() noexcept {
        for (auto& entry : handles) {
            if (entry.handle != INVALID_HANDLE_VALUE) {
                CloseHandle(entry.handle);
            }
        }
    }

    void rollback_created() noexcept {
        FILE_DISPOSITION_INFO disposition{};
        disposition.DeleteFile = TRUE;
        for (auto it = handles.rbegin(); it != handles.rend(); ++it) {
            if (!it->created || it->handle == INVALID_HANDLE_VALUE) {
                continue;
            }
            (void)SetFileInformationByHandle(
                it->handle,
                FileDispositionInfo,
                &disposition,
                sizeof(disposition));
            CloseHandle(it->handle);
            it->handle = INVALID_HANDLE_VALUE;
        }
    }

    bool lock_existing_chain(const std::filesystem::path& path, std::error_code& error) {
        missing.clear();
        std::error_code absolute_error;
        auto probe = std::filesystem::absolute(path, absolute_error);
        if (absolute_error) {
            error = absolute_error;
            return false;
        }

        const auto root = probe.root_path();
        while (!probe.empty() && probe != root) {
            const HANDLE handle = CreateFileW(
                probe.c_str(),
                FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                const DWORD native = GetLastError();
                if (native != ERROR_FILE_NOT_FOUND && native != ERROR_PATH_NOT_FOUND) {
                    error = std::error_code(static_cast<int>(native), std::system_category());
                    return false;
                }
                missing.push_back(probe);
                probe = probe.parent_path();
                continue;
            }

            if (!safe_directory_handle(handle, error)) {
                CloseHandle(handle);
                return false;
            }

            try {
                handles.push_back({handle, false});
            } catch (...) {
                CloseHandle(handle);
                throw;
            }
            probe = probe.parent_path();
        }
        return true;
    }

    bool create_missing(std::error_code& error) {
        for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
            const BOOL created_now = CreateDirectoryW(it->c_str(), nullptr);
            if (created_now == 0) {
                const DWORD native = GetLastError();
                if (native != ERROR_ALREADY_EXISTS) {
                    error = std::error_code(static_cast<int>(native), std::system_category());
                    rollback_created();
                    return false;
                }
            }

            const HANDLE handle = CreateFileW(
                it->c_str(),
                FILE_READ_ATTRIBUTES | (created_now != 0 ? DELETE : 0),
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
                rollback_created();
                return false;
            }
            if (!safe_directory_handle(handle, error)) {
                CloseHandle(handle);
                rollback_created();
                return false;
            }

            try {
                handles.push_back({handle, created_now != 0});
            } catch (...) {
                if (created_now != 0) {
                    FILE_DISPOSITION_INFO disposition{TRUE};
                    (void)SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition));
                }
                CloseHandle(handle);
                rollback_created();
                throw;
            }
        }
        missing.clear();
        return true;
    }

    bool prepare_directory(const std::filesystem::path& directory, std::error_code& error) {
        if (directory.empty()) {
            return true;
        }
        return lock_existing_chain(directory, error) && create_missing(error);
    }
};

} // namespace velocitycopy::detail
