#include "velocitycopy/drop_menu_model.hpp"

#include <array>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int wmain() {
    velocitycopy::DropMenuBuilder builder;

    const std::array<velocitycopy::DropItem, 5> items{{
        {fs::path(LR"(D:\Library\Novela\capitulo1.mkv)"), velocitycopy::DropItemKind::File},
        {fs::path(LR"(D:\Library\Novela\capitulo2.mkv)"), velocitycopy::DropItemKind::File},
        {fs::path(LR"(D:\Fotos\Viaje)"), velocitycopy::DropItemKind::Directory},
        {fs::path(LR"(D:\Music\song.flac)"), velocitycopy::DropItemKind::File},
        {fs::path(LR"(D:\Work\report.pdf)"), velocitycopy::DropItemKind::File},
    }};

    const fs::path destination = LR"(E:\Backup)";
    const auto model = builder.build(items, destination);

    if (model.item_count != 5 ||
        model.preserve.destinations.size() != 3 ||
        model.direct.destinations.size() != 3 ||
        model.preserve.hidden_items != 2 ||
        model.direct.hidden_items != 2) {
        return 1;
    }

    if (model.preserve.destinations[0] != destination / L"Novela" / L"capitulo1.mkv") {
        return 2;
    }
    if (model.direct.destinations[0] != destination / L"capitulo1.mkv") {
        return 3;
    }
    if (model.preserve.destinations[2] != destination / L"Viaje") {
        return 4;
    }
    if (model.direct.destinations[2] != destination) {
        return 5;
    }

    const auto job = builder.make_job(
        items,
        destination,
        velocitycopy::DestinationLayout::PreserveSourceFolder,
        42);

    if (job.id != 42 ||
        job.destination != destination ||
        job.layout != velocitycopy::DestinationLayout::PreserveSourceFolder ||
        job.sources.size() != items.size() ||
        job.sources[0] != items[0].source) {
        return 6;
    }

    std::wcout << L"VelocityCopy drop menu model test passed.\n";
    return 0;
}
