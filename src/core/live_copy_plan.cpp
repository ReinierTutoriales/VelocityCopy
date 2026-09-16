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
    for (const auto& file : pending_files_) {
        next_file_id_ = std::max(next_file_id_, file.id + 1);
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
    };
}

LivePlanAppendResult LiveCopyPlan::append(CopyPlan plan) noexcept {
    try {
        std::lock_guard lock(mutex_);

        if (normalized_path_key(plan.destination_root) != normalized_path_key(destination_root_)) {
            return LivePlanAppendResult::DifferentDestination;
        }
        if (plan.total_bytes > std::numeric_limits<std::uint64_t>::max() - total_bytes_) {
            return LivePlanAppendResult::SizeOverflow;
        }
        if (plan.files.size() > std::numeric_limits<std::uint64_t>::max() - total_files_) {
            return LivePlanAppendResult::SizeOverflow;
        }

        std::unordered_set<std::wstring> occupied;
        occupied.reserve(pending_files_.size() + active_files_.size() + plan.files.size());
        for (const auto& file : pending_files_) {
            occupied.insert(normalized_path_key(file.destination));
        }
        for (const auto& file : active_files_) {
            occupied.insert(normalized_path_key(file.destination));
        }
        for (const auto& file : plan.files) {
            const auto key = normalized_path_key(file.destination);
            if (key.empty() || !occupied.insert(key).second) {
                return LivePlanAppendResult::DestinationCollision;
            }
        }

        source_roots_.insert(
            source_roots_.end(),
            std::make_move_iterator(plan.source_roots.begin()),
            std::make_move_iterator(plan.source_roots.end()));
        directories_.insert(
            directories_.end(),
            std::make_move_iterator(plan.directories.begin()),
            std::make_move_iterator(plan.directories.end()));

        pending_files_.reserve(pending_files_.size() + plan.files.size());
        for (auto& file : plan.files) {
            file.id = next_file_id_++;
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
    if (it != active_files_.end()) {
        active_files_.erase(it);
    }
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

std::uint64_t LiveCopyPlan::largest_file_bytes() const noexcept {
    return largest_file_bytes_;
}

} // namespace velocitycopy
