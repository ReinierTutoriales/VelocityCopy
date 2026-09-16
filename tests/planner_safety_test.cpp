#include "velocitycopy/job_planner.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int wmain() {
    const auto base = fs::temp_directory_path() / L"VelocityCopyPlannerSafetyTest";
    const auto source = base / L"Source";
    const auto file = source / L"a.txt";
    const auto invalid_destination = source / L"NestedDestination";

    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(source, ec);
    if (ec) {
        return 1;
    }
    {
        std::ofstream out(file);
        out << "test";
    }

    velocitycopy::CopyJob job{};
    job.sources = {source};
    job.destination = invalid_destination;
    job.layout = velocitycopy::DestinationLayout::PreserveSourceFolder;

    velocitycopy::JobPlanner planner;
    bool rejected = false;
    try {
        (void)planner.build(job);
    } catch (const fs::filesystem_error&) {
        rejected = true;
    }

    fs::remove_all(base, ec);
    if (!rejected) {
        return 2;
    }

    std::wcout << L"VelocityCopy planner safety test passed.\n";
    return 0;
}
