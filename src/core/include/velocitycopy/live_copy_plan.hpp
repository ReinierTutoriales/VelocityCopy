#pragma once

#include "velocitycopy/job_planner.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>

namespace velocitycopy {

struct LiveCopyPlanSnapshot {
    std::vector<PlannedFile> pending_files;
    std::vector<PlannedFile> active_files;
    std::uint64_t total_bytes{};
    std::uint64_t total_files{};
    std::uint64_t completed_bytes{};
    std::uint64_t completed_files{};
};

struct LiveQueueView {
    std::vector<PlannedFile> pending_files;
    std::uint64_t pending_count{};
    std::uint64_t active_count{};
    std::uint64_t completed_files{};
};

enum class LivePlanAppendResult {
    Appended,
    Drained,
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
    [[nodiscard]] LiveQueueView queue_view(std::size_t max_items) const;

    // Appends a separately planned batch to this live operation. File ids from
    // the incoming plan are remapped so callers may safely append plans that
    // each start numbering at 1. A drained plan is only reopened when the UI
    // session has already reserved the append before the last worker drained.
    [[nodiscard]] LivePlanAppendResult append(CopyPlan plan, bool allow_drained = false) noexcept;

    [[nodiscard]] bool move_pending_file(std::uint64_t file_id, std::size_t new_index) noexcept;
    [[nodiscard]] bool move_pending_file_up(std::uint64_t file_id) noexcept;
    [[nodiscard]] bool move_pending_file_down(std::uint64_t file_id) noexcept;
    // Reorders the currently-pending members named by ordered_file_ids in one
    // O(n + k) pass. Missing ids (for example a file acquired by a worker while
    // the user was dragging) are ignored. Non-mentioned files keep both their
    // relative order and their positions outside the reordered subset.
    [[nodiscard]] bool reorder_pending_files(const std::vector<std::uint64_t>& ordered_file_ids) noexcept;
    [[nodiscard]] bool remove_pending_file(std::uint64_t file_id) noexcept;

    // Each successful acquisition moves one pending file into the active set.
    // Multiple callers may therefore hold distinct active files concurrently.
    [[nodiscard]] std::optional<PlannedFile> acquire_next() noexcept;
    void complete_active(std::uint64_t file_id) noexcept;
    void release_active(std::uint64_t file_id) noexcept;

    [[nodiscard]] std::uint64_t total_bytes() const noexcept;
    [[nodiscard]] std::uint64_t total_files() const noexcept;
    [[nodiscard]] std::uint64_t completed_bytes() const noexcept;
    [[nodiscard]] std::uint64_t completed_files() const noexcept;
    [[nodiscard]] std::uint64_t remaining_files() const noexcept;
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
    std::unordered_set<std::wstring> reserved_destination_keys_;
    std::uint64_t total_bytes_{};
    std::uint64_t total_files_{};
    std::uint64_t completed_bytes_{};
    std::uint64_t completed_files_{};
    std::uint64_t largest_file_bytes_{};
    std::uint64_t next_file_id_{1};
};

} // namespace velocitycopy
