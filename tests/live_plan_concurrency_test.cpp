#include "velocitycopy/live_copy_plan.hpp"
#include <algorithm>
#include <atomic>
#include <barrier>
#include <filesystem>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {
constexpr std::uint64_t initial_count = 20000, appended_count = 2000;
constexpr auto total_count = initial_count + appended_count;

velocitycopy::CopyPlan batch(std::uint64_t start, std::uint64_t count) {
    velocitycopy::CopyPlan plan;
    plan.destination_root = L"C:/VelocityCopyConcurrency/destination";
    plan.files.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        const auto name = std::to_wstring(start + i) + L".bin";
        plan.files.push_back({i + 1, std::filesystem::path(L"C:/VelocityCopyConcurrency/source") / name,
                              plan.destination_root / name, 8});
    }
    plan.total_bytes = count * 8;
    plan.largest_file_bytes = 8;
    return plan;
}
}

int main() {
    using namespace velocitycopy;
    LiveCopyPlan plan(batch(1, initial_count));
    std::vector<std::atomic_uint> seen(total_count + 1);
    std::atomic_bool producer_done{}, valid{true};
    std::atomic_uint workers_done{}, snapshots{};
    std::barrier ready(6);
    std::vector<std::jthread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&] {
            ready.arrive_and_wait();
            for (;;) {
                const auto file = plan.acquire_next();
                if (!file) {
                    if (producer_done.load() && plan.remaining_files() == 0) break;
                    std::this_thread::yield();
                    continue;
                }
                if (file->id > total_count || seen[file->id].fetch_add(1) != 0) valid = false;
                plan.record_attempt_bytes(file->id, 4);
                plan.record_attempt_bytes(file->id, 8);
                plan.complete_active(file->id);
            }
            ++workers_done;
        });
    }
    std::jthread producer([&] {
        ready.arrive_and_wait();
        for (std::uint64_t i = 0; i < appended_count; i += 500) {
            if (plan.append(batch(initial_count + i + 1, 500), true) != LivePlanAppendResult::Appended) valid = false;
        }
        producer_done = true;
    });
    std::jthread observer([&] {
        ready.arrive_and_wait();
        do {
            const auto offset = (snapshots.load() % 4) * 256;
            const auto view = plan.queue_view(256, offset);
            const auto active = plan.active_queue_view(8);
            if (view.pending_files.size() > 256 || active.active_count > 4 || active.active_files.size() > 4) valid = false;
            std::vector<std::uint64_t> ids;
            std::unordered_set<std::uint64_t> unique;
            for (const auto& file : view.pending_files) {
                if (!unique.insert(file.id).second) valid = false;
                ids.push_back(file.id);
            }
            std::reverse(ids.begin(), ids.end());
            // False is legitimate when acquisition makes the page stale.
            (void)plan.reorder_pending_page(offset, ids);
            ++snapshots;
        } while (workers_done.load() != 4);
    });
    producer.join();
    for (auto& worker : workers) worker.join();
    observer.join();
    if (!valid || !snapshots || plan.total_files() != total_count ||
        plan.completed_files() != total_count || plan.remaining_files() ||
        plan.completed_bytes() != total_count * 8 || !plan.retained_results().empty()) return 1;
    for (std::uint64_t id = 1; id <= total_count; ++id) if (seen[id] != 1) return 2;
    return 0;
}
