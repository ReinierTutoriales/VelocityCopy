#include "velocitycopy/job_planner.hpp"

#include <algorithm>
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
    if (!has_destination(mixed_keep, destination / L"capitulo1.mkv") ||
        !has_destination(mixed_keep, destination / L"Fotos" / L"playa.jpg") ||
        !has_destination(mixed_keep, destination / L"notas.txt")) return 6;

    // Several loose files selected in one folder land directly in the
    // destination, like Explorer; the source folder is never recreated.
    const auto chapter2 = novel / L"capitulo2.mkv";
    { std::ofstream out(chapter2, std::ios::binary); out << "chapter2"; }
    velocitycopy::CopyJob loose_files{};
    loose_files.sources = {file, chapter2};
    loose_files.destination = destination;
    loose_files.layout = velocitycopy::DestinationLayout::PreserveSourceFolder;
    const auto loose_plan = planner.build(loose_files);
    if (loose_plan.files.size() != 2 ||
        !has_destination(loose_plan, destination / L"capitulo1.mkv") ||
        !has_destination(loose_plan, destination / L"capitulo2.mkv") ||
        !loose_plan.directories.empty()) return 7;

    // Only loose files whose names collide are disambiguated by parent.
    const auto other_notes = photos / L"notas.txt";
    { std::ofstream out(other_notes, std::ios::binary); out << "other"; }
    velocitycopy::CopyJob colliding{};
    colliding.sources = {notes, other_notes, file};
    colliding.destination = destination;
    colliding.layout = velocitycopy::DestinationLayout::PreserveSourceFolder;
    const auto colliding_plan = planner.build(colliding);
    if (colliding_plan.files.size() != 3 ||
        !has_destination(colliding_plan, destination / L"Library" / L"notas.txt") ||
        !has_destination(colliding_plan, destination / L"Fotos" / L"notas.txt") ||
        !has_destination(colliding_plan, destination / L"capitulo1.mkv")) return 8;

    fs::remove_all(base, ec);
    std::wcout << L"VelocityCopy drop layout test passed.\n";
    return 0;
}
