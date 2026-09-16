#include "velocitycopy/live_copy_plan.hpp"

#include <filesystem>

int main() {
    namespace fs = std::filesystem;
    using namespace velocitycopy;

    const fs::path destination = LR"(C:\VelocityCopyStructureTest\destination)";
    const fs::path source_a = LR"(C:\VelocityCopyStructureTest\source-a)";
    const fs::path source_b = LR"(C:\VelocityCopyStructureTest\source-b)";

    CopyPlan initial{};
    initial.source_roots.push_back(source_a);
    initial.destination_root = destination;
    initial.directories.push_back({destination / L"source-a"});
    initial.files.push_back({1, source_a / L"a.txt", destination / L"source-a" / L"a.txt", 10});
    initial.total_bytes = 10;
    initial.largest_file_bytes = 10;

    LiveCopyPlan live(std::move(initial));

    CopyPlan appended{};
    appended.source_roots.push_back(source_b);
    appended.destination_root = destination;
    appended.directories.push_back({destination / L"source-b"});
    appended.directories.push_back({destination / L"source-b" / L"empty"});
    appended.files.push_back({1, source_b / L"b.txt", destination / L"source-b" / L"b.txt", 20});
    appended.total_bytes = 20;
    appended.largest_file_bytes = 20;

    if (live.append(std::move(appended)) != LivePlanAppendResult::Appended) return 1;

    const auto exported = live.export_remaining_plan();
    if (exported.source_roots.size() != 2 ||
        exported.source_roots[0] != source_a || exported.source_roots[1] != source_b) return 2;
    if (exported.directories.size() != 3 ||
        exported.directories[0].destination != destination / L"source-a" ||
        exported.directories[1].destination != destination / L"source-b" ||
        exported.directories[2].destination != destination / L"source-b" / L"empty") return 3;
    if (exported.files.size() != 2 || exported.total_bytes != 30 ||
        exported.largest_file_bytes != 20) return 4;

    CopyPlan empty_folder_only{};
    empty_folder_only.source_roots.push_back(source_b / L"empty-only-source");
    empty_folder_only.destination_root = destination;
    empty_folder_only.directories.push_back({destination / L"empty-only"});
    if (live.append(std::move(empty_folder_only)) != LivePlanAppendResult::Appended) return 5;

    const auto after_empty = live.export_remaining_plan();
    if (after_empty.source_roots.size() != 3 || after_empty.directories.size() != 4 ||
        after_empty.directories.back().destination != destination / L"empty-only") return 6;

    const auto before_reject = live.export_remaining_plan();
    CopyPlan rejected{};
    rejected.source_roots.push_back(LR"(C:\VelocityCopyStructureTest\rejected-source)");
    rejected.destination_root = LR"(D:\DifferentDestination)";
    rejected.directories.push_back({LR"(D:\DifferentDestination\must-not-appear)"});
    if (live.append(std::move(rejected)) != LivePlanAppendResult::DifferentDestination) return 7;

    const auto after_reject = live.export_remaining_plan();
    if (after_reject.source_roots != before_reject.source_roots ||
        after_reject.directories.size() != before_reject.directories.size() ||
        after_reject.files.size() != before_reject.files.size() ||
        after_reject.total_bytes != before_reject.total_bytes) return 8;

    return 0;
}
