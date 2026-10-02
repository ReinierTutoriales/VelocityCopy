#pragma once

#include <array>
#include <cstdint>
#include <filesystem>

namespace velocitycopy {

struct FileIdentity {
    std::uint64_t volume_serial{};
    std::array<std::uint8_t, 16> file_id{};
};

struct FileFingerprint {
    std::uint64_t size{};
    std::int64_t last_write_time{};
    bool has_identity{};
    FileIdentity identity{};
};

struct SourceRemovalRecovery {
    std::uint64_t file_id{};
    std::int32_t hresult{};
    std::filesystem::path source;
    std::filesystem::path destination;
    bool destination_preexisted{};
    std::uint32_t attempt_count{1};
    FileFingerprint source_fingerprint{};
    FileFingerprint destination_fingerprint{};
};

enum class FingerprintProbe {
    Present,
    Missing,
    Unavailable,
};

enum class SourceRemovalValidation {
    Verified,
    ChangedOrMissing,
    TemporarilyUnavailable,
};

struct FingerprintProbeResult {
    FingerprintProbe status{FingerprintProbe::Unavailable};
    FileFingerprint fingerprint{};
    std::int32_t hresult{};
};

[[nodiscard]] FingerprintProbeResult probe_file_fingerprint(
    const std::filesystem::path& path) noexcept;
[[nodiscard]] SourceRemovalValidation validate_source_removal_recovery(
    const SourceRemovalRecovery& recovery) noexcept;

} // namespace velocitycopy
