#include "velocitycopy/execution_control.hpp"
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/live_copy_plan.hpp"

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

struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

CopyPlan make_plan(const fs::path& source, const fs::path& destination, FileOperation operation) {
    CopyPlan plan{};
    plan.operation = operation;
    plan.source_roots.push_back(source);
    plan.destination_root = destination;
    plan.directories.push_back({destination});
    plan.files.push_back({1, source / L"a.txt", destination / L"a.txt", 4});
    plan.files.push_back({2, source / L"b.txt", destination / L"b.txt", 4});
    plan.files.push_back({3, source / L"c.txt", destination / L"c.txt", 4});
    plan.total_bytes = 12;
    plan.largest_file_bytes = 4;
    return plan;
}

constexpr auto kSharingViolation = static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));

} // namespace

int wmain() {
    const auto root = fs::temp_directory_path() / L"VelocityCopyItemFailureTest";
    std::error_code ec;
    fs::remove_all(root, ec);
    JobExecutor executor;

    {
        const auto source = root / L"copy-source";
        const auto destination = root / L"copy-destination";
        write_text(source / L"a.txt", "AAAA");
        write_text(source / L"b.txt", "BBBB");
        write_text(source / L"c.txt", "CCCC");
        Handle lock;
        lock.value = CreateFileW((source / L"b.txt").c_str(), GENERIC_READ, 0, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock.value == INVALID_HANDLE_VALUE) { fs::remove_all(root, ec); return 1; }

        LiveCopyPlan plan(make_plan(source, destination, FileOperation::Copy));
        ExecutionControl control;
        std::uint64_t last_total = 0;
        std::uint64_t last_transferred = 0;
        const auto result = executor.execute(plan, control, JobExecutionOptions{1},
            [&](const JobProgress& progress) {
                last_total = progress.total_bytes;
                last_transferred = progress.transferred_bytes;
                return JobDecision::Continue;
            });

        const auto results = plan.retained_results();
        if (!result.success || result.cancelled || result.stopped || result.destination_conflict ||
            result.outcomes.succeeded != 2 || result.outcomes.failed != 1 ||
            results.size() != 1 || results[0].file_id != 2 ||
            results[0].outcome != ItemOutcome::Failed || results[0].hresult != kSharingViolation ||
            results[0].destination_preexisted ||
            !fs::exists(destination / L"a.txt") || fs::exists(destination / L"b.txt") ||
            !fs::exists(destination / L"c.txt") ||
            last_total == 0 || last_transferred != last_total ||
            plan.unresolved_files() != 0) {
            fs::remove_all(root, ec);
            return 2;
        }
    }

    {
        const auto source = root / L"move-source";
        const auto destination = root / L"move-destination";
        write_text(source / L"a.txt", "AAAA");
        write_text(source / L"b.txt", "BBBB");
        write_text(source / L"c.txt", "CCCC");
        Handle lock;
        lock.value = CreateFileW((source / L"b.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock.value == INVALID_HANDLE_VALUE) { fs::remove_all(root, ec); return 3; }

        LiveCopyPlan plan(make_plan(source, destination, FileOperation::Move));
        ExecutionControl control;
        const auto result = executor.execute(plan, control, JobExecutionOptions{1}, {});

        const auto results = plan.retained_results();
        if (!result.success || result.cancelled || result.stopped ||
            result.outcomes.succeeded != 2 || result.outcomes.copied_source_retained != 1 ||
            result.outcomes.failed != 0 ||
            results.size() != 1 || results[0].outcome != ItemOutcome::CopiedSourceRetained ||
            !fs::exists(destination / L"b.txt") || !fs::exists(source / L"b.txt") ||
            fs::exists(source / L"a.txt") || fs::exists(source / L"c.txt") ||
            plan.resolution_view().counters.bytes_succeeded != 12) {
            fs::remove_all(root, ec);
            return 4;
        }
    }

    {
        const auto source = root / L"dir-source";
        const auto destination = root / L"dir-destination";
        write_text(source / L"ok.txt", "OKOK");
        write_text(source / L"blocked" / L"x.txt", "XXXX");
        write_text(destination / L"blocked", "a regular file where a directory is planned");

        CopyPlan raw{};
        raw.source_roots.push_back(source);
        raw.destination_root = destination;
        raw.directories.push_back({destination});
        raw.directories.push_back({destination / L"blocked"});
        raw.files.push_back({1, source / L"ok.txt", destination / L"ok.txt", 4});
        raw.files.push_back({2, source / L"blocked" / L"x.txt", destination / L"blocked" / L"x.txt", 4});
        raw.total_bytes = 8;
        raw.largest_file_bytes = 4;

        LiveCopyPlan plan(std::move(raw));
        ExecutionControl control;
        const auto result = executor.execute(plan, control, JobExecutionOptions{1}, {});

        const auto results = plan.retained_results();
        if (!result.success || result.cancelled ||
            result.outcomes.succeeded != 1 || result.outcomes.failed != 1 ||
            results.size() != 1 || results[0].file_id != 2 ||
            !fs::exists(destination / L"ok.txt") || !fs::is_regular_file(destination / L"blocked")) {
            fs::remove_all(root, ec);
            return 5;
        }
    }

    fs::remove_all(root, ec);
    return 0;
}
