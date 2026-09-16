#include "velocitycopy/live_copy_plan.hpp"

#include <filesystem>
#include <type_traits>
#include <utility>
#include <vector>

using velocitycopy::LiveCopyPlan;
using velocitycopy::PlannedDirectory;

static_assert(std::is_same_v<
    decltype(std::declval<const LiveCopyPlan&>().directories()),
    std::vector<PlannedDirectory>>);
static_assert(std::is_same_v<
    decltype(std::declval<const LiveCopyPlan&>().source_roots()),
    std::vector<std::filesystem::path>>);

int main() {
    namespace fs = std::filesystem;
    using namespace velocitycopy;

    const fs::path destination = LR"(C:\VelocityCopySnapshotContract\destination)";
    const fs::path source_a = LR"(C:\VelocityCopySnapshotContract\source-a)";
    const fs::path source_b = LR"(C:\VelocityCopySnapshotContract\source-b)";

    CopyPlan initial{};
    initial.destination_root = destination;
    initial.source_roots.push_back(source_a);
    initial.directories.push_back({destination / L"source-a"});
    initial.files.push_back({1, source_a / L"a.txt", destination / L"source-a" / L"a.txt", 1});
    initial.total_bytes = 1;
    initial.largest_file_bytes = 1;

    LiveCopyPlan live(std::move(initial));
    const auto old_directories = live.directories();
    const auto old_roots = live.source_roots();

    CopyPlan appended{};
    appended.destination_root = destination;
    appended.source_roots.push_back(source_b);
    appended.directories.push_back({destination / L"source-b"});
    appended.files.push_back({1, source_b / L"b.txt", destination / L"source-b" / L"b.txt", 2});
    appended.total_bytes = 2;
    appended.largest_file_bytes = 2;

    if (live.append(std::move(appended)) != LivePlanAppendResult::Appended) return 1;
    if (old_directories.size() != 1 || old_directories.front().destination != destination / L"source-a") return 2;
    if (old_roots.size() != 1 || old_roots.front() != source_a) return 3;

    const auto new_directories = live.directories();
    const auto new_roots = live.source_roots();
    if (new_directories.size() != 2 || new_directories.back().destination != destination / L"source-b") return 4;
    if (new_roots.size() != 2 || new_roots.back() != source_b) return 5;

    return 0;
}
