#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/live_copy_plan.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

bool create_file_with_size(const std::filesystem::path& path, const std::uint64_t size) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }

    std::vector<char> block(1024 * 1024, 'V');
    std::uint64_t remaining = size;
    while (remaining != 0) {
        const auto count = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, block.size()));
        stream.write(block.data(), count);
        if (!stream) {
            return false;
        }
        remaining -= static_cast<std::uint64_t>(count);
    }
    return true;
}

} // namespace

int wmain() {
    namespace fs = std::filesystem;
    using namespace velocitycopy;

    const auto root = fs::temp_directory_path() / L"VelocityCopyParallelExecutorTest";
    const auto source = root / L"Source";
    const auto destination = root / L"Destination";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(source, ec);
    if (ec) {
        return 1;
    }

    constexpr std::uint64_t file_size = 4ull * 1024ull * 1024ull;
    CopyPlan static_plan{};
    static_plan.directories.push_back({destination});
    static_plan.source_roots.push_back(source);
    static_plan.destination_root = destination;
    static_plan.largest_file_bytes = file_size;

    for (std::uint64_t index = 0; index < 4; ++index) {
        const auto name = L"file" + std::to_wstring(index) + L".bin";
        const auto source_file = source / name;
        const auto destination_file = destination / name;
        if (!create_file_with_size(source_file, file_size)) {
            fs::remove_all(root, ec);
            return 2;
        }
        static_plan.files.push_back({index + 1, source_file, destination_file, file_size});
        static_plan.total_bytes += file_size;
    }

    LiveCopyPlan plan(std::move(static_plan));
    ExecutionControl control;
    JobExecutor executor;
    std::atomic_bool saw_multiple_active{false};
    std::atomic_bool inspected{false};

    const auto result = executor.execute(
        plan,
        control,
        JobExecutionOptions{2},
        [&](const JobProgress&) {
            if (!inspected.exchange(true, std::memory_order_acq_rel)) {
                // The callback itself is serialized. Keep the first worker inside
                // it briefly so the second worker can acquire/start another file.
                for (int attempt = 0; attempt < 500; ++attempt) {
                    if (plan.snapshot().active_files.size() >= 2) {
                        saw_multiple_active.store(true, std::memory_order_release);
                        break;
                    }
                    std::this_thread::sleep_for(1ms);
                }
            }
            return JobDecision::Continue;
        });

    if (!result.success || result.cancelled || result.stopped ||
        !saw_multiple_active.load(std::memory_order_acquire)) {
        fs::remove_all(root, ec);
        return 3;
    }

    const auto final_snapshot = plan.snapshot();
    if (!final_snapshot.pending_files.empty() || !final_snapshot.active_files.empty()) {
        fs::remove_all(root, ec);
        return 4;
    }

    for (std::uint64_t index = 0; index < 4; ++index) {
        const auto target = destination / (L"file" + std::to_wstring(index) + L".bin");
        if (!fs::exists(target, ec) || ec || fs::file_size(target, ec) != file_size || ec) {
            fs::remove_all(root, ec);
            return 5;
        }
    }

    fs::remove_all(root, ec);
    return 0;
}
