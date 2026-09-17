#pragma once

#include "velocitycopy/copy_job.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace velocitycopy {

enum class DropItemKind : std::uint8_t {
    File,
    Directory,
};

struct DropItem {
    std::filesystem::path source;
    DropItemKind kind{DropItemKind::File};
};

struct DropChoicePreview {
    DestinationLayout layout{DestinationLayout::PreserveSourceFolder};
    std::vector<std::filesystem::path> destinations;
    std::size_t hidden_items{};
};

struct DropMenuModel {
    std::filesystem::path destination;
    std::size_t item_count{};
    DropChoicePreview preserve;
    DropChoicePreview direct;
};

class DropMenuBuilder final {
public:
    static constexpr std::size_t max_preview_items = 3;

    [[nodiscard]] DropMenuModel build(
        std::span<const DropItem> items,
        const std::filesystem::path& destination) const;

    [[nodiscard]] CopyJob make_job(
        std::span<const DropItem> items,
        const std::filesystem::path& destination,
        DestinationLayout layout,
        std::uint64_t job_id = 0,
        FileOperation operation = FileOperation::Copy) const;

private:
    [[nodiscard]] static std::filesystem::path preview_destination(
        const DropItem& item,
        const std::filesystem::path& destination,
        DestinationLayout layout);
};

} // namespace velocitycopy
