#pragma once

#include "velocitycopy/copy_job.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <system_error>
#include <vector>

namespace velocitycopy {

struct PlannedDirectory {
    std::filesystem::path destination;
};

struct PlannedFile {
    std::uint64_t id{};
    std::filesystem::path source;
    std::filesystem::path destination;
    std::uint64_t size{};
};

// An entry found while planning that cannot be transferred (unreadable
// folder, junction/symlink, unsupported file type, file gone mid-scan). It is
// reported as a Failed item; the rest of the job still runs.
struct PlanningFailure {
    std::filesystem::path source;
    std::filesystem::path destination;
    std::int32_t hresult{};
};

struct CopyPlan {
    std::vector<PlannedDirectory> directories;
    std::vector<PlannedFile> files;
    std::vector<std::filesystem::path> source_roots;
    std::filesystem::path destination_root;
    FileOperation operation{FileOperation::Copy};
    std::uint64_t total_bytes{};
    std::uint64_t largest_file_bytes{};
    std::vector<PlanningFailure> failures;

    [[nodiscard]] bool move_file(std::uint64_t file_id, std::size_t new_index) noexcept;
    [[nodiscard]] bool move_file_up(std::uint64_t file_id) noexcept;
    [[nodiscard]] bool move_file_down(std::uint64_t file_id) noexcept;
    [[nodiscard]] bool remove_file(std::uint64_t file_id) noexcept;
};

// Maps a planner failure to an HRESULT the UI can explain. Planner validation
// errors use std::errc (generic category); their numeric values are POSIX errno
// values and must not be reinterpreted as Win32 codes (errc::file_exists == 17
// would otherwise read as ERROR_NOT_SAME_DEVICE).
[[nodiscard]] std::int32_t planning_error_hresult(const std::error_code& code) noexcept;

class JobPlanner final {
public:
    [[nodiscard]] CopyPlan build(const CopyJob& job) const;
    [[nodiscard]] CopyPlan build(const CopyJob& job, std::stop_token stop_token) const;
};

} // namespace velocitycopy
