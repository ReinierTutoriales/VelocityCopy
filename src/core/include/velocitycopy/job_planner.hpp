#pragma once

#include "velocitycopy/copy_job.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace velocitycopy {

struct PlannedDirectory {
    std::filesystem::path destination;
};

struct PlannedFile {
    std::filesystem::path source;
    std::filesystem::path destination;
    std::uint64_t size{};
};

struct CopyPlan {
    std::vector<PlannedDirectory> directories;
    std::vector<PlannedFile> files;
    std::uint64_t total_bytes{};
};

class JobPlanner final {
public:
    [[nodiscard]] CopyPlan build(const CopyJob& job) const;
};

} // namespace velocitycopy
