#include "velocitycopy/job_executor.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int wmain() {
    const auto base = fs::temp_directory_path() / L"VelocityCopySmokeTest";
    const auto source = base / L"SourceFolder";
    const auto destination = base / L"Destination";

    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(source / L"Nested" / L"Empty", ec);
    if (ec) {
        std::wcerr << L"Failed to prepare test directories.\n";
        return 1;
    }

    {
        std::ofstream file(source / L"Nested" / L"hello.txt", std::ios::binary);
        file << "VelocityCopy smoke test";
    }

    velocitycopy::CopyJob job{};
    job.id = 1;
    job.sources = {source};
    job.destination = destination;
    job.layout = velocitycopy::DestinationLayout::PreserveSourceFolder;

    velocitycopy::JobExecutor executor;
    const auto result = executor.execute(job);
    if (!result.success) {
        std::wcerr << L"Copy job failed.\n";
        fs::remove_all(base, ec);
        return 2;
    }

    const auto copied_root = destination / source.filename();
    const auto copied_file = copied_root / L"Nested" / L"hello.txt";
    const auto copied_empty = copied_root / L"Nested" / L"Empty";

    if (!fs::is_regular_file(copied_file) || !fs::is_directory(copied_empty)) {
        std::wcerr << L"Copied structure is incomplete.\n";
        fs::remove_all(base, ec);
        return 3;
    }

    std::ifstream file(copied_file, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (content != "VelocityCopy smoke test") {
        std::wcerr << L"Copied content does not match source.\n";
        fs::remove_all(base, ec);
        return 4;
    }

    fs::remove_all(base, ec);
    std::wcout << L"VelocityCopy smoke test passed.\n";
    return 0;
}
