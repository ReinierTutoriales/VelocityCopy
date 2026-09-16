#include "velocitycopy/live_copy_plan.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace velocitycopy {

CopyPlan LiveCopyPlan::export_remaining_plan() const {
    std::lock_guard lock(mutex_);

    CopyPlan plan{};
    plan.directories = directories_;
    plan.source_roots = source_roots_;
    plan.destination_root = destination_root_;
    plan.files.reserve(active_files_.size() + pending_files_.size());

    std::uint64_t next_id = 1;
    auto append_file = [&](const PlannedFile& source) {
        PlannedFile file = source;
        file.id = next_id++;
        if (std::numeric_limits<std::uint64_t>::max() - plan.total_bytes < file.size) {
            throw std::overflow_error("remaining plan byte total overflow");
        }
        plan.total_bytes += file.size;
        plan.largest_file_bytes = (std::max)(plan.largest_file_bytes, file.size);
        plan.files.push_back(std::move(file));
    };

    // Active files are restarted from source when an archive is loaded. They
    // are placed first so partially-started work is not silently postponed
    // behind a potentially very large pending queue.
    for (const auto& file : active_files_) {
        append_file(file);
    }
    for (const auto& file : pending_files_) {
        append_file(file);
    }
    return plan;
}

} // namespace velocitycopy
