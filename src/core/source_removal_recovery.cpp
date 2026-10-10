#include "velocitycopy/source_removal_recovery.hpp"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <iterator>

namespace velocitycopy {
namespace {

bool root_is_accessible(const std::filesystem::path& path) noexcept {
    wchar_t volume_path[MAX_PATH]{};
    if (!GetVolumePathNameW(path.c_str(), volume_path, MAX_PATH)) return false;
    const auto attributes = GetFileAttributesW(volume_path);
    return attributes != INVALID_FILE_ATTRIBUTES;
}

bool same_fingerprint(const FileFingerprint& expected, const FileFingerprint& actual) noexcept {
    if (expected.size != actual.size || expected.last_write_time != actual.last_write_time) return false;
    if (!expected.has_identity) return true;
    return actual.has_identity &&
        expected.identity.volume_serial == actual.identity.volume_serial &&
        expected.identity.file_id == actual.identity.file_id;
}

} // namespace

FingerprintProbeResult probe_file_fingerprint(const std::filesystem::path& path) noexcept {
    const HANDLE handle = CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        const auto hr = static_cast<std::int32_t>(HRESULT_FROM_WIN32(error));
        if (error == ERROR_FILE_NOT_FOUND ||
            (error == ERROR_PATH_NOT_FOUND && root_is_accessible(path))) {
            return {FingerprintProbe::Missing, {}, hr};
        }
        return {FingerprintProbe::Unavailable, {}, hr};
    }

    FILE_STANDARD_INFO standard{};
    FILE_BASIC_INFO basic{};
    if (!GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)) ||
        !GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic))) {
        const auto hr = static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError()));
        CloseHandle(handle);
        return {FingerprintProbe::Unavailable, {}, hr};
    }

    FileFingerprint fingerprint{};
    fingerprint.size = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart);
    fingerprint.last_write_time = basic.LastWriteTime.QuadPart;

    FILE_ID_INFO id{};
    if (GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id))) {
        fingerprint.has_identity = true;
        fingerprint.identity.volume_serial = id.VolumeSerialNumber;
        std::copy(std::begin(id.FileId.Identifier), std::end(id.FileId.Identifier),
                  fingerprint.identity.file_id.begin());
    }
    CloseHandle(handle);
    return {FingerprintProbe::Present, fingerprint, S_OK};
}

SourceRemovalValidation validate_source_removal_recovery(
    const SourceRemovalRecovery& recovery) noexcept {
    const auto destination = probe_file_fingerprint(recovery.destination);
    if (destination.status == FingerprintProbe::Unavailable) {
        return SourceRemovalValidation::TemporarilyUnavailable;
    }
    if (destination.status == FingerprintProbe::Missing ||
        !same_fingerprint(recovery.destination_fingerprint, destination.fingerprint)) {
        return SourceRemovalValidation::ChangedOrMissing;
    }

    const auto source = probe_file_fingerprint(recovery.source);
    if (source.status == FingerprintProbe::Unavailable) {
        return SourceRemovalValidation::TemporarilyUnavailable;
    }
    if (source.status == FingerprintProbe::Missing) {
        return SourceRemovalValidation::AlreadyRemoved;
    }
    if (!same_fingerprint(recovery.source_fingerprint, source.fingerprint)) {
        return SourceRemovalValidation::ChangedOrMissing;
    }
    // The "source" must be a different file from the verified copy; a queue
    // file naming one file as both would otherwise delete the only copy.
    if (source.fingerprint.has_identity && destination.fingerprint.has_identity &&
        source.fingerprint.identity.volume_serial == destination.fingerprint.identity.volume_serial &&
        source.fingerprint.identity.file_id == destination.fingerprint.identity.file_id) {
        return SourceRemovalValidation::ChangedOrMissing;
    }
    if (_wcsicmp(recovery.source.lexically_normal().c_str(), recovery.destination.lexically_normal().c_str()) == 0) {
        return SourceRemovalValidation::ChangedOrMissing;
    }
    return SourceRemovalValidation::Verified;
}

} // namespace velocitycopy
