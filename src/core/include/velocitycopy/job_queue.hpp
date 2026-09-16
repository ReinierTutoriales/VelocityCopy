#pragma once

#include "velocitycopy/copy_job.hpp"
#include "velocitycopy/job_executor.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>

namespace velocitycopy {

struct QueueProgress {
    std::uint64_t active_job_id{};
    std::size_t active_index{};
    std::size_t total_jobs{};
    JobProgress job_progress{};
};

using QueueProgressCallback = std::function<JobDecision(const QueueProgress&)>;

class JobQueue final {
public:
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    void enqueue(CopyJob job);
    [[nodiscard]] bool remove_pending(std::uint64_t job_id) noexcept;
    [[nodiscard]] bool move_pending(std::uint64_t job_id, std::size_t new_index) noexcept;

    [[nodiscard]] const std::deque<CopyJob>& jobs() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> active_job_id() const noexcept;

    [[nodiscard]] JobResult execute_next(
        const QueueProgressCallback& progress = {}) noexcept;

    [[nodiscard]] std::size_t execute_all(
        const QueueProgressCallback& progress = {}) noexcept;

private:
    [[nodiscard]] std::optional<std::size_t> next_pending_index() const noexcept;

    std::deque<CopyJob> jobs_;
    JobExecutor executor_;
    std::optional<std::uint64_t> active_job_id_;
};

} // namespace velocitycopy
