#include "velocitycopy/drop_flow.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

int wmain() {
    velocitycopy::DropFlowController flow;
    const std::vector<velocitycopy::DropItem> items{
        {fs::path(L"C:\\Library\\Novela\\capitulo1.mkv"), velocitycopy::DropItemKind::File},
    };

    flow.begin(items);
    if (flow.stage() != velocitycopy::DropFlowStage::Destination || flow.empty()) {
        return 1;
    }

    if (flow.choose_destination(fs::path(L"C:\\Library\\Novela")) != velocitycopy::DestinationValidation::InsideSource) {
        return 2;
    }

    if (flow.choose_destination(fs::path(L"D:\\Backup")) != velocitycopy::DestinationValidation::Valid) {
        return 3;
    }
    if (flow.stage() != velocitycopy::DropFlowStage::Layout || !flow.menu()) {
        return 4;
    }
    if (flow.menu()->preserve.destinations.empty() ||
        flow.menu()->preserve.destinations.front() != fs::path(L"D:\\Backup\\Novela\\capitulo1.mkv")) {
        return 5;
    }

    if (!flow.choose_layout(velocitycopy::DestinationLayout::ContentsOnly) ||
        flow.stage() != velocitycopy::DropFlowStage::Ready) {
        return 6;
    }

    const auto job = flow.make_job(77);
    if (!job || job->id != 77 || job->layout != velocitycopy::DestinationLayout::ContentsOnly ||
        job->destination != fs::path(L"D:\\Backup")) {
        return 7;
    }

    if (!flow.back() || flow.stage() != velocitycopy::DropFlowStage::Layout) {
        return 8;
    }
    if (!flow.back() || flow.stage() != velocitycopy::DropFlowStage::Destination) {
        return 9;
    }

    std::wcout << L"VelocityCopy drop flow test passed.\n";
    return 0;
}
