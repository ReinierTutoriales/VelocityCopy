#pragma once

#include "velocitycopy/source_removal_recovery.hpp"

#include <windows.h>

#include <algorithm>
#include <iterator>

namespace velocitycopy::detail {

inline FingerprintProbeResult fingerprint_from_handle(const HANDLE handle) noexcept {
    FILE_STANDARD_INFO standard{};
    FILE_BASIC_INFO basic{};
    if (!GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)) ||
        !GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic))) {
        return {FingerprintProbe::Unavailable, {}, static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError()))};
    }
    if (standard.Directory) {
        return {FingerprintProbe::Unavailable, {}, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_DIRECTORY))};
    }
    FileFingerprint fingerprint{};
    fingerprint.size = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart);
    fingerprint.last_write_time = basic.LastWriteTime.QuadPart;
    FILE_ID_INFO id{};
    if (GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id))) {
        fingerprint.has_identity = true;
        fingerprint.identity.volume_serial = id.VolumeSerialNumber;
        std::copy(std::begin(id.FileId.Identifier), std::end(id.FileId.Identifier), fingerprint.identity.file_id.begin());
    }
    return {FingerprintProbe::Present, fingerprint, S_OK};
}

inline bool same_file_identity(const FileFingerprint& left, const FileFingerprint& right) noexcept {
    return left.has_identity && right.has_identity &&
        left.identity.volume_serial == right.identity.volume_serial &&
        left.identity.file_id == right.identity.file_id;
}

inline bool same_file_fingerprint(const FileFingerprint& left, const FileFingerprint& right) noexcept {
    return same_file_identity(left, right) && left.size == right.size &&
        left.last_write_time == right.last_write_time;
}

} // namespace velocitycopy::detail
