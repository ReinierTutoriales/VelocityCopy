#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace velocitycopy {

enum class JobState {
    Pending,
    Running,
    Paused,
    Completed,
    Failed,
    Cancelled,
};

enum class DestinationLayout {
    PreserveSourceFolder,
    ContentsOnly,
};

struct CopyJob {
    std::uint64_t id{};
    std::vector<std::filesystem::path> sources;
    std::filesystem::path destination;
    DestinationLayout layout{DestinationLayout::PreserveSourceFolder};
    JobState state{JobState::Pending};
    std::wstring display_name;
};

} // namespace velocitycopy
