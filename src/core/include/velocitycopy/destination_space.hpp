#pragma once

#include "velocitycopy/copy_job.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace velocitycopy {

struct SpaceShortage {
    std::uint64_t required_bytes{};
    std::uint64_t available_bytes{};
};

// Pre-flight free-space check for a planned transfer. Returns the shortage when
// the bytes still to be written exceed the space available to the caller on the
// destination volume. A Move whose sources all live on the destination volume
// is a rename and needs no space. Unknown volumes (the query fails) are not
// reported: the check only ever warns, the engine still reports a full disk.
[[nodiscard]] std::optional<SpaceShortage> find_space_shortage(
    const std::vector<std::filesystem::path>& source_roots,
    const std::filesystem::path& destination_root,
    FileOperation operation,
    std::uint64_t required_bytes) noexcept;

} // namespace velocitycopy
