#include "velocitycopy/job_executor.hpp"

#include <algorithm>
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

    const auto move_source_root = base / L"move-source";
    const auto move_destination_root = base / L"move-destination";
    const auto move_source = move_source_root / L"payload.txt";
    const auto move_destination = move_destination_root / L"payload.txt";

    fs::create_directories(move_source_root, ec);
    if (ec) {
        fs::remove_all(base, ec);
        return 6;
    }
    {
        std::ofstream stream(move_source, std::ios::binary | std::ios::trunc);
        stream << "move-plan-adapter";
        if (!stream) {
            fs::remove_all(base, ec);
            return 7;
        }
    }

    velocitycopy::CopyPlan move_plan{};
    move_plan.operation = velocitycopy::FileOperation::Move;
    move_plan.directories.push_back({move_destination_root});
    move_plan.files.push_back({1, move_source, move_destination, fs::file_size(move_source, ec)});
    if (ec) {
        fs::remove_all(base, ec);
        return 8;
    }
    move_plan.source_roots.push_back(move_source_root);
    move_plan.destination_root = move_destination_root;
    move_plan.total_bytes = move_plan.files.front().size;
    move_plan.largest_file_bytes = move_plan.files.front().size;

    const auto move_result = executor.execute(move_plan);
    if (!move_result.success || move_result.cancelled || move_result.stopped ||
        !fs::is_regular_file(move_destination) || fs::exists(move_source) ||
        fs::exists(move_source_root)) {
        fs::remove_all(base, ec);
        return 9;
    }

    std::ifstream moved(move_destination, std::ios::binary);
    const std::string moved_contents{
        std::istreambuf_iterator<char>(moved),
        std::istreambuf_iterator<char>()};
    if (moved_contents != "move-plan-adapter") {
        fs::remove_all(base, ec);
        return 10;
    }

    // Skip in a move session must never delete the skipped source.
    const auto skip_source_root = base / L"move-skip-source";
    const auto skip_destination_root = base / L"move-skip-destination";
    fs::create_directories(skip_source_root, ec);
    if (ec) {
        fs::remove_all(base, ec);
        return 11;
    }
    const auto skip_a = skip_source_root / L"a.txt";
    const auto skip_b = skip_source_root / L"b.txt";
    {
        std::ofstream a(skip_a, std::ios::binary | std::ios::trunc);
        std::ofstream b(skip_b, std::ios::binary | std::ios::trunc);
        a << "keep-me";
        b << "move-me";
        if (!a || !b) {
            fs::remove_all(base, ec);
            return 12;
        }
    }

    velocitycopy::CopyPlan skip_plan{};
    skip_plan.operation = velocitycopy::FileOperation::Move;
    skip_plan.source_roots.push_back(skip_source_root);
    skip_plan.destination_root = skip_destination_root;
    skip_plan.directories.push_back({skip_destination_root});
    skip_plan.files.push_back({1, skip_a, skip_destination_root / L"a.txt", fs::file_size(skip_a, ec)});
    skip_plan.files.push_back({2, skip_b, skip_destination_root / L"b.txt", fs::file_size(skip_b, ec)});
    if (ec) {
        fs::remove_all(base, ec);
        return 13;
    }
    skip_plan.total_bytes = skip_plan.files[0].size + skip_plan.files[1].size;
    skip_plan.largest_file_bytes = (std::max)(skip_plan.files[0].size, skip_plan.files[1].size);

    velocitycopy::LiveCopyPlan skip_live(std::move(skip_plan));
    velocitycopy::ExecutionControl skip_control;
    skip_control.request_skip(1);
    const auto skip_result = executor.execute(
        skip_live,
        skip_control,
        velocitycopy::JobExecutionOptions{1});
    if (!skip_result.success ||
        !fs::exists(skip_a) ||
        fs::exists(skip_b) ||
        fs::exists(skip_destination_root / L"a.txt") ||
        !fs::exists(skip_destination_root / L"b.txt")) {
        fs::remove_all(base, ec);
        return 14;
    }

    // Cancellation may leave a partial move, but must keep every file that was
    // not successfully completed.
    const auto cancel_source_root = base / L"move-cancel-source";
    const auto cancel_destination_root = base / L"move-cancel-destination";
    fs::create_directories(cancel_source_root, ec);
    if (ec) {
        fs::remove_all(base, ec);
        return 15;
    }
    const auto cancel_a = cancel_source_root / L"a.txt";
    const auto cancel_b = cancel_source_root / L"b.txt";
    {
        std::ofstream a(cancel_a, std::ios::binary | std::ios::trunc);
        std::ofstream b(cancel_b, std::ios::binary | std::ios::trunc);
        a << "first";
        b << "second";
        if (!a || !b) {
            fs::remove_all(base, ec);
            return 16;
        }
    }

    velocitycopy::CopyPlan cancel_plan{};
    cancel_plan.operation = velocitycopy::FileOperation::Move;
    cancel_plan.source_roots.push_back(cancel_source_root);
    cancel_plan.destination_root = cancel_destination_root;
    cancel_plan.directories.push_back({cancel_destination_root});
    cancel_plan.files.push_back({1, cancel_a, cancel_destination_root / L"a.txt", fs::file_size(cancel_a, ec)});
    cancel_plan.files.push_back({2, cancel_b, cancel_destination_root / L"b.txt", fs::file_size(cancel_b, ec)});
    if (ec) {
        fs::remove_all(base, ec);
        return 17;
    }
    cancel_plan.total_bytes = cancel_plan.files[0].size + cancel_plan.files[1].size;
    cancel_plan.largest_file_bytes = (std::max)(cancel_plan.files[0].size, cancel_plan.files[1].size);

    velocitycopy::LiveCopyPlan cancel_live(std::move(cancel_plan));
    velocitycopy::ExecutionControl cancel_control;
    bool requested_cancel = false;
    const auto cancel_result = executor.execute(
        cancel_live,
        cancel_control,
        velocitycopy::JobExecutionOptions{1},
        [&](const velocitycopy::JobProgress& progress) {
            if (!requested_cancel && progress.completed_files >= 1) {
                requested_cancel = true;
                return velocitycopy::JobDecision::Cancel;
            }
            return velocitycopy::JobDecision::Continue;
        });
    if (!cancel_result.cancelled || !requested_cancel ||
        fs::exists(cancel_a) ||
        !fs::exists(cancel_b) ||
        !fs::exists(cancel_destination_root / L"a.txt") ||
        fs::exists(cancel_destination_root / L"b.txt")) {
        fs::remove_all(base, ec);
        return 18;
    }

    fs::remove_all(base, ec);
    return 0;
}
