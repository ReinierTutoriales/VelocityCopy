#include "velocitycopy/job_planner.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int wmain() {
    const auto base = fs::temp_directory_path() / L"VelocityCopyDropLayoutTest";
    const auto library = base / L"Library";
    const auto novel = library / L"Novela";
    const auto file = novel / L"capitulo1.mkv";
    const auto destination = base / L"Destination";

    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(novel, ec);
    if (ec) {
        return 1;
    }

    {
        std::ofstream out(file, std::ios::binary);
        out << "chapter";
    }

    velocitycopy::JobPlanner planner;

    velocitycopy::CopyJob keep_file{};
    keep_file.sources = {file};
    keep_file.destination = destination;
    keep_file.layout = velocitycopy::DestinationLayout::PreserveSourceFolder;

    const auto keep_file_plan = planner.build(keep_file);
    if (keep_file_plan.files.size() != 1 ||
        keep_file_plan.files[0].destination != destination / L"Novela" / L"capitulo1.mkv") {
        return 2;
    }

    velocitycopy::CopyJob loose_file = keep_file;
    loose_file.layout = velocitycopy::DestinationLayout::ContentsOnly;
    const auto loose_file_plan = planner.build(loose_file);
    if (loose_file_plan.files.size() != 1 ||
        loose_file_plan.files[0].destination != destination / L"capitulo1.mkv") {
        return 3;
    }

    velocitycopy::CopyJob keep_folder{};
    keep_folder.sources = {novel};
    keep_folder.destination = destination;
    keep_folder.layout = velocitycopy::DestinationLayout::PreserveSourceFolder;

    const auto keep_folder_plan = planner.build(keep_folder);
    if (keep_folder_plan.files.size() != 1 ||
        keep_folder_plan.files[0].destination != destination / L"Novela" / L"capitulo1.mkv") {
        return 4;
    }

    velocitycopy::CopyJob loose_folder = keep_folder;
    loose_folder.layout = velocitycopy::DestinationLayout::ContentsOnly;
    const auto loose_folder_plan = planner.build(loose_folder);
    if (loose_folder_plan.files.size() != 1 ||
        loose_folder_plan.files[0].destination != destination / L"capitulo1.mkv") {
        return 5;
    }

    fs::remove_all(base, ec);
    std::wcout << L"VelocityCopy drop layout test passed.\n";
    return 0;
}
