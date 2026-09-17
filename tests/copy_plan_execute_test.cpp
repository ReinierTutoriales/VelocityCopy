#include "velocitycopy/job_executor.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

int wmain() {
    const auto base = fs::temp_directory_path() / L"VelocityCopyPlanExecuteTest";
    const auto source_root = base / L"source";
    const auto destination_root = base / L"destination";
    const auto source = source_root / L"payload.txt";
    const auto destination = destination_root / L"nested" / L"payload.txt";

    std::error_code ec;
    fs::remove_all(base, ec);
    ec.clear();
    fs::create_directories(source_root, ec);
    if (ec) return 1;

    {
        std::ofstream stream(source, std::ios::binary | std::ios::trunc);
        stream << "copy-plan-adapter";
        if (!stream) {
            fs::remove_all(base, ec);
            return 2;
        }
    }

    velocitycopy::CopyPlan plan{};
    plan.directories.push_back({destination_root / L"nested"});
    plan.files.push_back({1, source, destination, fs::file_size(source, ec)});
    if (ec) {
        fs::remove_all(base, ec);
        return 3;
    }
    plan.source_roots.push_back(source_root);
    plan.destination_root = destination_root;
    plan.total_bytes = plan.files.front().size;
    plan.largest_file_bytes = plan.files.front().size;

    velocitycopy::JobExecutor executor;
    const auto result = executor.execute(plan);
    if (!result.success || result.cancelled || result.stopped || !fs::is_regular_file(destination)) {
        fs::remove_all(base, ec);
        return 4;
    }

    std::ifstream copied(destination, std::ios::binary);
    const std::string contents{
        std::istreambuf_iterator<char>(copied),
        std::istreambuf_iterator<char>()};
    if (contents != "copy-plan-adapter") {
        fs::remove_all(base, ec);
        return 5;
    }

    fs::remove_all(base, ec);
    return 0;
}
