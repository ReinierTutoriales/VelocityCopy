#include "velocitycopy/live_copy_plan.hpp"

#include <algorithm>
#include <utility>

namespace velocitycopy {

LiveCopyPlan::LiveCopyPlan(CopyPlan plan)
    : directories_(std::move(plan.directories)),
      pending_files_(std::move(plan.files)),
      total_bytes_(plan.total_bytes),
      total_files_(pending_files_.size()) {
}

const std::vector<PlannedDirectory>& LiveCopyPlan::directories() const noexcept {
    return directories_;
}

LiveCopyPlanSnapshot LiveCopyPlan::snapshot() const {
    std::lock_guard lock(mutex_);
    return {
        pending_files_,
        active_file_,
        total_bytes_,
        total_files_,
    };
}

std::vector<PlannedFile>::iterator LiveCopyPlan::find_pending(const std::uint64_t file_id) noexcept {
    return std::find_if(pending_files_.begin(), pending_files_.end(), [file_id](const PlannedFile& file) {
        return file.id == file_id;
    });
}

bool LiveCopyPlan::move_pending_file(const std::uint64_t file_id, const std::size_t new_index) noexcept {
    std::lock_guard lock(mutex_);
    auto it = find_pending(file_id);
    if (it == pending_files_.end() || new_index >= pending_files_.size()) {
        return false;
    }

    const auto current_index = static_cast<std::size_t>(std::distance(pending_files_.begin(), it));
    if (current_index == new_index) {
        return true;
    }

    if (current_index < new_index) {
        std::rotate(it, it + 1, pending_files_.begin() + static_cast<std::ptrdiff_t>(new_index + 1));
    } else {
        std::rotate(pending_files_.begin() + static_cast<std::ptrdiff_t>(new_index), it, it + 1);
    }
    return true;
}

bool LiveCopyPlan::move_pending_file_up(const std::uint64_t file_id) noexcept {
    std::lock_guard lock(mutex_);
    auto it = find_pending(file_id);
    if (it == pending_files_.end() || it == pending_files_.begin()) {
        return false;
    }
    std::iter_swap(it, it - 1);
    return true;
}

bool LiveCopyPlan::move_pending_file_down(const std::uint64_t file_id) noexcept {
    std::lock_guard lock(mutex_);
    auto it = find_pending(file_id);
    if (it == pending_files_.end() || std::next(it) == pending_files_.end()) {
        return false;
    }
    std::iter_swap(it, it + 1);
    return true;
}

bool LiveCopyPlan::remove_pending_file(const std::uint64_t file_id) noexcept {
    std::lock_guard lock(mutex_);
    auto it = find_pending(file_id);
    if (it == pending_files_.end()) {
        return false;
    }

    total_bytes_ -= it->size;
    --total_files_;
    pending_files_.erase(it);
    return true;
}

std::optional<PlannedFile> LiveCopyPlan::acquire_next() noexcept {
    std::lock_guard lock(mutex_);
    if (active_file_ || pending_files_.empty()) {
        return std::nullopt;
    }

    active_file_ = std::move(pending_files_.front());
    pending_files_.erase(pending_files_.begin());
    return active_file_;
}

void LiveCopyPlan::complete_active(const std::uint64_t file_id) noexcept {
    std::lock_guard lock(mutex_);
    if (active_file_ && active_file_->id == file_id) {
        active_file_.reset();
    }
}

void LiveCopyPlan::release_active(const std::uint64_t file_id) noexcept {
    std::lock_guard lock(mutex_);
    if (!active_file_ || active_file_->id != file_id) {
        return;
    }
    pending_files_.insert(pending_files_.begin(), std::move(*active_file_));
    active_file_.reset();
}

std::uint64_t LiveCopyPlan::total_bytes() const noexcept {
    std::lock_guard lock(mutex_);
    return total_bytes_;
}

std::uint64_t LiveCopyPlan::total_files() const noexcept {
    std::lock_guard lock(mutex_);
    return total_files_;
}

} // namespace velocitycopy
