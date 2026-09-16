#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/job_planner.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int wmain() {
    const auto base = fs::temp_directory_path() / L"VelocityCopyPlanEditTest";
    const auto source = base / L"Source";
    const auto destination = base / L"Destination";

    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(source, ec);
    if (ec) {
        return 1;
    }

    {
        std::ofstream(source / L"one.txt", std::ios::binary) << "1";
        std::ofstream(source / L"two.txt", std::ios::binary) << "22";
        std::ofstream(source / L"three.txt", std::ios::binary) << "333";
    }

    velocitycopy::CopyJob job{};
    job.id = 1;
    job.sources = {source};
    job.destination = destination;

    velocitycopy::JobPlanner planner;
    auto plan = planner.build(job);
    if (plan.files.size() != 3 || plan.total_bytes != 6) {
        return 2;
    }

    const auto find_id = [&](const wchar_t* name) -> std::uint64_t {
        for (const auto& file : plan.files) {
            if (file.source.filename() == name) {
                return file.id;
            }
        }
        return 0;
    };

    const auto one_id = find_id(L"one.txt");
    const auto two_id = find_id(L"two.txt");
    const auto three_id = find_id(L"three.txt");
    if (one_id == 0 || two_id == 0 || three_id == 0) {
        return 3;
    }

    if (!plan.move_file(three_id, 0) || plan.files.front().id != three_id) {
        return 4;
    }

    if (!plan.move_file_down(three_id) || plan.files[1].id != three_id) {
        return 5;
    }

    if (!plan.move_file_up(three_id) || plan.files.front().id != three_id) {
        return 6;
    }

    if (!plan.remove_file(two_id) || plan.files.size() != 2 || plan.total_bytes != 4) {
        return 7;
    }

    if (plan.move_file_up(three_id)) {
        return 8;
    }

    velocitycopy::JobExecutor executor;
    const auto result = executor.execute(plan);
    if (!result.success) {
        return 9;
    }

    const auto copied_root = destination / source.filename();
    if (!fs::is_regular_file(copied_root / L"one.txt") ||
        !fs::is_regular_file(copied_root / L"three.txt") ||
        fs::exists(copied_root / L"two.txt")) {
        return 10;
    }

    fs::remove_all(base, ec);
    std::wcout << L"VelocityCopy editable plan test passed.\n";
    return 0;
}
