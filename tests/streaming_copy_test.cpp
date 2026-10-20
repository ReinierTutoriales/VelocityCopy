#include "velocitycopy/streaming_copy.hpp"
#include <windows.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace velocitycopy;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
struct Fixture {
    fs::path root = fs::temp_directory_path() / (L"VelocityCopyStream-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    Fixture() { fs::create_directories(root / "source"); }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
    CopyJob job(const wchar_t* target = L"target") const {
        CopyJob j; j.sources = {root / "source"}; j.destination = root / target; return j;
    }
};
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void write(const fs::path& p, const std::string& value) {
    fs::create_directories(p.parent_path()); std::ofstream out(p, std::ios::binary); out << value;
    require(static_cast<bool>(out), "fixture write failed");
}
std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary); require(static_cast<bool>(in), "missing copied file");
    return {std::istreambuf_iterator<char>(in), {}};
}
void populate(const Fixture& f, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
        write(f.root / "source" / (std::to_string(i) + ".txt"), std::to_string(i));
    fs::create_directories(f.root / "source" / L"空-Пусто-😀");
}
void verify(const Fixture& f, const CopyJob& job, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        auto name = std::to_string(i) + ".txt";
        require(read(job.destination / "source" / name) == std::to_string(i), "wrong destination contents");
        require(read(f.root / "source" / name) == std::to_string(i), "source changed");
    }
    require(fs::is_directory(job.destination / "source" / L"空-Пусто-😀"), "empty Unicode directory missing");
}
int regressions() {
    Fixture f; populate(f, 129);
    StreamingCopyOptions options{8, 16}; ExecutionControl control;
    bool provisional = false, final = false; std::uint64_t physical = 0;
    auto copied = execute_streaming_copy(f.job(), control, {}, options, [&](const JobProgress& p) {
        if (!p.totals_final && p.bytes_written_physical) provisional = true;
        final = p.totals_final; physical = p.bytes_written_physical;
        return JobDecision::Continue;
    });
    require(copied.result.success && copied.scan_complete && copied.result.outcomes.succeeded == 129,
        "stream copy failed");
    require(provisional && final && physical && copied.peak_buffered_entries <= 16,
        "copy did not overlap discovery or buffer exceeded");
    require(copied.recovery.queued_jobs.empty() && !copied.recovery.current_plan, "completed recovery not cleared");
    verify(f, f.job(), 129);

    for (bool stop : {false, true}) {
        ExecutionControl interrupted;
        auto partial = execute_streaming_copy(f.job(stop ? L"stopped" : L"cancelled"), interrupted, {}, options,
            [&](const JobProgress& p) {
                if (p.bytes_written_physical) {
                    if (stop) interrupted.request_stop(); else interrupted.request_cancel();
                }
                return JobDecision::Continue;
            });
        require(!partial.result.success && (stop ? partial.result.stopped : partial.result.cancelled),
            "control result lost");
        require(partial.recovery.queued_jobs.size() == 1, "rediscovery intent lost");
        QueueArchiveStore store;
        require(store.save(f.root / "queue.dat", partial.recovery), "checkpoint save failed");
        auto restored = store.load(f.root / "queue.dat");
        require(restored && restored->queued_jobs.size() == 1, "checkpoint load failed");
        ExecutionControl resumed; JobExecutionOptions skip; skip.conflict_policy = ConflictPolicy::SkipAll;
        auto complete = execute_streaming_copy(restored->queued_jobs.front(), resumed, skip, options);
        require(complete.result.success, "rediscovery recovery failed");
        verify(f, restored->queued_jobs.front(), 129);
    }
    ExecutionControl paused; paused.request_pause();
    std::jthread resume([&] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); paused.resume(); });
    require(execute_streaming_copy(f.job(L"paused"), paused, {}, options).result.success, "pause/resume failed");
    ExecutionControl failure_control; auto checkpoint_options = options;
    checkpoint_options.checkpoint = [&](const QueueArchive&) {
        // Producer may itself be waiting on Pause. Internal stop must join it.
        failure_control.request_pause(); return false;
    };
    auto failed = execute_streaming_copy(f.job(L"checkpoint-failed"), failure_control, {}, checkpoint_options);
    require(!failed.result.success && !fs::exists(f.root / "checkpoint-failed"), "checkpoint failure copied data");
    require(failed.recovery.queued_jobs.size() == 1, "checkpoint failure lost intent");

    ExecutionControl skip_control; bool requested_skip = false;
    auto one_skipped = execute_streaming_copy(f.job(L"one-skipped"), skip_control, {}, options,
        [&](const JobProgress& p) {
            if (!requested_skip && p.current_file_skippable) {
                requested_skip = true; skip_control.request_skip(p.current_file_id);
            }
            return JobDecision::Continue;
        });
    require(requested_skip && one_skipped.result.success && one_skipped.result.outcomes.skipped == 1 &&
        one_skipped.result.outcomes.succeeded == 128, "per-file Skip affected wrong items");

    ExecutionControl throwing_control;
    auto throwing_options = options;
    throwing_options.checkpoint = [&](const QueueArchive&) -> bool {
        throwing_control.request_pause(); throw std::runtime_error("checkpoint failure");
    };
    auto threw = execute_streaming_copy(f.job(L"throwing"), throwing_control, {}, throwing_options);
    require(!threw.result.success && threw.recovery.queued_jobs.size() == 1 &&
        !fs::exists(f.root / "throwing"), "exception during paused checkpoint lost intent or hung");

    JobExecutionOptions policy; policy.conflict_policy = ConflictPolicy::SkipAll;
    ExecutionControl skipping;
    auto skipped = execute_streaming_copy(f.job(), skipping, policy, options);
    require(skipped.result.success && skipped.result.outcomes.skipped == 129, "SkipAll mismatch");
    policy.conflict_policy = ConflictPolicy::ReplaceAll; ExecutionControl replacing;
    require(execute_streaming_copy(f.job(), replacing, policy, options).result.success, "ReplaceAll failed");
    verify(f, f.job(), 129);
    policy.conflict_policy = ConflictPolicy::KeepBothAll; ExecutionControl keeping;
    require(execute_streaming_copy(f.job(), keeping, policy, options).result.success, "KeepBothAll failed");
    require(read(f.root / "target/source/0 (2).txt") == "0", "KeepBoth contents wrong");
    ExecutionControl prompt;
    auto conflict = execute_streaming_copy(f.job(), prompt, {}, options);
    require(!conflict.result.success && conflict.result.destination_conflict &&
        conflict.recovery.queued_jobs.size() == 1, "Prompt conflict lost recovery");

    auto move = f.job(); move.operation = FileOperation::Move; ExecutionControl invalid;
    require(!execute_streaming_copy(move, invalid, {}, options).result.success, "stream Move unexpectedly accepted");
    auto bad_options = options; bad_options.buffered_entries = 1;
    require(!execute_streaming_copy(f.job(), invalid, {}, bad_options).result.success, "invalid buffer accepted");
    std::cout << "Streaming overlap, bounded pending, Unicode, controls, policies and checkpoint recovery passed\n";
    return 0;
}
int measured(bool large) {
    Fixture f; const std::size_t count = large ? 250'001 : 3000; populate(f, count);
    StreamingCopyOptions options{512, 2048};
    if (large) {
        ExecutionControl control; auto start = Clock::now();
        auto result = execute_streaming_copy(f.job(), control, {}, options);
        auto seconds = std::chrono::duration<double>(Clock::now() - start).count();
        require(result.result.success && result.result.outcomes.succeeded == count &&
            result.peak_buffered_entries <= options.buffered_entries, "large copy failed or buffer exceeded");
        verify(f, f.job(), count);
        std::cout << "{\"files\":" << count << ",\"seconds\":" << seconds
                  << ",\"peak_buffered_entries\":" << result.peak_buffered_entries << ",\"contents_verified\":true}\n";
        return 0;
    }
    std::cout << "{\"files\":" << count << ",\"warmups\":1,\"samples\":[";
    bool comma = false;
    for (int round = -1; round < 7; ++round) for (int turn = 0; turn < 2; ++turn) {
        bool stream = (turn == ((round + 2) % 2)); auto job = f.job(stream ? L"stream" : L"static");
        ExecutionControl control; double first = -1; auto start = Clock::now();
        auto progress = [&](const JobProgress& p) {
            if (p.bytes_written_physical && first < 0) first = std::chrono::duration<double>(Clock::now() - start).count();
            return JobDecision::Continue;
        };
        JobResult result;
        if (stream) result = execute_streaming_copy(job, control, {}, options, progress).result;
        else { LiveCopyPlan plan(JobPlanner{}.build(job)); result = JobExecutor{}.execute(plan, control, {}, progress); }
        auto seconds = std::chrono::duration<double>(Clock::now() - start).count();
        require(result.success, "benchmark copy failed"); verify(f, job, count);
        if (round >= 0) {
            if (comma) std::cout << ','; comma = true;
            std::cout << "{\"round\":" << round << ",\"stream\":" << (stream ? "true" : "false")
                      << ",\"seconds\":" << seconds << ",\"first_write_seconds\":" << first << '}';
        }
        fs::remove_all(job.destination);
    }
    std::cout << "]}\n"; return 0;
}
int main(int argc, char** argv) {
    try {
        if (argc > 1) return measured(std::string(argv[1]) == "--large");
        return regressions();
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
