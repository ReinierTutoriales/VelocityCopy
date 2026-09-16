#pragma once

#include "velocitycopy/copy_job.hpp"
#include "velocitycopy/job_planner.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace velocitycopy {

struct QueueArchive {
    std::optional<CopyPlan> current_plan;
    std::vector<CopyJob> current_append_jobs;
    std::vector<CopyJob> queued_jobs;
};

class QueueArchiveStore final {
public:
    [[nodiscard]] bool save(
        const std::filesystem::path& path,
        const QueueArchive& archive) const noexcept;

    [[nodiscard]] std::optional<QueueArchive> load(
        const std::filesystem::path& path) const noexcept;
};

} // namespace velocitycopy
