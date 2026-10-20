#include "velocitycopy/destination_catalog.hpp"
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/path_key.hpp"
#include "velocitycopy/queue_archive.hpp"
#include "velocitycopy/transfer_router.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace velocitycopy;

bool set_case(const fs::path& path, const bool sensitive) {
    const HANDLE handle = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    FILE_CASE_SENSITIVE_INFO info{sensitive ? FILE_CS_FLAG_CASE_SENSITIVE_DIR : 0u};
    const auto result = SetFileInformationByHandle(handle, FileCaseSensitiveInfo, &info, sizeof(info));
    const auto error = GetLastError();
    CloseHandle(handle);
    if (!result) std::cerr << "Unable to set case-sensitive fixture: " << error << '\n';
    return result != FALSE;
}
void write_text(const fs::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary);
    stream << text;
    if (!stream) throw std::runtime_error("Unable to write case-sensitive fixture");
}
std::string read_text(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

int run_test() {
    const auto root = fs::temp_directory_path() /
        (L"VelocityCopyCaseSensitivePathsTest-" + std::to_wstring(GetCurrentProcessId()));
    const auto source = root / L"source";
    const auto destination = root / L"destination";
    fs::create_directories(source);
    fs::create_directories(destination);
    // This suite must actually enable the native flag. It is never silently skipped.
    if (!set_case(source, true) || !set_case(destination, true)) return 1;
    if (!directory_case_sensitive(source) || !directory_case_sensitive(destination)) return 2;
    write_text(source / L"A.txt", "upper-ascii");
    write_text(source / L"a.txt", "lower-ascii");
    write_text(source / L"É.txt", "upper-unicode");
    write_text(source / L"é.txt", "lower-unicode");
    PathKeyFactory keys;
    if (path_key_equal(keys.make(destination / L"A.txt"), keys.make(destination / L"a.txt")) ||
        path_key_equal(keys.make(destination / L"É.txt"), keys.make(destination / L"é.txt"))) return 3;
    if (same_destination(destination / L"A", destination / L"a")) return 4;
    if (DestinationCatalog::validate(std::vector<fs::path>{destination / L"A"}, destination / L"a") !=
        DestinationValidation::Valid) return 5;

    CopyJob job{};
    job.sources = {source};
    job.destination = destination;
    job.layout = DestinationLayout::ContentsOnly;
    const auto raw = JobPlanner{}.build(job);
    if (raw.files.size() != 4 || !JobExecutor{}.execute(raw).success) return 6;
    for (const auto& entry : {L"A.txt", L"a.txt", L"É.txt", L"é.txt"}) {
        if (read_text(source / entry) != read_text(destination / entry)) return 7;
    }
    if (read_text(destination / L"A.txt") == read_text(destination / L"a.txt") ||
        read_text(destination / L"É.txt") == read_text(destination / L"é.txt")) return 8;

    const auto ordinary = root / L"ordinary";
    fs::create_directory(ordinary);
    job.destination = ordinary;
    bool collision = false;
    try { (void)JobPlanner{}.build(job); }
    catch (const fs::filesystem_error& error) { collision = error.code() == std::errc::file_exists; }
    if (!collision || !fs::is_empty(ordinary)) return 9;

    // Archive a case-distinct plan, restore its current native policy, and copy
    // both files. No locale-sensitive comparison key is persisted.
    const auto restored_target = root / L"restored";
    fs::create_directory(restored_target);
    if (!set_case(restored_target, true)) return 10;
    job.destination = restored_target;
    QueueArchive archive{};
    archive.current_plan = JobPlanner{}.build(job);
    QueueArchiveStore store;
    const auto archive_path = root / L"queue.vcq";
    if (!store.save(archive_path, archive)) return 11;
    const auto loaded = store.load(archive_path);
    if (!loaded || !loaded->current_plan) return 12;
    LiveCopyPlan restored(*loaded->current_plan);
    if (!JobExecutor{}.execute(restored).success || read_text(restored_target / L"A.txt") != "upper-ascii" ||
        read_text(restored_target / L"a.txt") != "lower-ascii") return 13;

    fs::create_directory(destination / L"A");
    fs::create_directory(destination / L"a");
    QueueArchive escaped{};
    CopyPlan invalid{};
    invalid.source_roots = {source};
    invalid.destination_root = destination / L"A";
    invalid.files = {{1, source / L"A.txt", destination / L"a" / L"escaped.txt", 11}};
    invalid.total_bytes = 11;
    escaped.current_plan = invalid;
    if (store.save(root / L"invalid.vcq", escaped)) return 14;

    // Completed outputs remain reserved if the parent policy changes before
    // a new batch is appended to the same live queue.
    const auto appended = root / L"appended";
    fs::create_directory(appended);
    if (!set_case(appended, true)) return 20;
    job.sources = {source / L"A.txt"};
    job.destination = appended;
    LiveCopyPlan completed(JobPlanner{}.build(job));
    if (!JobExecutor{}.execute(completed).success || read_text(appended / L"A.txt") != "upper-ascii") return 21;
    if (!set_case(appended, false)) return 22;
    job.sources = {source / L"a.txt"};
    if (completed.append(JobPlanner{}.build(job), true) != LivePlanAppendResult::DestinationCollision ||
        completed.total_files() != 1 || read_text(appended / L"A.txt") != "upper-ascii") return 23;

    // A changed flag after planning is not permission to overwrite the other
    // spelling. The item must remain recoverable without mutating the target.
    const auto changed = root / L"changed";
    fs::create_directory(changed);
    if (!set_case(changed, true)) return 15;
    job.sources = {source / L"A.txt"};
    job.destination = changed;
    auto stale = JobPlanner{}.build(job);
    if (!set_case(changed, false)) return 16;
    write_text(changed / L"a.txt", "existing");
    LiveCopyPlan stale_live(std::move(stale));
    ExecutionControl control;
    JobExecutionOptions options{};
    options.conflict_policy = ConflictPolicy::ReplaceAll;
    const auto stale_result = JobExecutor{}.execute(stale_live, control, options);
    if (!stale_result.success || stale_result.parked_files != 1 ||
        read_text(changed / L"a.txt") != "existing" || !fs::exists(source / L"A.txt")) return 17;

    // The same-volume Move path must preserve all four distinct names too.
    const auto moved_target = root / L"moved";
    fs::create_directory(moved_target);
    if (!set_case(moved_target, true)) return 18;
    job.sources = {source};
    job.destination = moved_target;
    job.operation = FileOperation::Move;
    const auto moved = JobExecutor{}.execute(job);
    if (!moved.success || moved.outcomes.succeeded != 4 || fs::exists(source) ||
        read_text(moved_target / L"A.txt") != "upper-ascii" ||
        read_text(moved_target / L"a.txt") != "lower-ascii" ||
        read_text(moved_target / L"É.txt") != "upper-unicode" ||
        read_text(moved_target / L"é.txt") != "lower-unicode") return 19;
    fs::remove_all(root);
    return 0;
}

int main() {
    try {
        const auto result = run_test();
        if (result) std::cerr << "Case-sensitive paths failed at check " << result << '\n';
        return result;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 99;
    }
}
