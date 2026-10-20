#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/queue_archive.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstdlib>
#include <new>

namespace { thread_local int fail_after = -1; }
void* operator new(std::size_t size) {
    if (fail_after == 0) { fail_after = -1; throw std::bad_alloc{}; }
    if (fail_after > 0) --fail_after;
    if (auto* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace fs = std::filesystem;
using namespace velocitycopy;
#define REQUIRE(value) do { if (!(value)) throw std::runtime_error("Tree Move check at line " + std::to_string(__LINE__)); } while (false)

void write_text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out << text;
    REQUIRE(out.good());
}
std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
std::array<unsigned char, 16> identity(const fs::path& path, std::uint64_t* volume = nullptr) {
    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);
    FILE_ID_INFO info{};
    const bool ok = GetFileInformationByHandleEx(handle, FileIdInfo, &info, sizeof(info)) != FALSE;
    CloseHandle(handle); REQUIRE(ok);
    if (volume) *volume = info.VolumeSerialNumber;
    std::array<unsigned char, 16> result{};
    std::copy(std::begin(info.FileId.Identifier), std::end(info.FileId.Identifier), result.begin());
    return result;
}
struct Fixture {
    fs::path root, source, target;
    Fixture() {
        static unsigned sequence = 0;
        root = fs::temp_directory_path() / (L"VelocityCopyTreeMove-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence));
        REQUIRE(fs::create_directory(root));
        source = root / L"資料-\U0001F4C1"; target = root / L"destination";
        fs::create_directory(source); fs::create_directory(target);
        write_text(source / L"É.txt", "upper");
        write_text(source / L"sub" / L"данные-\U0001F600.txt", "nested");
        fs::create_directory(source / L"empty");
    }
    ~Fixture() { std::error_code error; fs::remove_all(root, error); }
    CopyJob job() const {
        CopyJob result{}; result.sources = {source}; result.destination = target;
        result.operation = FileOperation::Move; return result;
    }
    fs::path moved() const { return target / source.filename(); }
};

void correctness() {
    { // Root/file identities and empty folders survive; no physical bytes invented.
        Fixture f; const auto root_id = identity(f.source), file_id = identity(f.source / L"É.txt");
        LiveCopyPlan plan(JobPlanner{}.build(f.job())); ExecutionControl control;
        unsigned callbacks = 0;
        auto result = JobExecutor{}.execute(plan, control, JobExecutionOptions{}, [&](const JobProgress& value) {
            ++callbacks; REQUIRE(!value.current_file_skippable && value.current_file_id == 0);
            REQUIRE(value.bytes_written_physical == 0); return JobDecision::Continue;
        });
        REQUIRE(result.success && result.tree_moved && result.outcomes.succeeded == 2 && callbacks == 2);
        REQUIRE(!fs::exists(f.source) && identity(f.moved()) == root_id && identity(f.moved() / L"É.txt") == file_id);
        REQUIRE(read_text(f.moved() / L"sub" / L"данные-\U0001F600.txt") == "nested");
        REQUIRE(fs::is_directory(f.moved() / L"empty") && plan.unresolved_files() == 0);
        REQUIRE(plan.resolution_view().counters.bytes_succeeded == 11);
        REQUIRE(plan.export_remaining_plan().files.empty());
        REQUIRE(!plan.try_move_tree(control));
    }
    for (int variant = 0; variant < 4; ++variant) {
        Fixture f; LiveCopyPlan plan(JobPlanner{}.build(f.job())); ExecutionControl control;
        const auto first = plan.queue_view(2).pending_files.front();
        if (variant == 0) REQUIRE(plan.remove_pending_file(first.id));
        if (variant == 1) REQUIRE(plan.move_pending_file_down(first.id));
        if (variant == 2) { const auto active = plan.acquire_next(); REQUIRE(active); plan.release_active(active->id); }
        if (variant == 3) {
            CopyJob added{}; added.sources = {f.source / L"É.txt"}; added.destination = f.target;
            added.operation = FileOperation::Move;
            REQUIRE(plan.append(JobPlanner{}.build(added)) == LivePlanAppendResult::Appended);
        }
        REQUIRE(!plan.tree_move_candidate() && !plan.try_move_tree(control));
        REQUIRE(fs::exists(f.source) && !fs::exists(f.moved()));
        if (variant == 0) {
            const auto result = JobExecutor{}.execute(plan, control, JobExecutionOptions{});
            REQUIRE(result.success && !result.tree_moved && fs::exists(first.source));
        }
    }
    for (int variant = 0; variant < 3; ++variant) {
        Fixture f; LiveCopyPlan plan(JobPlanner{}.build(f.job())); ExecutionControl control;
        if (variant == 0) write_text(f.source / L"late.txt", "new");
        if (variant == 1) write_text(f.source / L"É.txt", "changed-size");
        if (variant == 2) {
            fs::rename(f.source, f.root / L"original");
            fs::create_directory(f.source);
            write_text(f.source / L"É.txt", "upper");
            write_text(f.source / L"sub" / L"данные-\U0001F600.txt", "nested");
            fs::create_directory(f.source / L"empty");
        }
        REQUIRE(!plan.try_move_tree(control) && fs::exists(f.source) && !fs::exists(f.moved()));
    }
    for (int variant = 0; variant < 4; ++variant) {
        Fixture f; LiveCopyPlan plan(JobPlanner{}.build(f.job())); ExecutionControl control;
        const auto skipped = plan.queue_view(2).pending_files.front();
        bool first = true;
        const auto result = JobExecutor{}.execute(plan, control, JobExecutionOptions{}, [&](const JobProgress&) {
            if (first) {
                first = false;
                if (variant == 0) control.request_cancel();
                if (variant == 1) control.request_stop();
                if (variant == 2) { control.request_pause(); REQUIRE(!plan.try_move_tree(control)); control.resume(); }
                if (variant == 3) control.request_skip(skipped.id);
            }
            return JobDecision::Continue;
        });
        if (variant < 2) REQUIRE(!result.tree_moved && (result.cancelled || result.stopped) && fs::exists(f.source));
        if (variant == 2) REQUIRE(result.success && result.tree_moved);
        if (variant == 3) REQUIRE(result.success && !result.tree_moved && result.outcomes.skipped == 1 && fs::exists(skipped.source));
    }
    for (auto policy : {ConflictPolicy::Prompt, ConflictPolicy::ReplaceAll, ConflictPolicy::SkipAll, ConflictPolicy::KeepBothAll}) {
        Fixture f; write_text(f.moved() / L"É.txt", "existing");
        const auto target_id = identity(f.moved());
        LiveCopyPlan plan(JobPlanner{}.build(f.job())); ExecutionControl control;
        JobExecutionOptions options{}; options.conflict_policy = policy;
        const auto result = JobExecutor{}.execute(plan, control, options);
        REQUIRE(!result.tree_moved && identity(f.moved()) == target_id);
        if (policy == ConflictPolicy::ReplaceAll) REQUIRE(read_text(f.moved() / L"É.txt") == "upper");
        else REQUIRE(read_text(f.moved() / L"É.txt") == "existing");
        if (policy == ConflictPolicy::SkipAll) REQUIRE(result.outcomes.skipped == 1 && fs::exists(f.source / L"É.txt"));
        if (policy == ConflictPolicy::KeepBothAll) REQUIRE(read_text(f.moved() / L"É (2).txt") == "upper");
    }
    { // Archive recovery deliberately uses the per-file protocol.
        Fixture f; LiveCopyPlan plan(JobPlanner{}.build(f.job()));
        QueueArchive archive{}; archive.current_plan = plan.export_remaining_plan();
        QueueArchiveStore store; REQUIRE(store.save(f.root / L"queue.vcq", archive));
        const auto restored = store.load(f.root / L"queue.vcq"); REQUIRE(restored && restored->current_plan);
        LiveCopyPlan resumed(*restored->current_plan); REQUIRE(!resumed.tree_move_candidate());
        const auto result = JobExecutor{}.execute(resumed); REQUIRE(result.success && !result.tree_moved);
    }
    { Fixture f; fs::remove_all(f.source); fs::create_directory(f.source);
      const auto id = identity(f.source); const auto result = JobExecutor{}.execute(f.job());
      REQUIRE(result.success && result.tree_moved && result.outcomes.succeeded == 0 && identity(f.moved()) == id); }
    { // Late cancellation cannot roll the successful rename back or replay it.
        Fixture f; LiveCopyPlan plan(JobPlanner{}.build(f.job())); ExecutionControl control;
        const auto result = JobExecutor{}.execute(plan, control, JobExecutionOptions{}, [&](const JobProgress& value) {
            return value.completed_files ? JobDecision::Cancel : JobDecision::Continue;
        });
        REQUIRE(result.cancelled && result.tree_moved && result.outcomes.succeeded == 2);
        REQUIRE(!fs::exists(f.source) && plan.export_remaining_plan().files.empty());
    }
    { // A stale pre-commit archive must not overwrite or delete the moved tree.
        Fixture f; LiveCopyPlan plan(JobPlanner{}.build(f.job()));
        QueueArchive archive{}; archive.current_plan = plan.export_remaining_plan();
        QueueArchiveStore store; REQUIRE(store.save(f.root / L"before.vcq", archive));
        REQUIRE(JobExecutor{}.execute(plan).tree_moved);
        const auto id = identity(f.moved());
        auto restored = store.load(f.root / L"before.vcq"); REQUIRE(restored && restored->current_plan);
        LiveCopyPlan stale(*restored->current_plan); ExecutionControl control;
        JobExecutionOptions options{}; options.conflict_policy = ConflictPolicy::ReplaceAll;
        const auto result = JobExecutor{}.execute(stale, control, options);
        REQUIRE(!result.tree_moved && result.parked_files == 2 && identity(f.moved()) == id);
        REQUIRE(read_text(f.moved() / L"É.txt") == "upper");
    }
    { // The hosted runner provides a real second volume; single-volume machines report the gap.
        Fixture f; std::uint64_t source_volume{}, other_volume{};
        (void)identity(f.source, &source_volume); (void)identity(fs::current_path(), &other_volume);
        if (source_volume != other_volume) {
            const auto folder = fs::current_path() / (L"VelocityCopyCrossVolume-" + std::to_wstring(GetCurrentProcessId()));
            REQUIRE(fs::create_directory(folder));
            struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{folder};
            f.target = folder;
            LiveCopyPlan plan(JobPlanner{}.build(f.job())); ExecutionControl control;
            REQUIRE(!plan.try_move_tree(control) && fs::exists(f.source));
            const auto result = JobExecutor{}.execute(plan, control, JobExecutionOptions{});
            REQUIRE(result.success && !result.tree_moved && result.outcomes.succeeded == 2);
            REQUIRE(read_text(f.moved() / L"É.txt") == "upper" && !fs::exists(f.source));
            std::cout << "Cross-volume copy/delete fallback passed\n";
        } else std::cout << "Cross-volume runtime case unavailable on this single-volume fixture\n";
    }
    {
        bool succeeded = false; int failures = 0;
        for (int allocation = 0; allocation < 4096; ++allocation) {
            Fixture f; LiveCopyPlan plan(JobPlanner{}.build(f.job())); ExecutionControl control;
            fail_after = allocation;
            const bool moved = plan.try_move_tree(control);
            fail_after = -1;
            if (moved) { REQUIRE(!fs::exists(f.source) && plan.completed_files() == 2); succeeded = true; break; }
            ++failures;
            REQUIRE(fs::exists(f.source) && !fs::exists(f.moved()) && plan.completed_files() == 0);
            REQUIRE(plan.try_move_tree(control));
        }
        REQUIRE(succeeded && failures > 10);
        std::cout << "Tree Move allocation failures checked: " << failures << '\n';
    }
    std::cout << "Tree Move admission, identity, controls, conflicts and recovery passed\n";
}

void benchmark() {
    std::array<std::array<double, 7>, 2> samples{};
    for (int iteration = -1; iteration < 7; ++iteration) {
        for (int order = 0; order < 2; ++order) {
            const int fast = (iteration + order + 2) % 2;
            Fixture f;
            for (int i = 0; i < 3000; ++i) write_text(f.source / (L"file-" + std::to_wstring(i)), "payload");
            LiveCopyPlan plan(JobPlanner{}.build(f.job())); ExecutionControl control;
            JobExecutionOptions options{}; options.enable_tree_move = fast != 0;
            const auto start = std::chrono::steady_clock::now();
            const auto result = JobExecutor{}.execute(plan, control, options);
            const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            REQUIRE(result.success && result.tree_moved == (fast != 0) && result.outcomes.succeeded == 3002);
            REQUIRE(!fs::exists(f.source));
            for (int i = 0; i < 3000; ++i) REQUIRE(read_text(f.moved() / (L"file-" + std::to_wstring(i))) == "payload");
            if (iteration >= 0) samples[fast][iteration] = seconds;
        }
    }
    std::cout << "{\"files\":3002,\"samples\":7,\"baseline_seconds\":[";
    for (int mode = 0; mode < 2; ++mode) {
        if (mode) std::cout << "],\"tree_move_seconds\":[";
        for (int i = 0; i < 7; ++i) { if (i) std::cout << ','; std::cout << samples[mode][i]; }
    }
    std::cout << "]}\n";
}
int main(int argc, char**) {
    try { if (argc > 1) benchmark(); else correctness(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
