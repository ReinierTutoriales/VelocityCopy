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
    if (ec) return 1;

    { std::ofstream out(file, std::ios::binary); out << "chapter"; }

    velocitycopy::JobPlanner planner;

    velocitycopy::CopyJob single_file{};
    single_file.sources = {file};
    single_file.destination = destination;
    single_file.layout = velocitycopy::DestinationLayout::PreserveSourceFolder;
    auto plan = planner.build(single_file);
    if (plan.files.size() != 1 || plan.files[0].destination != destination / L"capitulo1.mkv") return 2;

    velocitycopy::CopyJob single_folder{};
    single_folder.sources = {novel};
    single_folder.destination = destination;
    single_folder.layout = velocitycopy::DestinationLayout::PreserveSourceFolder;
    plan = planner.build(single_folder);
    if (plan.files.size() != 1 || plan.files[0].destination != destination / L"Novela" / L"capitulo1.mkv") return 3;

    const auto photos = library / L"Fotos";
    const auto photo = photos / L"playa.jpg";
    const auto notes = library / L"notas.txt";
    fs::create_directories(photos, ec);
    if (ec) return 4;
    { std::ofstream out(photo, std::ios::binary); out << "photo"; }
    { std::ofstream out(notes, std::ios::binary); out << "notes"; }

    velocitycopy::CopyJob mixed{};
    mixed.sources = {file, photos, notes};
    mixed.destination = destination;
    mixed.layout = velocitycopy::DestinationLayout::PreserveSourceFolder;
    mixed.operation = velocitycopy::FileOperation::Move;
    const auto mixed_keep = planner.build(mixed);
    if (mixed_keep.files.size() != 3 || mixed_keep.operation != velocitycopy::FileOperation::Move) return 5;

    const auto has_destination = [](const auto& candidate, const fs::path& expected) {
        return std::ranges::any_of(candidate.files, [&](const auto& entry) { return entry.destination == expected; });
    };
    if (!has_destination(mixed_keep, destination / L"Novela" / L"capitulo1.mkv") ||
        !has_destination(mixed_keep, destination / L"Fotos" / L"playa.jpg") ||
        !has_destination(mixed_keep, destination / L"Library" / L"notas.txt")) return 6;

    fs::remove_all(base, ec);
    std::wcout << L"VelocityCopy drop layout test passed.\n";
    return 0;
}
