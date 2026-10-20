#include "velocitycopy/source_removal_recovery.hpp"
#include "destination_path_guard.hpp"
#include "win32_handle.hpp"

#include <windows.h>

#include <algorithm>
#include <iterator>
#include <new>
#include <system_error>

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

bool matches_pinned_file(const HANDLE handle, const FileFingerprint& expected) noexcept {
    FILE_STANDARD_INFO standard{};
    FILE_BASIC_INFO basic{};
    FILE_ID_INFO id{};
    if (!expected.has_identity || detail::name_surrogate_reparse(handle) ||
        !GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)) ||
        !GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) ||
        !GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id)) || standard.Directory) {
        return false;
    }
    return expected.size == static_cast<std::uint64_t>(standard.EndOfFile.QuadPart) &&
        expected.last_write_time == basic.LastWriteTime.QuadPart &&
        expected.identity.volume_serial == id.VolumeSerialNumber &&
        std::equal(expected.identity.file_id.begin(), expected.identity.file_id.end(),
            std::begin(id.FileId.Identifier));
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
    return SourceRemovalValidation::Verified;
}

std::int32_t remove_verified_recovery_source(const SourceRemovalRecovery& recovery) noexcept {
    try {
        detail::DestinationPathGuard source_ancestors;
        detail::DestinationPathGuard destination_ancestors;
        std::error_code error;
        if (!source_ancestors.lock_existing_chain(recovery.source.parent_path(), error, FILE_SHARE_READ) ||
            !destination_ancestors.lock_existing_chain(recovery.destination.parent_path(), error, FILE_SHARE_READ)) {
            return static_cast<std::int32_t>(HRESULT_FROM_WIN32(error.value()));
        }
        // Refuse existing writers/deleters and prevent new ones until deletion
        // completes. OPEN_REPARSE_POINT inspects the named objects themselves.
        // Metadata-only access does not participate in Windows share checks.
        // Request data-read access to exclude an already-open data writer.
        detail::Win32Handle destination(CreateFileW(recovery.destination.c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (destination.get() == INVALID_HANDLE_VALUE)
            return static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError()));
        if (!matches_pinned_file(destination.get(), recovery.destination_fingerprint))
            return static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_FILE_INVALID));
        detail::Win32Handle source(CreateFileW(recovery.source.c_str(), DELETE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (source.get() == INVALID_HANDLE_VALUE) {
            const auto native = GetLastError();
            return native == ERROR_FILE_NOT_FOUND ? S_OK :
                static_cast<std::int32_t>(HRESULT_FROM_WIN32(native));
        }
        if (!matches_pinned_file(source.get(), recovery.source_fingerprint))
            return static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_FILE_INVALID));
        FILE_DISPOSITION_INFO disposition{TRUE};
        return SetFileInformationByHandle(source.get(), FileDispositionInfo, &disposition, sizeof(disposition))
            ? S_OK : static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError()));
    } catch (const std::bad_alloc&) {
        return static_cast<std::int32_t>(E_OUTOFMEMORY);
    } catch (const std::system_error& error) {
        return static_cast<std::int32_t>(HRESULT_FROM_WIN32(error.code().value()));
    } catch (...) {
        return static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_UNHANDLED_EXCEPTION));
    }
}

} // namespace velocitycopy
