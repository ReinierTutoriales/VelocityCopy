#include "velocitycopy/live_copy_plan.hpp"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace velocitycopy {
namespace {

static_assert(std::is_nothrow_move_constructible_v<std::filesystem::path>);
static_assert(std::is_nothrow_move_constructible_v<PlannedDirectory>);
static_assert(std::is_nothrow_move_constructible_v<PlannedFile>);

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
      operation_(plan.operation),
      pending_files_(std::make_move_iterator(plan.files.begin()),
                     std::make_move_iterator(plan.files.end())),
      total_bytes_(plan.total_bytes),
      total_files_(pending_files_.size()),
      largest_file_bytes_(plan.largest_file_bytes) {
    reserved_destination_keys_.reserve(pending_files_.size());
    for (const auto& file : pending_files_) {
        if (file.id == std::numeric_limits<std::uint64_t>::max()) {
            next_file_id_ = 0;
        } else if (next_file_id_ != 0) {
            next_file_id_ = std::max(next_file_id_, file.id + 1);
        }
        reserved_destination_keys_.insert(normalized_path_key(file.destination));
        counters_.resolution_total += item_resolution_weight(file.size);
    }
}

std::vector<PlannedDirectory> LiveCopyPlan::directories() const {
    std::lock_guard lock(mutex_);
    return directories_;
}

std::vector<std::filesystem::path> LiveCopyPlan::source_roots() const {
    std::lock_guard lock(mutex_);
    return source_roots_;
}

const std::filesystem::path& LiveCopyPlan::destination_root() const noexcept {
    return destination_root_;
}

FileOperation LiveCopyPlan::operation() const noexcept {
    return operation_;
}

LiveCopyPlanSnapshot LiveCopyPlan::snapshot() const {
    std::lock_guard lock(mutex_);
    LiveCopyPlanSnapshot snapshot{};
    snapshot.pending_files.assign(pending_files_.begin(), pending_files_.end());
    snapshot.active_files = active_files_;
    snapshot.total_bytes = total_bytes_;
    snapshot.total_files = total_files_;
    snapshot.completed_bytes = completed_bytes_;
    snapshot.completed_files = completed_files_;
    return snapshot;
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
    view.parked_count = static_cast<std::uint64_t>(parked_files_.size());
    view.completed_files = completed_files_;
    return view;
}

LivePlanAppendResult LiveCopyPlan::append(CopyPlan plan, const bool allow_drained) noexcept {
    try {
        std::lock_guard lock(mutex_);
        if (!allow_drained && pending_files_.empty() && active_files_.empty() &&
            parked_files_.empty()) {
            return LivePlanAppendResult::Drained;
        }
        if (normalized_path_key(plan.destination_root) != normalized_path_key(destination_root_)) {
            return LivePlanAppendResult::DifferentDestination;
        }
        if (plan.operation != operation_) {
            return LivePlanAppendResult::DifferentOperation;
        }
        if (plan.total_bytes > std::numeric_limits<std::uint64_t>::max() - total_bytes_ ||
            plan.files.size() > std::numeric_limits<std::uint64_t>::max() - total_files_) {
            return LivePlanAppendResult::SizeOverflow;
        }
        if (!plan.files.empty()) {
            if (next_file_id_ == 0) return LivePlanAppendResult::InternalFailure;
            const auto available_ids =
                std::numeric_limits<std::uint64_t>::max() - next_file_id_ + 1;
            if (plan.files.size() > available_ids) return LivePlanAppendResult::InternalFailure;
        }

        std::uint64_t incoming_weight = 0;
        for (const auto& file : plan.files) {
            const auto weight = item_resolution_weight(file.size);
            if (weight > std::numeric_limits<std::uint64_t>::max() - counters_.resolution_total - incoming_weight) {
                return LivePlanAppendResult::SizeOverflow;
            }
            incoming_weight += weight;
        }

        std::vector<std::wstring> incoming_keys;
        incoming_keys.reserve(plan.files.size());
        std::unordered_set<std::wstring> unique_incoming;
        unique_incoming.reserve(plan.files.size());
        for (const auto& file : plan.files) {
            auto key = normalized_path_key(file.destination);
            if (key.empty() || reserved_destination_keys_.contains(key) ||
                !unique_incoming.insert(key).second) {
                return LivePlanAppendResult::DestinationCollision;
            }
            incoming_keys.push_back(std::move(key));
        }

        directories_.reserve(directories_.size() + plan.directories.size());
        source_roots_.reserve(source_roots_.size() + plan.source_roots.size());

        auto staged_destination_keys = reserved_destination_keys_;
        staged_destination_keys.reserve(staged_destination_keys.size() + incoming_keys.size());
        for (const auto& key : incoming_keys) {
            staged_destination_keys.insert(key);
        }

        directories_.insert(
            directories_.end(),
            std::make_move_iterator(plan.directories.begin()),
            std::make_move_iterator(plan.directories.end()));
        source_roots_.insert(
            source_roots_.end(),
            std::make_move_iterator(plan.source_roots.begin()),
            std::make_move_iterator(plan.source_roots.end()));

        std::uint64_t assigned_id = next_file_id_;
        for (auto& file : plan.files) {
            file.id = assigned_id;
            assigned_id = assigned_id == std::numeric_limits<std::uint64_t>::max()
                ? 0
                : assigned_id + 1;
            pending_files_.push_back(std::move(file));
        }

        reserved_destination_keys_.swap(staged_destination_keys);
        next_file_id_ = assigned_id;
        total_bytes_ += plan.total_bytes;
        total_files_ += static_cast<std::uint64_t>(plan.files.size());
        counters_.resolution_total += incoming_weight;
        largest_file_bytes_ = std::max(largest_file_bytes_, plan.largest_file_bytes);
        return LivePlanAppendResult::Appended;
    } catch (...) {
        return LivePlanAppendResult::InternalFailure;
    }
}

std::deque<PlannedFile>::iterator LiveCopyPlan::find_pending(const std::uint64_t file_id) noexcept {
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
        std::uint64_t removed_weight = 0;
        bool removed_largest = false;
        for (const auto& file : pending_files_) {
            if (!selected.contains(file.id)) continue;
            removals.push_back({file.size, normalized_path_key(file.destination)});
            removed_bytes += file.size;
            removed_weight += item_resolution_weight(file.size);
            removed_largest = removed_largest || file.size == largest_file_bytes_;
        }
        if (removals.empty()) return 0;

        for (const auto& removal : removals) {
            reserved_destination_keys_.erase(removal.destination_key);
        }
        total_bytes_ -= std::min(total_bytes_, removed_bytes);
        total_files_ -= std::min<std::uint64_t>(
            total_files_, static_cast<std::uint64_t>(removals.size()));
        // Removing a Pending item is a plan edit, not a resolution.
        counters_.resolution_total -= std::min(counters_.resolution_total, removed_weight);
        for (const auto& file : pending_files_) {
            if (selected.contains(file.id)) drop_in_flight_locked(file.id);
        }
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
    pending_files_.pop_front();
    active_files_.push_back(file);
    attempt_bytes_.erase(file.id);
    // Keep the attempt number while this item is active. If this retry parks
    // again, park_active() must report the incremented attempt.
    return file;
}

void LiveCopyPlan::complete_active(const std::uint64_t file_id) noexcept {
    std::lock_guard lock(mutex_);
    auto it = find_active(file_id);
    if (it == active_files_.end()) return;
    const auto remaining_bytes = total_bytes_ > completed_bytes_ ? total_bytes_ - completed_bytes_ : 0;
    completed_bytes_ += std::min(it->size, remaining_bytes);
    if (completed_files_ < total_files_) ++completed_files_;
    // Legacy completion is the Succeeded fast path of the per-item contract.
    const auto weight = item_resolution_weight(it->size);
    std::uint64_t high_water = 0;
    if (const auto hw = high_water_.find(file_id); hw != high_water_.end()) {
        high_water = hw->second;
        high_water_.erase(hw);
    }
    counters_.resolution_weight += weight - std::min(weight, high_water);
    counters_.bytes_succeeded += it->size;
    ++outcomes_.succeeded;
    attempt_bytes_.erase(file_id);
    active_files_.erase(it);
}

void LiveCopyPlan::release_active(const std::uint64_t file_id) noexcept {
    std::lock_guard lock(mutex_);
    auto it = find_active(file_id);
    if (it == active_files_.end()) return;
    PlannedFile file = std::move(*it);
    active_files_.erase(it);
    pending_files_.push_front(std::move(file));
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
        // Legacy skip removes the item from the plan (pre-contract semantics).
        // It is retired when the executor migrates to resolve_active(Skipped).
        counters_.resolution_total -= std::min(
            counters_.resolution_total, item_resolution_weight(skipped_size));
        drop_in_flight_locked(file_id);
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

std::vector<LiveCopyPlan::ParkedFile>::iterator LiveCopyPlan::find_parked(
    const std::uint64_t file_id) noexcept {
    return std::find_if(parked_files_.begin(), parked_files_.end(), [file_id](const ParkedFile& parked) {
        return parked.file.id == file_id;
    });
}

void LiveCopyPlan::drop_in_flight_locked(const std::uint64_t file_id) noexcept {
    if (const auto hw = high_water_.find(file_id); hw != high_water_.end()) {
        counters_.resolution_weight -= std::min(counters_.resolution_weight, hw->second);
        high_water_.erase(hw);
    }
    attempt_bytes_.erase(file_id);
    attempt_counts_.erase(file_id);
}

// Preconditions: mutex_ held; `file` is still in its source container. May
// throw only before any state is changed (allocation of the retained result or
// of the destination key), so callers can return false with the plan intact.
bool LiveCopyPlan::resolve_locked(
    const PlannedFile& file,
    const ItemOutcome outcome,
    const std::int32_t hresult,
    const bool destination_preexisted) {
    const bool releases_destination =
        outcome == ItemOutcome::Skipped || outcome == ItemOutcome::Failed;
    std::wstring destination_key;
    if (releases_destination) destination_key = normalized_path_key(file.destination);
    if (outcome != ItemOutcome::Succeeded) {
        retained_results_.push_back(ItemResult{
            file.id, outcome, hresult, file.source, file.destination, destination_preexisted});
    }

    const auto weight = item_resolution_weight(file.size);
    std::uint64_t high_water = 0;
    if (const auto hw = high_water_.find(file.id); hw != high_water_.end()) {
        high_water = hw->second;
        high_water_.erase(hw);
    }
    counters_.resolution_weight += weight - std::min(weight, high_water);
    attempt_bytes_.erase(file.id);
    attempt_counts_.erase(file.id);
    if (is_successful_transfer(outcome)) counters_.bytes_succeeded += file.size;
    if (releases_destination) reserved_destination_keys_.erase(destination_key);

    switch (outcome) {
    case ItemOutcome::Succeeded: ++outcomes_.succeeded; break;
    case ItemOutcome::Skipped: ++outcomes_.skipped; break;
    case ItemOutcome::Failed: ++outcomes_.failed; break;
    case ItemOutcome::CopiedSourceRetained: ++outcomes_.copied_source_retained; break;
    }

    // Keep the legacy accessors coherent while both APIs coexist.
    if (is_successful_transfer(outcome)) {
        const auto remaining_bytes = total_bytes_ > completed_bytes_ ? total_bytes_ - completed_bytes_ : 0;
        completed_bytes_ += std::min(file.size, remaining_bytes);
        if (completed_files_ < total_files_) ++completed_files_;
    }
    return true;
}

bool LiveCopyPlan::park_active(
    const std::uint64_t file_id,
    const std::int32_t hresult,
    const bool destination_preexisted,
    const RecoveryAction recovery_action) noexcept {
    static_assert(is_valid_item_transition(ItemState::Active, ItemState::Parked));
    try {
        std::lock_guard lock(mutex_);
        auto it = find_active(file_id);
        if (it == active_files_.end()) return false;
        const auto attempt_it = attempt_counts_.find(file_id);
        const auto attempt_count = attempt_it == attempt_counts_.end() ? 1u : attempt_it->second;
        ParkedFile parked{*it, ItemIncident{
            it->id, hresult, it->source, it->destination, destination_preexisted,
            recovery_action, attempt_count}};
        parked_files_.push_back(std::move(parked));
        active_files_.erase(it);
        return true;
    } catch (...) {
        return false;
    }
}

bool LiveCopyPlan::unpark(const std::uint64_t file_id) noexcept {
    static_assert(is_valid_item_transition(ItemState::Parked, ItemState::Pending));
    try {
        std::lock_guard lock(mutex_);
        auto it = find_parked(file_id);
        if (it == parked_files_.end() ||
            it->incident.recovery_action != RecoveryAction::RetryTransfer) {
            return false;
        }
        pending_files_.push_front(it->file);
        attempt_counts_[file_id] = it->incident.attempt_count + 1;
        parked_files_.erase(it);
        // The retry is a new attempt; the high-water mark is kept so the
        // visible progress does not move backwards.
        attempt_bytes_.erase(file_id);
        return true;
    } catch (...) {
        return false;
    }
}

bool LiveCopyPlan::resolve_pending(
    const std::uint64_t file_id,
    const ItemOutcome outcome,
    const std::int32_t hresult,
    const bool destination_preexisted) noexcept {
    static_assert(is_valid_item_transition(ItemState::Pending, ItemState::Terminal));
    try {
        std::lock_guard lock(mutex_);
        auto it = find_pending(file_id);
        if (it == pending_files_.end()) return false;
        if (!resolve_locked(*it, outcome, hresult, destination_preexisted)) return false;
        pending_files_.erase(it);
        return true;
    } catch (...) {
        return false;
    }
}

bool LiveCopyPlan::resolve_active(
    const std::uint64_t file_id,
    const ItemOutcome outcome,
    const std::int32_t hresult,
    const bool destination_preexisted) noexcept {
    static_assert(is_valid_item_transition(ItemState::Active, ItemState::Terminal));
    try {
        std::lock_guard lock(mutex_);
        auto it = find_active(file_id);
        if (it == active_files_.end()) return false;
        if (!resolve_locked(*it, outcome, hresult, destination_preexisted)) return false;
        active_files_.erase(it);
        return true;
    } catch (...) {
        return false;
    }
}

bool LiveCopyPlan::resolve_parked(
    const std::uint64_t file_id,
    const ItemOutcome outcome,
    const std::int32_t hresult,
    const bool destination_preexisted) noexcept {
    static_assert(is_valid_item_transition(ItemState::Parked, ItemState::Terminal));
    try {
        std::lock_guard lock(mutex_);
        auto it = find_parked(file_id);
        if (it == parked_files_.end()) return false;
        if (!resolve_locked(it->file, outcome, hresult, destination_preexisted)) return false;
        parked_files_.erase(it);
        return true;
    } catch (...) {
        return false;
    }
}

std::size_t LiveCopyPlan::fail_pending_under(
    const std::filesystem::path& directory,
    const std::int32_t hresult) noexcept {
    try {
        auto prefix = normalized_path_key(directory);
        if (prefix.empty()) return 0;
        if (prefix.back() != L'\\' && prefix.back() != L'/') prefix.push_back(L'\\');
        const auto alternate = [&] {
            auto value = prefix;
            value.back() = value.back() == L'\\' ? L'/' : L'\\';
            return value;
        }();

        std::lock_guard lock(mutex_);
        std::size_t resolved = 0;
        for (auto it = pending_files_.begin(); it != pending_files_.end();) {
            const auto key = normalized_path_key(it->destination);
            if (!key.starts_with(prefix) && !key.starts_with(alternate)) {
                ++it;
                continue;
            }
            if (!resolve_locked(*it, ItemOutcome::Failed, hresult, false)) return resolved;
            it = pending_files_.erase(it);
            ++resolved;
        }
        return resolved;
    } catch (...) {
        return 0;
    }
}

void LiveCopyPlan::record_attempt_bytes(
    const std::uint64_t file_id,
    const std::uint64_t cumulative_attempt_bytes) noexcept {
    try {
        std::lock_guard lock(mutex_);
        auto it = find_active(file_id);
        if (it == active_files_.end()) return;

        auto& last = attempt_bytes_[file_id];
        auto& high_water = high_water_[file_id];
        // A smaller cumulative value means CopyFile2 restarted the attempt.
        const auto delta = cumulative_attempt_bytes >= last
            ? cumulative_attempt_bytes - last
            : cumulative_attempt_bytes;
        last = cumulative_attempt_bytes;
        counters_.bytes_written_physical += delta;

        const auto next = resolution_high_water(high_water, it->size, cumulative_attempt_bytes, false);
        counters_.resolution_weight += next - high_water;
        high_water = next;
    } catch (...) {
    }
}

LiveResolutionView LiveCopyPlan::resolution_view() const noexcept {
    std::lock_guard lock(mutex_);
    LiveResolutionView view{};
    view.counters = counters_;
    view.outcomes = outcomes_;
    view.pending_files = static_cast<std::uint64_t>(pending_files_.size());
    view.active_files = static_cast<std::uint64_t>(active_files_.size());
    view.parked_files = static_cast<std::uint64_t>(parked_files_.size());
    return view;
}

std::vector<ItemResult> LiveCopyPlan::retained_results() const {
    std::lock_guard lock(mutex_);
    return retained_results_;
}

std::vector<ItemIncident> LiveCopyPlan::parked_incidents() const {
    std::lock_guard lock(mutex_);
    std::vector<ItemIncident> incidents;
    incidents.reserve(parked_files_.size());
    for (const auto& parked : parked_files_) incidents.push_back(parked.incident);
    return incidents;
}

std::uint64_t LiveCopyPlan::unresolved_files() const noexcept {
    std::lock_guard lock(mutex_);
    return static_cast<std::uint64_t>(
        pending_files_.size() + active_files_.size() + parked_files_.size());
}

} // namespace velocitycopy
