#include "velocitycopy/live_copy_plan.hpp"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <unordered_set>
#include <utility>

namespace velocitycopy {
namespace {

std::wstring normalized_path_key(const std::filesystem::path& path) {
    auto value = path.lexically_normal().wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

} // namespace

LiveCopyPlan::LiveCopyPlan(CopyPlan plan)
    : directories_(std::move(plan.directories)),
      source_roots_(std::move(plan.source_roots)),
      destination_root_(std::move(plan.destination_root)),
      pending_files_(std::move(plan.files)),
      total_bytes_(plan.total_bytes),
      total_files_(pending_files_.size()),
      largest_file_bytes_(plan.largest_file_bytes) {
    reserved_destination_keys_.reserve(pending_files_.size());
    for (const auto& file : pending_files_) {
        next_file_id_ = std::max(next_file_id_, file.id + 1);
        reserved_destination_keys_.insert(normalized_path_key(file.destination));
    }
}

const std::vector<PlannedDirectory>& LiveCopyPlan::directories() const noexcept {
    return directories_;
}

const std::vector<std::filesystem::path>& LiveCopyPlan::source_roots() const noexcept {
    return source_roots_;
}

const std::filesystem::path& LiveCopyPlan::destination_root() const noexcept {
    return destination_root_;
}

LiveCopyPlanSnapshot LiveCopyPlan::snapshot() const {
    std::lock_guard lock(mutex_);
    return {
        pending_files_,
        active_files_,
        total_bytes_,
        total_files_,
        completed_bytes_,
        completed_files_,
    };
}

LiveQueueView LiveCopyPlan::queue_view(const std::size_t max_items) const {
    std::lock_guard lock(mutex_);
    const auto count = std::min(max_items, pending_files_.size());

    LiveQueueView view{};
    view.pending_files.reserve(count);
    view.pending_files.insert(
        view.pending_files.end(),
        pending_files_.begin(),
        pending_files_.begin() + static_cast<std::ptrdiff_t>(count));
    view.pending_count = static_cast<std::uint64_t>(pending_files_.size());
    view.active_count = static_cast<std::uint64_t>(active_files_.size());
    view.completed_files = completed_files_;
    return view;
}

LivePlanAppendResult LiveCopyPlan::append(
    CopyPlan plan,
    const bool allow_drained) noexcept {
    try {
        std::lock_guard lock(mutex_);

        if (!allow_drained && pending_files_.empty() && active_files_.empty()) {
            return LivePlanAppendResult::Drained;
        }
        if (normalized_path_key(plan.destination_root) != normalized_path_key(destination_root_)) {
            return LivePlanAppendResult::DifferentDestination;
        }
        if (plan.total_bytes > std::numeric_limits<std::uint64_t>::max() - total_bytes_) {
            return LivePlanAppendResult::SizeOverflow;
        }
        if (plan.files.size() > std::numeric_limits<std::uint64_t>::max() - total_files_) {
            return LivePlanAppendResult::SizeOverflow;
        }

        std::unordered_set<std::wstring> incoming_keys;
        incoming_keys.reserve(plan.files.size());
        for (const auto& file : plan.files) {
            auto key = normalized_path_key(file.destination);
            if (key.empty() || reserved_destination_keys_.contains(key) ||
                !incoming_keys.insert(std::move(key)).second) {
                return LivePlanAppendResult::DestinationCollision;
            }
        }

        // directories_ and source_roots_ are immutable execution metadata.
        // Appended batches have their directories prepared by the background
        // planning worker before this atomic queue merge, avoiding races with
        // the executor's initial directory/strategy reads.
        pending_files_.reserve(pending_files_.size() + plan.files.size());
        reserved_destination_keys_.reserve(reserved_destination_keys_.size() + incoming_keys.size());
        for (auto& file : plan.files) {
            file.id = next_file_id_++;
            reserved_destination_keys_.insert(normalized_path_key(file.destination));
            pending_files_.push_back(std::move(file));
        }

        total_bytes_ += plan.total_bytes;
        total_files_ += static_cast<std::uint64_t>(plan.files.size());
        largest_file_bytes_ = std::max(largest_file_bytes_, plan.largest_file_bytes);
        return LivePlanAppendResult::Appended;
    } catch (...) {
        return LivePlanAppendResult::DestinationCollision;
    }
}

std::vector<PlannedFile>::iterator LiveCopyPlan::find_pending(const std::uint64_t file_id) noexcept {
    return std::find_if(pending_files_.begin(), pending_files_.end(), [file_id](const PlannedFile& file) {
        return file.id == file_id;
    });
}

std::vector<PlannedFile>::iterator LiveCopyPlan::find_active(const std::uint64_t file_id) noexcept {
    return std::find_if(active_files_.begin(), active_files_.end(), [file_id](const PlannedFile& file) {
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
    reserved_destination_keys_.erase(normalized_path_key(it->destination));
    pending_files_.erase(it);
    return true;
}

std::optional<PlannedFile> LiveCopyPlan::acquire_next() noexcept {
    std::lock_guard lock(mutex_);
    if (pending_files_.empty()) {
        return std::nullopt;
    }

    PlannedFile file = std::move(pending_files_.front());
    pending_files_.erase(pending_files_.begin());
    active_files_.push_back(file);
    return file;
}

void LiveCopyPlan::complete_active(const std::uint64_t file_id) noexcept {
    std::lock_guard lock(mutex_);
    auto it = find_active(file_id);
    if (it == active_files_.end()) {
        return;
    }

    const auto remaining_bytes = total_bytes_ > completed_bytes_
        ? total_bytes_ - completed_bytes_
        : 0;
    completed_bytes_ += std::min(it->size, remaining_bytes);
    if (completed_files_ < total_files_) {
        ++completed_files_;
    }
    active_files_.erase(it);
}

void LiveCopyPlan::release_active(const std::uint64_t file_id) noexcept {
    std::lock_guard lock(mutex_);
    auto it = find_active(file_id);
    if (it == active_files_.end()) {
        return;
    }

    PlannedFile file = std::move(*it);
    active_files_.erase(it);
    pending_files_.insert(pending_files_.begin(), std::move(file));
}

std::uint64_t LiveCopyPlan::total_bytes() const noexcept {
    std::lock_guard lock(mutex_);
    return total_bytes_;
}

std::uint64_t LiveCopyPlan::total_files() const noexcept {
    std::lock_guard lock(mutex_);
    return total_files_;
}

std::uint64_t LiveCopyPlan::completed_bytes() const noexcept {
    std::lock_guard lock(mutex_);
    return completed_bytes_;
}

std::uint64_t LiveCopyPlan::completed_files() const noexcept {
    std::lock_guard lock(mutex_);
    return completed_files_;
}

std::uint64_t LiveCopyPlan::remaining_files() const noexcept {
    std::lock_guard lock(mutex_);
    return static_cast<std::uint64_t>(pending_files_.size() + active_files_.size());
}

std::uint64_t LiveCopyPlan::largest_file_bytes() const noexcept {
    std::lock_guard lock(mutex_);
    return largest_file_bytes_;
}

} // namespace velocitycopy
