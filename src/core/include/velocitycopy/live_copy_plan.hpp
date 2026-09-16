#pragma once

#include "velocitycopy/job_planner.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>

namespace velocitycopy {

struct LiveCopyPlanSnapshot {
    std::vector<PlannedFile> pending_files;
    std::vector<PlannedFile> active_files;
    std::uint64_t total_bytes{};
    std::uint64_t total_files{};
};

enum class LivePlanAppendResult {
    Appended,
    DifferentDestination,
    DestinationCollision,
    SizeOverflow,
};

class LiveCopyPlan final {
public:
    explicit LiveCopyPlan(CopyPlan plan);

    LiveCopyPlan(const LiveCopyPlan&) = delete;
    LiveCopyPlan& operator=(const LiveCopyPlan&) = delete;

    [[nodiscard]] const std::vector<PlannedDirectory>& directories() const noexcept;
    [[nodiscard]] const std::vector<std::filesystem::path>& source_roots() const noexcept;
    [[nodiscard]] const std::filesystem::path& destination_root() const noexcept;
    [[nodiscard]] LiveCopyPlanSnapshot snapshot() const;

    // Appends a separately planned batch to this live operation. File ids from
    // the incoming plan are remapped so callers may safely append plans that
    // each start numbering at 1.
    [[nodiscard]] LivePlanAppendResult append(CopyPlan plan) noexcept;

    [[nodiscard]] bool move_pending_file(std::uint64_t file_id, std::size_t new_index) noexcept;
    [[nodiscard]] bool move_pending_file_up(std::uint64_t file_id) noexcept;
    [[nodiscard]] bool move_pending_file_down(std::uint64_t file_id) noexcept;
    [[nodiscard]] bool remove_pending_file(std::uint64_t file_id) noexcept;

    // Each successful acquisition moves one pending file into the active set.
    // Multiple callers may therefore hold distinct active files concurrently.
    [[nodiscard]] std::optional<PlannedFile> acquire_next() noexcept;
    void complete_active(std::uint64_t file_id) noexcept;
    void release_active(std::uint64_t file_id) noexcept;

    [[nodiscard]] std::uint64_t total_bytes() const noexcept;
    [[nodiscard]] std::uint64_t total_files() const noexcept;
    [[nodiscard]] std::uint64_t largest_file_bytes() const noexcept;

private:
    [[nodiscard]] std::vector<PlannedFile>::iterator find_pending(std::uint64_t file_id) noexcept;
    [[nodiscard]] std::vector<PlannedFile>::iterator find_active(std::uint64_t file_id) noexcept;

    std::vector<PlannedDirectory> directories_;
    std::vector<std::filesystem::path> source_roots_;
    std::filesystem::path destination_root_;
    mutable std::mutex mutex_;
    std::vector<PlannedFile> pending_files_;
    std::vector<PlannedFile> active_files_;
    std::uint64_t total_bytes_{};
    std::uint64_t total_files_{};
    std::uint64_t largest_file_bytes_{};
    std::uint64_t next_file_id_{1};
};

} // namespace velocitycopy
