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
    plan.directories.push_back({destination});
    plan.files.push_back({1, source / "first.txt", destination / "first.txt", 5});
    plan.files.push_back({2, source / "second.txt", destination / "second.txt", 6});
    plan.files.push_back({3, source / "third.txt", destination / "third.txt", 5});
    plan.total_bytes = 16;
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

    // Contract: the live plan supports more than one active file at a time.
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
            snapshot.pending_files[0].id != 3) {
            fs::remove_all(root, ec);
            return 2;
        }

        concurrent_plan.complete_active(first->id);
        snapshot = concurrent_plan.snapshot();
        if (snapshot.active_files.size() != 1 || snapshot.active_files[0].id != 2) {
            fs::remove_all(root, ec);
            return 3;
        }

        concurrent_plan.release_active(second->id);
        snapshot = concurrent_plan.snapshot();
        if (!snapshot.active_files.empty() || snapshot.pending_files.size() != 2 ||
            snapshot.pending_files[0].id != 2 || snapshot.pending_files[1].id != 3) {
            fs::remove_all(root, ec);
            return 4;
        }
    }

    LiveCopyPlan live_plan(make_plan(source, destination));
    JobExecutor executor;
    bool edited = false;
    bool saw_adjusted_totals = false;

    const auto result = executor.execute(live_plan, [&](const JobProgress& progress) {
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
        return 5;
    }

    if (!fs::exists(destination / "first.txt") ||
        !fs::exists(destination / "third.txt") ||
        fs::exists(destination / "second.txt")) {
        fs::remove_all(root, ec);
        return 6;
    }

    if (read_text(destination / "first.txt") != "first" ||
        read_text(destination / "third.txt") != "third") {
        fs::remove_all(root, ec);
        return 7;
    }

    const auto final_snapshot = live_plan.snapshot();
    if (!final_snapshot.active_files.empty() || !final_snapshot.pending_files.empty() ||
        final_snapshot.total_files != 2 || final_snapshot.total_bytes != 10) {
        fs::remove_all(root, ec);
        return 8;
    }

    fs::remove_all(root, ec);
    return 0;
}
