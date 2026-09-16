#include "velocitycopy/live_copy_plan.hpp"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <unordered_map>
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

std::unordered_set<std::uint64_t> make_id_set(
    const std::vector<std::uint64_t>& file_ids) {
    std::unordered_set<std::uint64_t> ids;
    ids.reserve(file_ids.size());
    ids.insert(file_ids.begin(), file_ids.end());
    return ids;
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
        pending_files_, active_files_, total_bytes_, total_files_,
        completed_bytes_, completed_files_,
    };
}

LiveQueueView LiveCopyPlan::queue_view(const std::size_t max_items) const {
    std::lock_guard lock(mutex_);
    const auto count = std::min(max_items, pending_files_.size());
    LiveQueueView view{};
    view.pending_files.reserve(count);
    view.pending_files.insert(
        view.pending_files.end(), pending_files_.begin(),
        pending_files_.begin() + static_cast<std::ptrdiff_t>(count));
    view.pending_count = static_cast<std::uint64_t>(pending_files_.size());
    view.active_count = static_cast<std::uint64_t>(active_files_.size());
    view.completed_files = completed_files_;
    return view;
}

LivePlanAppendResult LiveCopyPlan::append(CopyPlan plan, const bool allow_drained) noexcept {
    try {
        std::lock_guard lock(mutex_);
        if (!allow_drained && pending_files_.empty() && active_files_.empty()) {
            return LivePlanAppendResult::Drained;
        }
        if (normalized_path_key(plan.destination_root) != normalized_path_key(destination_root_)) {
            return LivePlanAppendResult::DifferentDestination;
        }
        if (plan.total_bytes > std::numeric_limits<std::uint64_t>::max() - total_bytes_ ||
            plan.files.size() > std::numeric_limits<std::uint64_t>::max() - total_files_) {
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

void LiveCopyPlan::recompute_largest_file_bytes_locked() noexcept {
    std::uint64_t largest = 0;
    for (const auto& file : pending_files_) {
        largest = std::max(largest, file.size);
    }
    for (const auto& file : active_files_) {
        largest = std::max(largest, file.size);
    }
    largest_file_bytes_ = largest;
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
    return move_pending_files_up({file_id});
}

bool LiveCopyPlan::move_pending_file_down(const std::uint64_t file_id) noexcept {
    return move_pending_files_down({file_id});
}

bool LiveCopyPlan::move_pending_files_up(const std::vector<std::uint64_t>& file_ids) noexcept {
    try {
        if (file_ids.empty()) return false;
        const auto selected = make_id_set(file_ids);
        std::lock_guard lock(mutex_);
        bool changed = false;
        for (std::size_t index = 1; index < pending_files_.size(); ++index) {
            if (selected.contains(pending_files_[index].id) &&
                !selected.contains(pending_files_[index - 1].id)) {
                std::iter_swap(pending_files_.begin() + static_cast<std::ptrdiff_t>(index - 1),
                               pending_files_.begin() + static_cast<std::ptrdiff_t>(index));
                changed = true;
            }
        }
        return changed;
    } catch (...) {
        return false;
    }
}

bool LiveCopyPlan::move_pending_files_down(const std::vector<std::uint64_t>& file_ids) noexcept {
    try {
        if (file_ids.empty()) return false;
        const auto selected = make_id_set(file_ids);
        std::lock_guard lock(mutex_);
        if (pending_files_.size() < 2) return false;
        bool changed = false;
        for (std::size_t cursor = pending_files_.size() - 1; cursor != 0; --cursor) {
            const auto index = cursor - 1;
            if (selected.contains(pending_files_[index].id) &&
                !selected.contains(pending_files_[index + 1].id)) {
                std::iter_swap(pending_files_.begin() + static_cast<std::ptrdiff_t>(index),
                               pending_files_.begin() + static_cast<std::ptrdiff_t>(index + 1));
                changed = true;
            }
        }
        return changed;
    } catch (...) {
        return false;
    }
}

bool LiveCopyPlan::reorder_pending_files(const std::vector<std::uint64_t>& ordered_file_ids) noexcept {
    try {
        if (ordered_file_ids.size() < 2) return false;
        std::unordered_map<std::uint64_t, std::size_t> rank;
        rank.reserve(ordered_file_ids.size());
        for (std::size_t index = 0; index < ordered_file_ids.size(); ++index) {
            if (!rank.emplace(ordered_file_ids[index], index).second) return false;
        }

        std::lock_guard lock(mutex_);
        std::vector<std::size_t> positions;
        std::vector<PlannedFile> matched;
        positions.reserve(ordered_file_ids.size());
        matched.reserve(ordered_file_ids.size());
        for (std::size_t index = 0; index < pending_files_.size(); ++index) {
            if (rank.contains(pending_files_[index].id)) {
                positions.push_back(index);
                matched.push_back(pending_files_[index]);
            }
        }
        if (matched.size() < 2) return false;

        std::sort(matched.begin(), matched.end(), [&](const PlannedFile& left, const PlannedFile& right) {
            return rank.at(left.id) < rank.at(right.id);
        });
        bool changed = false;
        for (std::size_t index = 0; index < positions.size(); ++index) {
            changed = changed || pending_files_[positions[index]].id != matched[index].id;
            pending_files_[positions[index]] = std::move(matched[index]);
        }
        return changed;
    } catch (...) {
        return false;
    }
}

bool LiveCopyPlan::remove_pending_file(const std::uint64_t file_id) noexcept {
    return remove_pending_files({file_id}) != 0;
}

std::size_t LiveCopyPlan::remove_pending_files(const std::vector<std::uint64_t>& file_ids) noexcept {
    try {
        if (file_ids.empty()) return 0;
        const auto selected = make_id_set(file_ids);
        std::lock_guard lock(mutex_);

        struct RemovalInfo {
            std::uint64_t size{};
            std::wstring destination_key;
        };
        std::vector<RemovalInfo> removals;
        removals.reserve(std::min(selected.size(), pending_files_.size()));
        std::uint64_t removed_bytes = 0;
        bool removed_largest = false;
        for (const auto& file : pending_files_) {
            if (!selected.contains(file.id)) continue;
            removals.push_back({file.size, normalized_path_key(file.destination)});
            removed_bytes += file.size;
            removed_largest = removed_largest || file.size == largest_file_bytes_;
        }
        if (removals.empty()) return 0;

        for (const auto& removal : removals) {
            reserved_destination_keys_.erase(removal.destination_key);
        }
        total_bytes_ -= std::min(total_bytes_, removed_bytes);
        total_files_ -= std::min<std::uint64_t>(
            total_files_, static_cast<std::uint64_t>(removals.size()));
        std::erase_if(pending_files_, [&](const PlannedFile& file) {
            return selected.contains(file.id);
        });
        if (removed_largest) {
            recompute_largest_file_bytes_locked();
        }
        return removals.size();
    } catch (...) {
        return 0;
    }
}

std::optional<PlannedFile> LiveCopyPlan::acquire_next() noexcept {
    std::lock_guard lock(mutex_);
    if (pending_files_.empty()) return std::nullopt;
    PlannedFile file = std::move(pending_files_.front());
    pending_files_.erase(pending_files_.begin());
    active_files_.push_back(file);
    return file;
}

void LiveCopyPlan::complete_active(const std::uint64_t file_id) noexcept {
    std::lock_guard lock(mutex_);
    auto it = find_active(file_id);
    if (it == active_files_.end()) return;
    const auto remaining_bytes = total_bytes_ > completed_bytes_ ? total_bytes_ - completed_bytes_ : 0;
    completed_bytes_ += std::min(it->size, remaining_bytes);
    if (completed_files_ < total_files_) ++completed_files_;
    active_files_.erase(it);
}

void LiveCopyPlan::release_active(const std::uint64_t file_id) noexcept {
    std::lock_guard lock(mutex_);
    auto it = find_active(file_id);
    if (it == active_files_.end()) return;
    PlannedFile file = std::move(*it);
    active_files_.erase(it);
    pending_files_.insert(pending_files_.begin(), std::move(file));
}

bool LiveCopyPlan::skip_active(const std::uint64_t file_id) noexcept {
    try {
        std::lock_guard lock(mutex_);
        auto it = find_active(file_id);
        if (it == active_files_.end()) return false;

        const auto skipped_size = it->size;
        const bool skipped_largest = skipped_size == largest_file_bytes_;
        const auto destination_key = normalized_path_key(it->destination);

        reserved_destination_keys_.erase(destination_key);
        total_bytes_ -= std::min(total_bytes_, skipped_size);
        if (total_files_ != 0) {
            --total_files_;
        }
        active_files_.erase(it);
        if (skipped_largest) {
            recompute_largest_file_bytes_locked();
        }
        return true;
    } catch (...) {
        return false;
    }
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