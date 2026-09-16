#pragma once

#include "velocitycopy/copy_job.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace velocitycopy {

struct PlannedDirectory {
    std::filesystem::path destination;
};

struct PlannedFile {
    std::uint64_t id{};
    std::filesystem::path source;
    std::filesystem::path destination;
    std::uint64_t size{};
};

struct CopyPlan {
    std::vector<PlannedDirectory> directories;
    std::vector<PlannedFile> files;
    std::uint64_t total_bytes{};

    [[nodiscard]] bool move_file(std::uint64_t file_id, std::size_t new_index) noexcept;
    [[nodiscard]] bool move_file_up(std::uint64_t file_id) noexcept;
    [[nodiscard]] bool move_file_down(std::uint64_t file_id) noexcept;
    [[nodiscard]] bool remove_file(std::uint64_t file_id) noexcept;
};

class JobPlanner final {
public:
    [[nodiscard]] CopyPlan build(const CopyJob& job) const;
};

} // namespace velocitycopy
