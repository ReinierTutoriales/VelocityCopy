#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <functional>

namespace velocitycopy {

struct CopyProgress {
    std::uint64_t total_bytes{};
    std::uint64_t transferred_bytes{};
};

enum class CopyDecision {
    Continue,
    Pause,
    Stop,
    Skip,
    Cancel,
};

using ProgressCallback = std::function<CopyDecision(const CopyProgress&)>;

enum class ExistingDestinationPolicy {
    Fail,
    Replace,
};

// Keeps the destination folder chain of the previous file opened and locked
// (no delete sharing, so it cannot be renamed or swapped for a junction) and
// reuses it when the next file goes to the same folder, instead of reopening
// every ancestor folder for each file. One per copy worker; destroying it
// releases the folders.
class DestinationLease final {
public:
    DestinationLease();
    ~DestinationLease();
    DestinationLease(const DestinationLease&) = delete;
    DestinationLease& operator=(const DestinationLease&) = delete;

private:
    friend class CopyEngine;
    struct State;
    std::unique_ptr<State> state_;
};

struct CopyOptions {
    bool resume_from_pause{};
    ExistingDestinationPolicy existing_destination{ExistingDestinationPolicy::Fail};
    std::uint32_t copy_flags{};
    std::uint32_t io_size_bytes{};
    // Optional; without it the folder chain is locked for this file only.
    DestinationLease* lease{};
};

struct CopyResult {
    bool success{};
    std::int32_t native_code{};
};

class CopyEngine final {
public:
    [[nodiscard]] CopyResult copy_file(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        const ProgressCallback& progress = {}) const noexcept;

    [[nodiscard]] CopyResult copy_file(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        const CopyOptions& options,
        const ProgressCallback& progress = {}) const noexcept;

    // Same-volume Move fast path: renames the file in place (MoveFileExW
    // without MOVEFILE_COPY_ALLOWED), so no data is copied. Fails with
    // ERROR_NOT_SAME_DEVICE across volumes; callers then fall back to
    // copy_file + source removal. The destination parent chain is locked with
    // the same non-reparse guard as copy_file.
    [[nodiscard]] CopyResult rename_file(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        ExistingDestinationPolicy existing_destination) const noexcept;
};

} // namespace velocitycopy
