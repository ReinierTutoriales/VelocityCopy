#include "../src/core/destination_path_guard.hpp"
#include "velocitycopy/copy_engine.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

void write_text(const fs::path& path, const char* text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

bool create_directory_symlink_if_available(const fs::path& link, const fs::path& target) {
    return CreateSymbolicLinkW(
        link.c_str(),
        target.c_str(),
        SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
}

} // namespace

int wmain() {
    using namespace velocitycopy;

    FILE_ATTRIBUTE_TAG_INFO mount{};
    mount.FileAttributes = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT;
    mount.ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    if (!detail::is_name_surrogate_reparse(mount)) return 1;

    FILE_ATTRIBUTE_TAG_INFO symlink{};
    symlink.FileAttributes = FILE_ATTRIBUTE_REPARSE_POINT;
    symlink.ReparseTag = IO_REPARSE_TAG_SYMLINK;
    if (!detail::is_name_surrogate_reparse(symlink)) return 2;

    FILE_ATTRIBUTE_TAG_INFO cloud{};
    cloud.FileAttributes = FILE_ATTRIBUTE_REPARSE_POINT;
    cloud.ReparseTag = IO_REPARSE_TAG_CLOUD;
    if (detail::is_name_surrogate_reparse(cloud)) return 3;

    FILE_ATTRIBUTE_TAG_INFO ordinary{};
    ordinary.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    ordinary.ReparseTag = 0;
    if (detail::is_name_surrogate_reparse(ordinary)) return 4;

    const auto base = fs::temp_directory_path() / L"VelocityCopyReparseSafetyTest";
    const auto source_root = base / L"source";
    const auto source = source_root / L"payload.txt";
    const auto destination_root = base / L"destination";
    const auto safe_nested = destination_root / L"safe" / L"nested";
    const auto outside = base / L"outside";

    std::error_code ec;
    fs::remove_all(base, ec);
    ec.clear();
    fs::create_directories(source_root, ec);
    fs::create_directories(destination_root, ec);
    fs::create_directories(outside, ec);
    if (ec) {
        fs::remove_all(base, ec);
        return 5;
    }
    write_text(source, "payload");

    {
        detail::DestinationDirectoryGuard guard;
        if (guard.prepare(safe_nested) != S_OK || !fs::is_directory(safe_nested)) {
            fs::remove_all(base, ec);
            return 6;
        }
    }

    const auto redirect = destination_root / L"redirect";
    if (create_directory_symlink_if_available(redirect, outside)) {
        detail::DestinationDirectoryGuard guard;
        const auto result = guard.prepare(redirect / L"must-not-exist");
        if (result != static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_CANT_ACCESS_FILE)) ||
            fs::exists(outside / L"must-not-exist")) {
            fs::remove_all(base, ec);
            return 7;
        }

        CopyEngine engine;
        const auto copied = engine.copy_file(source, redirect / L"payload.txt");
        if (copied.success ||
            copied.native_code != static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_CANT_ACCESS_FILE)) ||
            fs::exists(outside / L"payload.txt")) {
            fs::remove_all(base, ec);
            return 8;
        }
    }

    fs::remove_all(base, ec);
    return 0;
}
