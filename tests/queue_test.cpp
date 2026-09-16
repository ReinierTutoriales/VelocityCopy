#include "velocitycopy/job_queue.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int wmain() {
    const auto base = fs::temp_directory_path() / L"VelocityCopyQueueTest";
    const auto source_a = base / L"SourceA";
    const auto source_b = base / L"SourceB";
    const auto destination_a = base / L"DestinationA";
    const auto destination_b = base / L"DestinationB";

    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(source_a, ec);
    fs::create_directories(source_b, ec);
    if (ec) {
        std::wcerr << L"Failed to prepare queue test.\n";
        return 1;
    }

    {
        std::ofstream file(source_a / L"a.txt", std::ios::binary);
        file << "A";
    }
    {
        std::ofstream file(source_b / L"b.txt", std::ios::binary);
        file << "B";
    }

    velocitycopy::CopyJob job_a{};
    job_a.id = 101;
    job_a.sources = {source_a};
    job_a.destination = destination_a;

    velocitycopy::CopyJob job_b{};
    job_b.id = 202;
    job_b.sources = {source_b};
    job_b.destination = destination_b;

    velocitycopy::JobQueue queue;
    queue.enqueue(job_a);
    queue.enqueue(job_b);

    if (queue.size() != 2 || queue.jobs()[0].state != velocitycopy::JobState::Pending) {
        return 2;
    }

    if (!queue.move_pending(202, 0) || queue.jobs()[0].id != 202) {
        return 3;
    }

    std::size_t callbacks = 0;
    const auto processed = queue.execute_all([&](const velocitycopy::QueueProgress&) {
        ++callbacks;
        return velocitycopy::JobDecision::Continue;
    });

    if (processed != 2 || callbacks == 0) {
        return 4;
    }

    if (queue.jobs()[0].state != velocitycopy::JobState::Completed ||
        queue.jobs()[1].state != velocitycopy::JobState::Completed) {
        return 5;
    }

    if (!fs::is_regular_file(destination_b / source_b.filename() / L"b.txt") ||
        !fs::is_regular_file(destination_a / source_a.filename() / L"a.txt")) {
        return 6;
    }

    fs::remove_all(base, ec);
    std::wcout << L"VelocityCopy queue test passed.\n";
    return 0;
}
