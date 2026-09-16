#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace velocitycopy {

struct DestinationFolderEntry {
    std::filesystem::path path;
    std::filesystem::path name;
};

struct DestinationCapacity {
    bool available{};
    std::uint64_t free_bytes{};
    std::uint64_t total_bytes{};
};

class DestinationBrowser final {
public:
    [[nodiscard]] std::vector<DestinationFolderEntry> list_children(
        const std::filesystem::path& folder) const noexcept;

    [[nodiscard]] DestinationCapacity capacity(
        const std::filesystem::path& folder) const noexcept;

    [[nodiscard]] bool create_folder(
        const std::filesystem::path& parent,
        const std::filesystem::path& name,
        std::filesystem::path& created) const noexcept;
};

} // namespace velocitycopy
