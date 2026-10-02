#include "velocitycopy/execution_control.hpp"
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/live_copy_plan.hpp"
#include "velocitycopy/source_removal_recovery.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

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

int wmain() {
    const auto root = fs::temp_directory_path() / L"VelocityCopySourceRemovalRecoveryTest";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec) return 1;

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
            if (!plan.restore_parked_source_removal(recovery)) return 9;
            const auto result = retry_recovery(plan);
            const auto incidents = plan.parked_incidents();
            if (!result.success || !fs::exists(source) || result.parked_files != 1 ||
                incidents.size() != 1 || incidents[0].attempt_count != 4) return 10;
        }
    }

    fs::remove_all(root, ec);
    return 0;
}
