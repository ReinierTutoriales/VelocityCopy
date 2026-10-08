#include "velocitycopy/job_planner.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stop_token>
#include <system_error>

namespace fs = std::filesystem;

namespace {

void write_text(const fs::path& path, const char* text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out << text;
}

bool rejected_by_planner(const velocitycopy::CopyJob& job) {
    velocitycopy::JobPlanner planner;
    try {
        (void)planner.build(job);
    } catch (const fs::filesystem_error&) {
        return true;
    }
    return false;
}

bool cancelled_by_planner(const velocitycopy::CopyJob& job) {
    velocitycopy::JobPlanner planner;
    std::stop_source source;
    source.request_stop();
    try {
        (void)planner.build(job, source.get_token());
    } catch (const std::system_error& error) {
        return error.code() == std::make_error_code(std::errc::operation_canceled);
    }
    return false;
}

} // namespace

int wmain() {
    using velocitycopy::CopyJob;
    using velocitycopy::DestinationLayout;

    const auto base = fs::temp_directory_path() / L"VelocityCopyPlannerSafetyTest";
    std::error_code ec;
    fs::remove_all(base, ec);

    const auto source = base / L"Source";
    const auto file = source / L"a.txt";
    write_text(file, "test");

    CopyJob nested_destination{};
    nested_destination.sources = {source};
    nested_destination.destination = source / L"NestedDestination";
    nested_destination.layout = DestinationLayout::PreserveSourceFolder;
    if (!rejected_by_planner(nested_destination)) {
        fs::remove_all(base, ec);
        return 1;
    }

    const auto safe_destination = base / L"Destination";
    fs::create_directories(safe_destination, ec);
    if (ec) {
        fs::remove_all(base, ec);
        return 2;
    }

    CopyJob duplicate_source{};
    duplicate_source.sources = {file, file};
    duplicate_source.destination = safe_destination;
    duplicate_source.layout = DestinationLayout::ContentsOnly;
    if (!rejected_by_planner(duplicate_source)) {
        fs::remove_all(base, ec);
        return 3;
    }

    const auto left = base / L"Left" / L"same.txt";
    const auto right = base / L"Right" / L"same.txt";
    write_text(left, "left");
    write_text(right, "right");

    CopyJob colliding_files{};
    colliding_files.sources = {left, right};
    colliding_files.destination = safe_destination;
    colliding_files.layout = DestinationLayout::ContentsOnly;
    if (!rejected_by_planner(colliding_files)) {
        fs::remove_all(base, ec);
        return 4;
    }

    const auto first_shared = base / L"One" / L"Shared";
    const auto second_shared = base / L"Two" / L"Shared";
    write_text(first_shared / L"one.txt", "one");
    write_text(second_shared / L"two.txt", "two");

    CopyJob colliding_roots{};
    colliding_roots.sources = {first_shared, second_shared};
    colliding_roots.destination = safe_destination;
    colliding_roots.layout = DestinationLayout::PreserveSourceFolder;
    if (!rejected_by_planner(colliding_roots)) {
        fs::remove_all(base, ec);
        return 5;
    }

    CopyJob cancellable{};
    cancellable.sources = {source};
    cancellable.destination = safe_destination;
    cancellable.layout = DestinationLayout::PreserveSourceFolder;
    if (!cancelled_by_planner(cancellable)) {
        fs::remove_all(base, ec);
        return 6;
    }

    // A junction inside a selected folder is reported as one failed entry and
    // never traversed; the rest of the folder is still planned.
    const auto tree = base / L"Tree";
    const auto outside = base / L"Outside";
    write_text(tree / L"keep.txt", "keep");
    write_text(outside / L"secret.txt", "secret");
    const auto junction = tree / L"link";
    const auto command = L"cmd /c mklink /J \"" + junction.wstring() + L"\" \"" + outside.wstring() + L"\" >nul";
    if (_wsystem(command.c_str()) != 0) {
        fs::remove_all(base, ec);
        return 7;
    }
    CopyJob with_junction{};
    with_junction.sources = {tree};
    with_junction.destination = safe_destination;
    with_junction.layout = DestinationLayout::PreserveSourceFolder;
    try {
        const auto plan = velocitycopy::JobPlanner{}.build(with_junction);
        const bool traversed = std::ranges::any_of(plan.files, [&](const auto& file) {
            return file.source.filename() == L"secret.txt";
        });
        if (plan.files.size() != 1 || plan.failures.size() != 1 || traversed ||
            plan.failures.front().source != junction) {
            fs::remove(junction, ec);
            fs::remove_all(base, ec);
            return 8;
        }
    } catch (...) {
        fs::remove(junction, ec);
        fs::remove_all(base, ec);
        return 9;
    }
    fs::remove(junction, ec);

    fs::remove_all(base, ec);
    std::wcout << L"VelocityCopy planner safety test passed.\n";
    return 0;
}
