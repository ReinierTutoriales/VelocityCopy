#include "velocitycopy/drop_menu_model.hpp"

#include <algorithm>
#include <utility>

namespace velocitycopy {

std::filesystem::path DropMenuBuilder::preview_destination(
    const DropItem& item,
    const std::filesystem::path& destination,
    const DestinationLayout layout) {
    if (item.kind == DropItemKind::Directory) {
        return layout == DestinationLayout::PreserveSourceFolder
            ? destination / item.source.filename()
            : destination;
    }

    if (layout == DestinationLayout::PreserveSourceFolder) {
        const auto parent_name = item.source.parent_path().filename();
        if (!parent_name.empty()) {
            return destination / parent_name / item.source.filename();
        }
    }

    return destination / item.source.filename();
}

DropMenuModel DropMenuBuilder::build(
    const std::span<const DropItem> items,
    const std::filesystem::path& destination) const {
    DropMenuModel model{};
    model.destination = destination;
    model.item_count = items.size();
    model.preserve.layout = DestinationLayout::PreserveSourceFolder;
    model.direct.layout = DestinationLayout::ContentsOnly;

    const auto preview_count = std::min(items.size(), max_preview_items);
    model.preserve.destinations.reserve(preview_count);
    model.direct.destinations.reserve(preview_count);

    for (std::size_t index = 0; index < preview_count; ++index) {
        model.preserve.destinations.push_back(preview_destination(
            items[index], destination, DestinationLayout::PreserveSourceFolder));
        model.direct.destinations.push_back(preview_destination(
            items[index], destination, DestinationLayout::ContentsOnly));
    }

    model.preserve.hidden_items = items.size() - preview_count;
    model.direct.hidden_items = model.preserve.hidden_items;
    return model;
}

CopyJob DropMenuBuilder::make_job(
    const std::span<const DropItem> items,
    const std::filesystem::path& destination,
    const DestinationLayout layout,
    const std::uint64_t job_id) const {
    CopyJob job{};
    job.id = job_id;
    job.destination = destination;
    job.layout = layout;
    job.display_name = L"VelocityCopy transfer";
    job.sources.reserve(items.size());

    for (const auto& item : items) {
        if (!item.source.empty()) {
            job.sources.push_back(item.source);
        }
    }

    return job;
}

} // namespace velocitycopy
