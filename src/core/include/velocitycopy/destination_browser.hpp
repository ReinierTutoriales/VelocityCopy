#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace velocitycopy {

struct DestinationFolderEntry {
    std::filesystem::path path;
    std::filesystem::path name;
};

struct DestinationFolderListing {
    bool available{};
    std::vector<DestinationFolderEntry> children;
};

struct DestinationCapacity {
    bool available{};
    std::uint64_t free_bytes{};
    std::uint64_t total_bytes{};
};

class DestinationBrowser final {
public:
    [[nodiscard]] DestinationFolderListing list_children(
        const std::filesystem::path& folder) const noexcept;

    [[nodiscard]] DestinationCapacity capacity(
        const std::filesystem::path& folder) const noexcept;

    [[nodiscard]] bool create_folder(
        const std::filesystem::path& parent,
        const std::filesystem::path& name,
        std::filesystem::path& created) const noexcept;
};

} // namespace velocitycopy
