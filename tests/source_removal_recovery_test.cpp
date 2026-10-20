#include "velocitycopy/execution_control.hpp"
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/live_copy_plan.hpp"
#include "velocitycopy/source_removal_recovery.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>

namespace {
namespace fs = std::filesystem;
using namespace velocitycopy;

void write_text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

SourceRemovalRecovery make_recovery(
    const fs::path& source,
    const fs::path& destination,
    std::uint32_t attempt = 1) {
    const auto source_probe = probe_file_fingerprint(source);
    const auto destination_probe = probe_file_fingerprint(destination);
    if (source_probe.status != FingerprintProbe::Present ||
        destination_probe.status != FingerprintProbe::Present) {
        return {};
    }
    SourceRemovalRecovery recovery{};
    recovery.hresult = static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
    recovery.source = source;
    recovery.destination = destination;
    recovery.attempt_count = attempt;
    recovery.source_fingerprint = source_probe.fingerprint;
    recovery.destination_fingerprint = destination_probe.fingerprint;
    return recovery;
}

CopyPlan empty_move_plan(const fs::path& destination) {
    CopyPlan plan{};
    plan.operation = FileOperation::Move;
    plan.destination_root = destination;
    return plan;
}

JobResult retry_recovery(LiveCopyPlan& plan) {
    ExecutionControl control;
    JobExecutionOptions options{};
    options.worker_count = 1;
    options.retry_source_removals = true;
    return JobExecutor{}.execute(plan, control, options, {});
}

fs::path unavailable_root() {
    for (wchar_t drive = L'Z'; drive >= L'D'; --drive) {
        std::wstring root{drive, L':', L'\\'};
        if (GetDriveTypeW(root.c_str()) == DRIVE_NO_ROOT_DIR) return fs::path(root);
    }
    return {};
}

} // namespace

int run_test() {
    const auto root = fs::temp_directory_path() / L"VelocityCopySourceRemovalRecoveryTest";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec) return 1;

    // Cancel/Stop must be observed before retrying destructive source removal.
    for (bool cancel : {false, true}) {
        const auto source = root / (cancel ? L"cancel" : L"stop") / L"source.txt";
        const auto destination = source.parent_path() / L"destination.txt";
        write_text(source, "same");
        write_text(destination, "same");
        LiveCopyPlan plan(empty_move_plan(destination.parent_path()));
        if (!plan.restore_parked_source_removal(make_recovery(source, destination, 7))) return 30;
        ExecutionControl control;
        if (cancel) control.request_cancel(); else control.request_stop();
        JobExecutionOptions options{1};
        options.retry_source_removals = true;
        const auto result = JobExecutor{}.execute(plan, control, options, {});
        const auto incidents = plan.parked_incidents();
        if (result.success || result.cancelled != cancel || result.stopped == cancel ||
            !fs::exists(source) || !fs::exists(destination) || incidents.size() != 1 ||
            incidents[0].attempt_count != 7) return 31;
    }

    // Destination missing: never delete the retained source.
    {
        const auto source = root / L"missing" / L"source.txt";
        const auto destination = root / L"missing" / L"destination.txt";
        write_text(source, "same");
        write_text(destination, "same");
        auto recovery = make_recovery(source, destination);
        if (recovery.source.empty()) return 2;
        LiveCopyPlan plan(empty_move_plan(destination.parent_path()));
        if (!plan.restore_parked_source_removal(recovery)) return 3;
        fs::remove(destination, ec);
        const auto result = retry_recovery(plan);
        if (!result.success || !fs::exists(source) ||
            result.outcomes.copied_source_retained != 1 || result.parked_files != 0) return 4;
    }

    // Destination changed: same safety result.
    {
        const auto source = root / L"changed" / L"source.txt";
        const auto destination = root / L"changed" / L"destination.txt";
        write_text(source, "same");
        write_text(destination, "same");
        auto recovery = make_recovery(source, destination);
        LiveCopyPlan plan(empty_move_plan(destination.parent_path()));
        if (!plan.restore_parked_source_removal(recovery)) return 5;
        write_text(destination, "different-size");
        const auto result = retry_recovery(plan);
        if (!result.success || !fs::exists(source) ||
            result.outcomes.copied_source_retained != 1) return 6;
    }

    // Both fingerprints match: source removal is allowed.
    {
        const auto source = root / L"match" / L"source.txt";
        const auto destination = root / L"match" / L"destination.txt";
        write_text(source, "same");
        write_text(destination, "same");
        auto recovery = make_recovery(source, destination, 2);
        LiveCopyPlan plan(empty_move_plan(destination.parent_path()));
        if (!plan.restore_parked_source_removal(recovery)) return 7;
        const auto result = retry_recovery(plan);
        if (!result.success || fs::exists(source) ||
            result.outcomes.succeeded != 1 || result.parked_files != 0) return 8;
    }

    // Destination verified but source already absent: the move is already complete.
    {
        const auto source = root / L"removed" / L"source.txt";
        const auto destination = root / L"removed" / L"destination.txt";
        write_text(source, "same");
        write_text(destination, "same");
        auto recovery = make_recovery(source, destination, 3);
        LiveCopyPlan plan(empty_move_plan(destination.parent_path()));
        if (!plan.restore_parked_source_removal(recovery)) return 9;
        fs::remove(source, ec);
        const auto result = retry_recovery(plan);
        if (!result.success || fs::exists(source) || !fs::exists(destination) ||
            result.outcomes.succeeded != 1 ||
            result.outcomes.copied_source_retained != 0 || result.parked_files != 0) return 10;
    }

    // Unavailable destination volume: remain parked and do not consume an attempt.
    {
        const auto missing_root = unavailable_root();
        if (!missing_root.empty()) {
            const auto source = root / L"offline" / L"source.txt";
            const auto original_destination = root / L"offline" / L"destination.txt";
            write_text(source, "same");
            write_text(original_destination, "same");
            auto recovery = make_recovery(source, original_destination, 4);
            recovery.destination = missing_root / L"VelocityCopy" / L"destination.txt";
            LiveCopyPlan plan(empty_move_plan(missing_root));
            if (!plan.restore_parked_source_removal(recovery)) return 11;
            const auto result = retry_recovery(plan);
            const auto incidents = plan.parked_incidents();
            if (!result.success || !fs::exists(source) || result.parked_files != 1 ||
                incidents.size() != 1 || incidents[0].attempt_count != 4) return 12;
        }
    }

    // Simulate replacement after the executor's advisory validation. Matching
    // size and write time cannot authorize deletion of a different file ID.
    for (const bool replace_destination : {false, true}) {
        const auto folder = root / (replace_destination ? L"destination-swap" : L"source-swap");
        const auto source = folder / L"source.txt";
        const auto destination = folder / L"destination.txt";
        write_text(source, "same");
        write_text(destination, "same");
        const auto recovery = make_recovery(source, destination);
        if (validate_source_removal_recovery(recovery) != SourceRemovalValidation::Verified) return 32;
        const auto replaced = replace_destination ? destination : source;
        fs::rename(replaced, folder / L"original.txt");
        write_text(replaced, "same");
        const auto expected = replace_destination ? recovery.destination_fingerprint : recovery.source_fingerprint;
        const HANDLE file = CreateFileW(replaced.c_str(), FILE_WRITE_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) return 33;
        ULARGE_INTEGER ticks{};
        ticks.QuadPart = static_cast<ULONGLONG>(expected.last_write_time);
        const FILETIME time{ticks.LowPart, ticks.HighPart};
        const bool set_time = SetFileTime(file, nullptr, nullptr, &time) != FALSE;
        CloseHandle(file);
        if (!set_time || remove_verified_recovery_source(recovery) == S_OK ||
            !fs::exists(source) || !fs::exists(destination)) return 34;
    }
    // An existing writer must prevent destructive recovery even when all
    // fingerprints still match. Missing identity is deliberately fail-closed.
    {
        const auto source = root / L"writer" / L"source.txt";
        const auto destination = root / L"writer" / L"destination.txt";
        write_text(source, "same");
        write_text(destination, "same");
        auto recovery = make_recovery(source, destination);
        for (const auto& path : {source, destination}) {
            const HANDLE writer = CreateFileW(path.c_str(), GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (writer == INVALID_HANDLE_VALUE) return 35;
            const auto result = remove_verified_recovery_source(recovery);
            CloseHandle(writer);
            if (result == S_OK || !fs::exists(source)) return 36;
        }
        recovery.source_fingerprint.has_identity = false;
        if (remove_verified_recovery_source(recovery) == S_OK || !fs::exists(source)) return 37;
        recovery = make_recovery(source, destination);
        if (remove_verified_recovery_source(recovery) != S_OK || fs::exists(source) ||
            !fs::exists(destination)) return 38;
    }

    fs::remove_all(root, ec);
    return 0;
}

int wmain() {
    const auto result = run_test();
    if (result) std::cerr << "Source-removal recovery failed at check " << result << '\n';
    return result;
}
