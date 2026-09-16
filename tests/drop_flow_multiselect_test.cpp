#include "velocitycopy/drop_flow.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

int wmain() {
    velocitycopy::DropFlowController flow;
    const std::vector<velocitycopy::DropItem> items{
        {fs::path(L"C:\\Media\\Novela\\capitulo1.mkv"), velocitycopy::DropItemKind::File},
        {fs::path(L"C:\\Media\\Novela\\capitulo2.mkv"), velocitycopy::DropItemKind::File},
        {fs::path(L"C:\\Fotos\\Viaje"), velocitycopy::DropItemKind::Directory},
        {fs::path(L"D:\\Trabajo\\notas.txt"), velocitycopy::DropItemKind::File},
    };

    flow.begin(items);
    if (flow.items().size() != items.size()) {
        return 1;
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (flow.items()[i].source != items[i].source || flow.items()[i].kind != items[i].kind) {
            return 2;
        }
    }

    if (flow.choose_destination(fs::path(L"E:\\Backup")) != velocitycopy::DestinationValidation::Valid) {
        return 3;
    }
    if (!flow.menu() || flow.menu()->item_count != items.size()) {
        return 4;
    }
    if (flow.menu()->preserve.destinations.size() > velocitycopy::DropMenuBuilder::max_preview_items ||
        flow.menu()->direct.destinations.size() > velocitycopy::DropMenuBuilder::max_preview_items) {
        return 5;
    }
    if (flow.menu()->preserve.hidden_items != 1 || flow.menu()->direct.hidden_items != 1) {
        return 6;
    }

    if (!flow.choose_layout(velocitycopy::DestinationLayout::PreserveSourceFolder)) {
        return 7;
    }
    const auto job = flow.make_job(88);
    if (!job || job->sources.size() != items.size()) {
        return 8;
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (job->sources[i] != items[i].source) {
            return 9;
        }
    }

    std::wcout << L"VelocityCopy drop flow multiselect test passed.\n";
    return 0;
}
