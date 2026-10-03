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

struct DestinationPathGuard final {
    std::vector<HANDLE> handles;
    std::vector<std::filesystem::path> missing;
    std::vector<std::filesystem::path> created;

    DestinationPathGuard() = default;
    DestinationPathGuard(const DestinationPathGuard&) = delete;
    DestinationPathGuard& operator=(const DestinationPathGuard&) = delete;

    ~DestinationPathGuard() noexcept {
        for (const HANDLE handle : handles) {
            CloseHandle(handle);
        }
    }

    void rollback_created() noexcept {
        for (auto it = created.rbegin(); it != created.rend(); ++it) {
            std::error_code ec;
            std::filesystem::remove(*it, ec);
        }
        created.clear();
    }

    bool lock_existing_chain(const std::filesystem::path& path, std::error_code& error) noexcept {
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

            if (name_surrogate_reparse(handle)) {
                CloseHandle(handle);
                error = std::error_code(ERROR_CANT_ACCESS_FILE, std::system_category());
                return false;
            }

            handles.push_back(handle);
            probe = probe.parent_path();
        }
        return true;
    }

    bool create_missing(std::error_code& error) noexcept {
        for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
            const BOOL created_now = CreateDirectoryW(it->c_str(), nullptr);
            if (created_now == 0) {
                const DWORD native = GetLastError();
                if (native != ERROR_ALREADY_EXISTS) {
                    error = std::error_code(static_cast<int>(native), std::system_category());
                    rollback_created();
                    return false;
                }
            } else {
                created.push_back(*it);
            }

            const HANDLE handle = CreateFileW(
                it->c_str(),
                FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr);
            if (handle == INVALID_HANDLE_VALUE || name_surrogate_reparse(handle)) {
                if (handle != INVALID_HANDLE_VALUE) {
                    CloseHandle(handle);
                }
                error = std::error_code(ERROR_CANT_ACCESS_FILE, std::system_category());
                rollback_created();
                return false;
            }

            handles.push_back(handle);
        }
        missing.clear();
        return true;
    }

    bool prepare_directory(const std::filesystem::path& directory, std::error_code& error) noexcept {
        if (directory.empty()) {
            return true;
        }
        return lock_existing_chain(directory, error) && create_missing(error);
    }
};

} // namespace velocitycopy::detail
