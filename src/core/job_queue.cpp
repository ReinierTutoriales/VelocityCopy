#include "velocitycopy/job_queue.hpp"

#include <algorithm>
#include <utility>

namespace velocitycopy {

bool JobQueue::empty() const noexcept {
    return jobs_.empty();
}

std::size_t JobQueue::size() const noexcept {
    return jobs_.size();
}

void JobQueue::enqueue(CopyJob job) {
    job.state = JobState::Pending;
    jobs_.push_back(std::move(job));
}

bool JobQueue::remove_pending(const std::uint64_t job_id) noexcept {
    const auto it = std::find_if(jobs_.begin(), jobs_.end(), [job_id](const CopyJob& job) {
        return job.id == job_id && job.state == JobState::Pending;
    });

    if (it == jobs_.end()) {
        return false;
    }

    jobs_.erase(it);
    return true;
}

bool JobQueue::move_pending(const std::uint64_t job_id, const std::size_t new_index) noexcept {
    const auto it = std::find_if(jobs_.begin(), jobs_.end(), [job_id](const CopyJob& job) {
        return job.id == job_id && job.state == JobState::Pending;
    });

    if (it == jobs_.end() || new_index >= jobs_.size()) {
        return false;
    }

    const auto current_index = static_cast<std::size_t>(std::distance(jobs_.begin(), it));
    if (current_index == new_index) {
        return true;
    }

    CopyJob job = std::move(*it);
    jobs_.erase(it);
    jobs_.insert(jobs_.begin() + static_cast<std::ptrdiff_t>(new_index), std::move(job));
    return true;
}

const std::deque<CopyJob>& JobQueue::jobs() const noexcept {
    return jobs_;
}

std::optional<std::uint64_t> JobQueue::active_job_id() const noexcept {
    return active_job_id_;
}

std::optional<std::size_t> JobQueue::next_pending_index() const noexcept {
    for (std::size_t index = 0; index < jobs_.size(); ++index) {
        if (jobs_[index].state == JobState::Pending) {
            return index;
        }
    }

    return std::nullopt;
}

JobResult JobQueue::execute_next(const QueueProgressCallback& progress) noexcept {
    const auto pending_index = next_pending_index();
    if (!pending_index) {
        return {true, false, 0};
    }

    auto& job = jobs_[*pending_index];
    job.state = JobState::Running;
    active_job_id_ = job.id;

    const auto result = executor_.execute(job, [&](const JobProgress& job_progress) {
        if (!progress) {
            return JobDecision::Continue;
        }

        QueueProgress queue_progress{};
        queue_progress.active_job_id = job.id;
        queue_progress.active_index = *pending_index;
        queue_progress.total_jobs = jobs_.size();
        queue_progress.job_progress = job_progress;
        return progress(queue_progress);
    });

    active_job_id_.reset();

    if (result.cancelled) {
        job.state = JobState::Cancelled;
    } else if (result.success) {
        job.state = JobState::Completed;
    } else {
        job.state = JobState::Failed;
    }

    return result;
}

std::size_t JobQueue::execute_all(const QueueProgressCallback& progress) noexcept {
    std::size_t processed = 0;

    while (next_pending_index()) {
        const auto result = execute_next(progress);
        ++processed;

        if (result.cancelled) {
            break;
        }
    }

    return processed;
}

} // namespace velocitycopy
