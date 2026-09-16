#include "velocitycopy/storage_profiler.hpp"

#include <windows.h>

#include <array>
#include <system_error>

namespace velocitycopy {
namespace {

StorageKind map_drive_type(const UINT type) noexcept {
    switch (type) {
    case DRIVE_FIXED: return StorageKind::Fixed;
    case DRIVE_REMOVABLE: return StorageKind::Removable;
    case DRIVE_REMOTE: return StorageKind::Network;
    case DRIVE_CDROM: return StorageKind::Optical;
    case DRIVE_RAMDISK: return StorageKind::RamDisk;
    default: return StorageKind::Unknown;
    }
}

std::filesystem::path nearest_existing_path(std::filesystem::path path) noexcept {
    std::error_code ec;
    while (!path.empty() && !std::filesystem::exists(path, ec)) {
        path = path.parent_path();
        ec.clear();
    }
    return path;
}

} // namespace

StorageProfile StorageProfiler::inspect(const std::filesystem::path& path) const noexcept {
    StorageProfile profile{};

    const auto existing = nearest_existing_path(path);
    if (existing.empty()) {
        return profile;
    }

    std::array<wchar_t, MAX_PATH> volume_root{};
    if (GetVolumePathNameW(existing.c_str(), volume_root.data(), static_cast<DWORD>(volume_root.size())) != 0) {
        profile.volume_root = volume_root.data();
        const auto drive_type = GetDriveTypeW(volume_root.data());
        profile.kind = map_drive_type(drive_type);
        profile.remote = drive_type == DRIVE_REMOTE;
    }

    std::error_code directory_error;
    const bool is_directory = std::filesystem::is_directory(existing, directory_error);
    if (directory_error) {
        return profile;
    }

    const DWORD flags = is_directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL;

    const HANDLE handle = CreateFileW(
        existing.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        flags,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        return profile;
    }

    FILE_STORAGE_INFO info{};
    if (GetFileInformationByHandleEx(handle, FileStorageInfo, &info, sizeof(info)) != 0) {
        profile.logical_sector_bytes = info.LogicalBytesPerSector;
        profile.physical_sector_bytes = info.PhysicalBytesPerSectorForPerformance;
        profile.sector_info_available = profile.logical_sector_bytes != 0 && profile.physical_sector_bytes != 0;
    }

    CloseHandle(handle);
    return profile;
}

} // namespace velocitycopy
