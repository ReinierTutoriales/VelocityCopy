#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/live_copy_plan.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

velocitycopy::CopyPlan make_plan(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    velocitycopy::CopyPlan plan{};
    plan.source_roots.push_back(source);
    plan.destination_root = destination;
    plan.directories.push_back({destination});
    plan.files.push_back({1, source / "first.txt", destination / "first.txt", 5});
    plan.files.push_back({2, source / "second.txt", destination / "second.txt", 6});
    plan.files.push_back({3, source / "third.txt", destination / "third.txt", 5});
    plan.total_bytes = 16;
    plan.largest_file_bytes = 6;
    return plan;
}

velocitycopy::CopyPlan make_edit_plan(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    velocitycopy::CopyPlan plan{};
    plan.source_roots.push_back(source);
    plan.destination_root = destination;
    plan.directories.push_back({destination});
    for (std::uint64_t id = 1; id <= 5; ++id) {
        const auto name = std::to_string(id) + ".txt";
        plan.files.push_back({id, source / name, destination / name, 1});
    }
    plan.total_bytes = 5;
    plan.largest_file_bytes = 1;
    return plan;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using namespace velocitycopy;

    const auto root = fs::temp_directory_path() / "velocitycopy-live-plan-test";
    std::error_code ec;
    fs::remove_all(root, ec);

    const auto source = root / "source";
    const auto destination = root / "destination";
    write_text(source / "first.txt", "first");
    write_text(source / "second.txt", "second");
    write_text(source / "third.txt", "third");

    {
        LiveCopyPlan concurrent_plan(make_plan(source, destination));
        const auto first = concurrent_plan.acquire_next();
        const auto second = concurrent_plan.acquire_next();
        if (!first || !second || first->id != 1 || second->id != 2) {
            fs::remove_all(root, ec);
            return 1;
        }

        auto snapshot = concurrent_plan.snapshot();
        if (snapshot.active_files.size() != 2 || snapshot.pending_files.size() != 1 ||
            snapshot.active_files[0].id != 1 || snapshot.active_files[1].id != 2 ||
            snapshot.pending_files[0].id != 3 || concurrent_plan.remaining_files() != 3) {
            fs::remove_all(root, ec);
            return 2;
        }

        concurrent_plan.complete_active(first->id);
        snapshot = concurrent_plan.snapshot();
        if (snapshot.active_files.size() != 1 || snapshot.active_files[0].id != 2 ||
            snapshot.completed_files != 1 || snapshot.completed_bytes != 5 ||
            concurrent_plan.completed_files() != 1 || concurrent_plan.completed_bytes() != 5) {
            fs::remove_all(root, ec);
            return 3;
        }

        concurrent_plan.release_active(second->id);
        const auto view = concurrent_plan.queue_view(1);
        if (view.pending_files.size() != 1 || view.pending_files[0].id != 2 ||
            view.pending_count != 2 || view.active_count != 0 || view.completed_files != 1 ||
            concurrent_plan.remaining_files() != 2) {
            fs::remove_all(root, ec);
            return 4;
        }
    }

    {
        LiveCopyPlan appended_plan(make_plan(source, destination));
        CopyPlan extra{};
        extra.source_roots.push_back(source / "more");
        extra.destination_root = destination;
        extra.directories.push_back({destination / "more"});
        extra.files.push_back({1, source / "fourth.txt", destination / "fourth.txt", 7});
        extra.files.push_back({2, source / "fifth.txt", destination / "fifth.txt", 8});
        extra.total_bytes = 15;
        extra.largest_file_bytes = 8;

        if (appended_plan.append(std::move(extra)) != LivePlanAppendResult::Appended) {
            fs::remove_all(root, ec);
            return 5;
        }

        auto snapshot = appended_plan.snapshot();
        if (snapshot.pending_files.size() != 5 || snapshot.total_files != 5 ||
            snapshot.total_bytes != 31 || snapshot.pending_files[3].id != 4 ||
            snapshot.pending_files[4].id != 5 || appended_plan.largest_file_bytes() != 8) {
            fs::remove_all(root, ec);
            return 6;
        }

        const auto completed = appended_plan.acquire_next();
        if (!completed || completed->id != 1) {
            fs::remove_all(root, ec);
            return 7;
        }
        appended_plan.complete_active(completed->id);

        CopyPlan completed_collision{};
        completed_collision.destination_root = destination;
        completed_collision.files.push_back({1, source / "replacement.txt", destination / "first.txt", 1});
        completed_collision.total_bytes = 1;
        completed_collision.largest_file_bytes = 1;
        if (appended_plan.append(std::move(completed_collision)) != LivePlanAppendResult::DestinationCollision) {
            fs::remove_all(root, ec);
            return 8;
        }

        CopyPlan pending_collision{};
        pending_collision.destination_root = destination;
        pending_collision.files.push_back({1, source / "duplicate.txt", destination / "fifth.txt", 1});
        pending_collision.total_bytes = 1;
        pending_collision.largest_file_bytes = 1;
        if (appended_plan.append(std::move(pending_collision)) != LivePlanAppendResult::DestinationCollision) {
            fs::remove_all(root, ec);
            return 9;
        }

        if (!appended_plan.remove_pending_file(5)) {
            fs::remove_all(root, ec);
            return 10;
        }
        CopyPlan reuse_removed{};
        reuse_removed.destination_root = destination;
        reuse_removed.files.push_back({1, source / "replacement-fifth.txt", destination / "fifth.txt", 2});
        reuse_removed.total_bytes = 2;
        reuse_removed.largest_file_bytes = 2;
        if (appended_plan.append(std::move(reuse_removed)) != LivePlanAppendResult::Appended) {
            fs::remove_all(root, ec);
            return 11;
        }

        CopyPlan other_destination{};
        other_destination.destination_root = root / "other";
        if (appended_plan.append(std::move(other_destination)) != LivePlanAppendResult::DifferentDestination) {
            fs::remove_all(root, ec);
            return 12;
        }

        CopyPlan other_operation{};
        other_operation.destination_root = destination;
        other_operation.operation = FileOperation::Move;
        if (appended_plan.append(std::move(other_operation)) != LivePlanAppendResult::DifferentOperation) {
            fs::remove_all(root, ec);
            return 35;
        }
    }

    {
        LiveCopyPlan edit_plan(make_edit_plan(source, destination));
        if (!edit_plan.reorder_pending_files({3, 1, 5})) {
            fs::remove_all(root, ec);
            return 13;
        }
        auto snapshot = edit_plan.snapshot();
        if (snapshot.pending_files.size() != 5 ||
            snapshot.pending_files[0].id != 3 || snapshot.pending_files[1].id != 2 ||
            snapshot.pending_files[2].id != 1 || snapshot.pending_files[3].id != 4 ||
            snapshot.pending_files[4].id != 5) {
            fs::remove_all(root, ec);
            return 14;
        }

        if (!edit_plan.move_pending_files_down({3, 1})) {
            fs::remove_all(root, ec);
            return 15;
        }
        snapshot = edit_plan.snapshot();
        if (snapshot.pending_files[0].id != 2 || snapshot.pending_files[1].id != 3 ||
            snapshot.pending_files[2].id != 4 || snapshot.pending_files[3].id != 1 ||
            snapshot.pending_files[4].id != 5) {
            fs::remove_all(root, ec);
            return 16;
        }

        if (!edit_plan.move_pending_files_up({3, 1})) {
            fs::remove_all(root, ec);
            return 17;
        }
        snapshot = edit_plan.snapshot();
        if (snapshot.pending_files[0].id != 3 || snapshot.pending_files[1].id != 2 ||
            snapshot.pending_files[2].id != 1 || snapshot.pending_files[3].id != 4 ||
            snapshot.pending_files[4].id != 5) {
            fs::remove_all(root, ec);
            return 18;
        }

        if (edit_plan.remove_pending_files({2, 4, 999}) != 2) {
            fs::remove_all(root, ec);
            return 19;
        }
        snapshot = edit_plan.snapshot();
        if (snapshot.pending_files.size() != 3 || snapshot.total_files != 3 ||
            snapshot.total_bytes != 3 || snapshot.pending_files[0].id != 3 ||
            snapshot.pending_files[1].id != 1 || snapshot.pending_files[2].id != 5) {
            fs::remove_all(root, ec);
            return 20;
        }

        const auto acquired = edit_plan.acquire_next();
        if (!acquired || acquired->id != 3 ||
            !edit_plan.reorder_pending_files({5, 3, 1})) {
            fs::remove_all(root, ec);
            return 21;
        }
        snapshot = edit_plan.snapshot();
        if (snapshot.pending_files.size() != 2 || snapshot.pending_files[0].id != 5 ||
            snapshot.pending_files[1].id != 1 || snapshot.active_files.size() != 1 ||
            snapshot.active_files[0].id != 3) {
            fs::remove_all(root, ec);
            return 22;
        }
        edit_plan.release_active(3);
    }

    // Removing or skipping the largest remaining file must immediately update
    // adaptive workload metrics and release the destination reservation.
    {
        LiveCopyPlan removal_plan(make_plan(source, destination));
        if (!removal_plan.remove_pending_file(2) ||
            removal_plan.total_bytes() != 10 || removal_plan.total_files() != 2 ||
            removal_plan.largest_file_bytes() != 5) {
            fs::remove_all(root, ec);
            return 23;
        }

        LiveCopyPlan skip_plan(make_plan(source, destination));
        const auto first = skip_plan.acquire_next();
        const auto largest = skip_plan.acquire_next();
        if (!first || !largest || first->id != 1 || largest->id != 2 ||
            !skip_plan.skip_active(largest->id)) {
            fs::remove_all(root, ec);
            return 24;
        }
        auto snapshot = skip_plan.snapshot();
        if (snapshot.total_bytes != 10 || snapshot.total_files != 2 ||
            snapshot.completed_bytes != 0 || snapshot.completed_files != 0 ||
            snapshot.active_files.size() != 1 || snapshot.active_files[0].id != 1 ||
            snapshot.pending_files.size() != 1 || snapshot.pending_files[0].id != 3 ||
            skip_plan.largest_file_bytes() != 5 || skip_plan.remaining_files() != 2) {
            fs::remove_all(root, ec);
            return 25;
        }

        CopyPlan reuse_skipped{};
        reuse_skipped.destination_root = destination;
        reuse_skipped.files.push_back({1, source / "replacement-second.txt", destination / "second.txt", 4});
        reuse_skipped.total_bytes = 4;
        reuse_skipped.largest_file_bytes = 4;
        if (skip_plan.append(std::move(reuse_skipped)) != LivePlanAppendResult::Appended ||
            skip_plan.total_bytes() != 14 || skip_plan.total_files() != 3 ||
            skip_plan.largest_file_bytes() != 5) {
            fs::remove_all(root, ec);
            return 26;
        }
        skip_plan.release_active(first->id);
    }

    {
        LiveCopyPlan drained_plan(make_plan(source, destination));
        while (auto file = drained_plan.acquire_next()) {
            drained_plan.complete_active(file->id);
        }

        if (drained_plan.completed_files() != 3 || drained_plan.completed_bytes() != 16 ||
            drained_plan.remaining_files() != 0) {
            fs::remove_all(root, ec);
            return 27;
        }

        CopyPlan ordinary{};
        ordinary.destination_root = destination;
        ordinary.files.push_back({1, source / "fourth.txt", destination / "fourth.txt", 7});
        ordinary.total_bytes = 7;
        ordinary.largest_file_bytes = 7;
        if (drained_plan.append(std::move(ordinary)) != LivePlanAppendResult::Drained) {
            fs::remove_all(root, ec);
            return 28;
        }

        CopyPlan reserved{};
        reserved.destination_root = destination;
        reserved.files.push_back({1, source / "fifth.txt", destination / "fifth.txt", 8});
        reserved.total_bytes = 8;
        reserved.largest_file_bytes = 8;
        if (drained_plan.append(std::move(reserved), true) != LivePlanAppendResult::Appended) {
            fs::remove_all(root, ec);
            return 29;
        }

        const auto snapshot = drained_plan.snapshot();
        if (snapshot.pending_files.size() != 1 || snapshot.pending_files[0].id != 4 ||
            snapshot.total_files != 4 || snapshot.total_bytes != 24 ||
            snapshot.completed_files != 3 || snapshot.completed_bytes != 16 ||
            drained_plan.remaining_files() != 1) {
            fs::remove_all(root, ec);
            return 30;
        }
    }

    LiveCopyPlan live_plan(make_plan(source, destination));
    JobExecutor executor;
    ExecutionControl edit_control;
    bool edited = false;
    bool saw_adjusted_totals = false;

    const auto result = executor.execute(
        live_plan,
        edit_control,
        JobExecutionOptions{1},
        [&](const JobProgress& progress) {
            if (!edited && progress.completed_files == 1) {
                if (!live_plan.move_pending_file(3, 0)) {
                    return JobDecision::Cancel;
                }
                if (!live_plan.remove_pending_file(2)) {
                    return JobDecision::Cancel;
                }
                edited = true;
            }

            if (edited && progress.total_files == 2 && progress.total_bytes == 10) {
                saw_adjusted_totals = true;
            }
            return JobDecision::Continue;
        });

    if (!result.success || result.cancelled || !edited || !saw_adjusted_totals) {
        fs::remove_all(root, ec);
        return 31;
    }

    if (!fs::exists(destination / "first.txt") ||
        !fs::exists(destination / "third.txt") ||
        fs::exists(destination / "second.txt")) {
        fs::remove_all(root, ec);
        return 32;
    }

    if (read_text(destination / "first.txt") != "first" ||
        read_text(destination / "third.txt") != "third") {
        fs::remove_all(root, ec);
        return 33;
    }

    const auto final_snapshot = live_plan.snapshot();
    if (!final_snapshot.active_files.empty() || !final_snapshot.pending_files.empty() ||
        final_snapshot.total_files != 2 || final_snapshot.total_bytes != 10 ||
        final_snapshot.completed_files != 2 || final_snapshot.completed_bytes != 10) {
        fs::remove_all(root, ec);
        return 34;
    }

    fs::remove_all(root, ec);
    return 0;
}
