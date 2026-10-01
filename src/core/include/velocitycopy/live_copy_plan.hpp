#pragma once

#include "velocitycopy/item_result.hpp"
#include "velocitycopy/job_planner.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <unordered_map>
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
    std::uint64_t parked_count{};
    std::uint64_t completed_files{};
};

struct ItemOutcomeCounts {
    std::uint64_t succeeded{};
    std::uint64_t skipped{};
    std::uint64_t failed{};
    std::uint64_t copied_source_retained{};
};

// Per-item resolution view. resolution_weight / resolution_total is the visible
// progress ("all work resolved"); bytes_written_physical feeds throughput only;
// bytes_succeeded is the final statistic.
struct LiveResolutionView {
    TransferCounters counters;
    ItemOutcomeCounts outcomes;
    std::uint64_t pending_files{};
    std::uint64_t active_files{};
    std::uint64_t parked_files{};

    [[nodiscard]] std::uint64_t unresolved_files() const noexcept {
        return pending_files + active_files + parked_files;
    }
};

struct LiveDirectoryBatch {
    std::size_t through_index{};
    std::vector<PlannedDirectory> directories;
};

enum class LivePlanAppendResult {
    Appended,
    Drained,
    DifferentDestination,
    DifferentOperation,
    DestinationCollision,
    SizeOverflow,
    InternalFailure,
};

class LiveCopyPlan final {
public:
    explicit LiveCopyPlan(CopyPlan plan);

    LiveCopyPlan(const LiveCopyPlan&) = delete;
    LiveCopyPlan& operator=(const LiveCopyPlan&) = delete;

    [[nodiscard]] std::vector<PlannedDirectory> directories() const;
    [[nodiscard]] LiveDirectoryBatch pending_directories() const {
        std::lock_guard lock(mutex_);
        LiveDirectoryBatch batch{};
        batch.through_index = directories_.size();
        batch.directories.reserve(batch.through_index - materialized_directory_count_);
        batch.directories.insert(
            batch.directories.end(),
            directories_.begin() + static_cast<std::ptrdiff_t>(materialized_directory_count_),
            directories_.end());
        return batch;
    }
    void mark_directories_materialized(const std::size_t through_index) noexcept {
        std::lock_guard lock(mutex_);
        materialized_directory_count_ = std::max(
            materialized_directory_count_, std::min(through_index, directories_.size()));
    }
    [[nodiscard]] bool has_pending_directories() const noexcept {
        std::lock_guard lock(mutex_);
        return materialized_directory_count_ < directories_.size();
    }
    [[nodiscard]] std::vector<std::filesystem::path> source_roots() const;
    [[nodiscard]] const std::filesystem::path& destination_root() const noexcept;
    [[nodiscard]] FileOperation operation() const noexcept;
    [[nodiscard]] LiveCopyPlanSnapshot snapshot() const;
    [[nodiscard]] LiveQueueView queue_view(std::size_t max_items) const;
    [[nodiscard]] CopyPlan export_remaining_plan() const;

    [[nodiscard]] LivePlanAppendResult append(CopyPlan plan, bool allow_drained = false) noexcept;

    [[nodiscard]] bool move_pending_file(std::uint64_t file_id, std::size_t new_index) noexcept;
    [[nodiscard]] bool move_pending_file_up(std::uint64_t file_id) noexcept;
    [[nodiscard]] bool move_pending_file_down(std::uint64_t file_id) noexcept;
    [[nodiscard]] bool move_pending_files_up(const std::vector<std::uint64_t>& file_ids) noexcept;
    [[nodiscard]] bool move_pending_files_down(const std::vector<std::uint64_t>& file_ids) noexcept;
    [[nodiscard]] bool reorder_pending_files(const std::vector<std::uint64_t>& ordered_file_ids) noexcept;
    [[nodiscard]] bool remove_pending_file(std::uint64_t file_id) noexcept;
    [[nodiscard]] std::size_t remove_pending_files(const std::vector<std::uint64_t>& file_ids) noexcept;

    [[nodiscard]] std::optional<PlannedFile> acquire_next() noexcept;
    void complete_active(std::uint64_t file_id) noexcept;
    void release_active(std::uint64_t file_id) noexcept;
    [[nodiscard]] bool skip_active(std::uint64_t file_id) noexcept;

    // Per-item resolution API (ItemState contract). Runs alongside the legacy
    // complete/release/skip API until the executor migrates.
    //
    // Retention policy: every terminal item gets an ItemOutcome, but only
    // Skipped, Failed and CopiedSourceRetained keep a full ItemResult in
    // memory. Succeeded is counters-only and keeps its destination reservation.
    // Skipped and Failed release the reservation: that destination was not
    // produced.
    [[nodiscard]] bool park_active(
        std::uint64_t file_id, std::int32_t hresult, bool destination_preexisted) noexcept;
    [[nodiscard]] bool unpark(std::uint64_t file_id) noexcept;
    [[nodiscard]] bool resolve_pending(
        std::uint64_t file_id, ItemOutcome outcome, std::int32_t hresult,
        bool destination_preexisted) noexcept;
    [[nodiscard]] bool resolve_active(
        std::uint64_t file_id, ItemOutcome outcome, std::int32_t hresult,
        bool destination_preexisted) noexcept;
    // Resolves every Pending item whose destination is inside `directory` as
    // Failed with `hresult` (e.g. the directory could not be created). Returns
    // the number of items resolved.
    [[nodiscard]] std::size_t fail_pending_under(
        const std::filesystem::path& directory, std::int32_t hresult) noexcept;
    [[nodiscard]] bool resolve_parked(
        std::uint64_t file_id, ItemOutcome outcome, std::int32_t hresult,
        bool destination_preexisted) noexcept;
    // cumulative_attempt_bytes is the CopyFile2 TotalBytesTransferred value of
    // the current attempt. A new attempt starts at acquire_next()/unpark().
    void record_attempt_bytes(std::uint64_t file_id, std::uint64_t cumulative_attempt_bytes) noexcept;

    [[nodiscard]] LiveResolutionView resolution_view() const noexcept;
    [[nodiscard]] std::vector<ItemResult> retained_results() const;
    [[nodiscard]] std::vector<ItemIncident> parked_incidents() const;
    [[nodiscard]] std::uint64_t unresolved_files() const noexcept;


    [[nodiscard]] std::uint64_t total_bytes() const noexcept;
    [[nodiscard]] std::uint64_t total_files() const noexcept;
    [[nodiscard]] std::uint64_t completed_bytes() const noexcept;
    [[nodiscard]] std::uint64_t completed_files() const noexcept;
    [[nodiscard]] std::uint64_t remaining_files() const noexcept;
    [[nodiscard]] std::uint64_t largest_file_bytes() const noexcept;

private:
    [[nodiscard]] std::deque<PlannedFile>::iterator find_pending(std::uint64_t file_id) noexcept;
    [[nodiscard]] std::vector<PlannedFile>::iterator find_active(std::uint64_t file_id) noexcept;
    void recompute_largest_file_bytes_locked() noexcept;

    struct ParkedFile {
        PlannedFile file;
        ItemIncident incident;
    };

    [[nodiscard]] std::vector<ParkedFile>::iterator find_parked(std::uint64_t file_id) noexcept;
    [[nodiscard]] bool resolve_locked(
        const PlannedFile& file, ItemOutcome outcome, std::int32_t hresult,
        bool destination_preexisted);
    void drop_in_flight_locked(std::uint64_t file_id) noexcept;


    std::vector<PlannedDirectory> directories_;
    std::size_t materialized_directory_count_{};
    std::vector<std::filesystem::path> source_roots_;
    std::filesystem::path destination_root_;
    FileOperation operation_{FileOperation::Copy};
    mutable std::mutex mutex_;
    std::deque<PlannedFile> pending_files_;
    std::vector<PlannedFile> active_files_;
    std::unordered_set<std::wstring> reserved_destination_keys_;
    std::uint64_t total_bytes_{};
    std::uint64_t total_files_{};
    std::uint64_t completed_bytes_{};
    std::uint64_t completed_files_{};
    std::uint64_t largest_file_bytes_{};
    std::uint64_t next_file_id_{1};

    std::vector<ParkedFile> parked_files_;
    std::vector<ItemResult> retained_results_;
    std::unordered_map<std::uint64_t, std::uint64_t> high_water_;
    std::unordered_map<std::uint64_t, std::uint64_t> attempt_bytes_;
    TransferCounters counters_{};
    ItemOutcomeCounts outcomes_{};
};

} // namespace velocitycopy
