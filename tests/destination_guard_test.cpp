#include "../src/core/destination_path_guard.hpp"

#include <windows.h>

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    const auto root = fs::temp_directory_path() / L"VelocityCopyDestinationGuardSharingTest";
    const auto leaf = root / L"existing";
    const auto renamed = root / L"renamed";
    fs::create_directories(leaf);
    {
        velocitycopy::detail::DestinationPathGuard guard;
        std::error_code error;
        if (!guard.prepare_directory(leaf, error)) {
            std::cerr << "prepare_directory failed: " << error.value() << '\n';
            return 1;
        }
        // Pin the exact leaf, not only an ancestor with an open descendant.
        // An attributes-only handle lets this rename succeed on Windows.
        if (MoveFileExW(leaf.c_str(), renamed.c_str(), 0)) return 2;
        if (GetLastError() != ERROR_SHARING_VIOLATION) return 3;
        const HANDLE writer = CreateFileW(leaf.c_str(), DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (writer != INVALID_HANDLE_VALUE) {
            CloseHandle(writer);
            return 4;
        }
        if (GetLastError() != ERROR_SHARING_VIOLATION) return 5;
    }
    if (!MoveFileExW(leaf.c_str(), renamed.c_str(), 0)) return 6;
    fs::remove_all(root);
    return 0;
}
