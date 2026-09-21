#include "velocitycopy/storage_profiler.hpp"

#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <vector>
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

bool open_volume(const std::filesystem::path& volume_root, HANDLE& volume) noexcept {
    std::array<wchar_t, 64> volume_name{};
    if (GetVolumeNameForVolumeMountPointW(
            volume_root.c_str(),
            volume_name.data(),
            static_cast<DWORD>(volume_name.size())) == 0) {
        return false;
    }

    std::wstring device_path = volume_name.data();
    if (!device_path.empty() && device_path.back() == L'\\') {
        device_path.pop_back();
    }

    volume = CreateFileW(
        device_path.c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    return volume != INVALID_HANDLE_VALUE;
}

void query_physical_disk_extents(const std::filesystem::path& volume_root, StorageProfile& profile) noexcept {
    HANDLE volume = INVALID_HANDLE_VALUE;
    if (!open_volume(volume_root, volume)) {
        return;
    }

    std::vector<std::byte> buffer(sizeof(VOLUME_DISK_EXTENTS) + sizeof(DISK_EXTENT) * 7);
    for (;;) {
        DWORD bytes_returned = 0;
        if (DeviceIoControl(
                volume,
                IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                nullptr,
                0,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytes_returned,
                nullptr) != 0) {
            const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer.data());
            profile.physical_disk_numbers.reserve(extents->NumberOfDiskExtents);
            for (DWORD index = 0; index < extents->NumberOfDiskExtents; ++index) {
                profile.physical_disk_numbers.push_back(extents->Extents[index].DiskNumber);
            }
            std::sort(profile.physical_disk_numbers.begin(), profile.physical_disk_numbers.end());
            profile.physical_disk_numbers.erase(
                std::unique(profile.physical_disk_numbers.begin(), profile.physical_disk_numbers.end()),
                profile.physical_disk_numbers.end());
            profile.physical_disk_extents_available = !profile.physical_disk_numbers.empty();
            break;
        }

        if (GetLastError() != ERROR_MORE_DATA) {
            break;
        }
        if (buffer.size() > 1024 * 1024) {
            break;
        }
        buffer.resize(buffer.size() * 2);
    }

    CloseHandle(volume);
}

void query_device_number(const std::filesystem::path& volume_root, StorageProfile& profile) noexcept {
    // GetVolumePathNameW may return a drive root or a mounted-folder root.
    // Resolve that mount point to its stable volume GUID path before opening
    // the volume. Microsoft documents that CreateFile must receive the GUID
    // path without its trailing backslash when opening the volume itself.
    std::array<wchar_t, 64> volume_name{};
    if (GetVolumeNameForVolumeMountPointW(
            volume_root.c_str(),
            volume_name.data(),
            static_cast<DWORD>(volume_name.size())) == 0) {
        return;
    }

    std::wstring device_path = volume_name.data();
    if (!device_path.empty() && device_path.back() == L'\\') {
        device_path.pop_back();
    }

    const HANDLE volume = CreateFileW(
        device_path.c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (volume == INVALID_HANDLE_VALUE) {
        return;
    }

    STORAGE_DEVICE_NUMBER device_number{};
    DWORD bytes_returned = 0;
    if (DeviceIoControl(
            volume,
            IOCTL_STORAGE_GET_DEVICE_NUMBER,
            nullptr,
            0,
            &device_number,
            sizeof(device_number),
            &bytes_returned,
            nullptr) != 0 &&
        bytes_returned >= sizeof(device_number) &&
        device_number.DeviceNumber != 0xFFFFFFFFu) {
        profile.device_type = device_number.DeviceType;
        profile.device_number = device_number.DeviceNumber;
        profile.device_number_available = true;
    }

    CloseHandle(volume);
}

void query_seek_penalty(const std::filesystem::path& volume_root, StorageProfile& profile) noexcept {
    const auto root = volume_root.wstring();
    if (root.size() < 2 || root[1] != L':') {
        return;
    }

    std::wstring device_path = L"\\\\.\\";
    device_path.push_back(root[0]);
    device_path.push_back(L':');

    const HANDLE volume = CreateFileW(
        device_path.c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (volume == INVALID_HANDLE_VALUE) {
        return;
    }

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;

    DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor{};
    DWORD bytes_returned = 0;
    if (DeviceIoControl(
            volume,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &query,
            sizeof(query),
            &descriptor,
            sizeof(descriptor),
            &bytes_returned,
            nullptr) != 0 &&
        bytes_returned >= sizeof(descriptor)) {
        profile.incurs_seek_penalty = descriptor.IncursSeekPenalty != FALSE;
        profile.seek_penalty_available = true;
    }

    CloseHandle(volume);
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

        // Low-level volume opens and DeviceIoControl probes are useful for local
        // fixed disks, but can block for a long time on removable media that is
        // slow, sleeping, disconnected, or failing. Removable/network/optical
        // transfers already fall back to conservative single-worker behavior, so
        // do not put those synchronous hardware probes on the transfer path.
        if (profile.kind == StorageKind::Fixed) {
            query_device_number(profile.volume_root, profile);
            query_physical_disk_extents(profile.volume_root, profile);
            query_seek_penalty(profile.volume_root, profile);
        }
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
