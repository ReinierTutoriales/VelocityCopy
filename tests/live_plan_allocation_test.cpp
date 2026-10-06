#include "velocitycopy/live_copy_plan.hpp"

#include <cstdlib>
#include <iostream>
#include <new>
#include <string>

namespace { thread_local int fail_after = -1; }
void* operator new(std::size_t bytes) {
    if (fail_after == 0) { fail_after = -1; throw std::bad_alloc{}; }
    if (fail_after > 0) --fail_after;
    if (auto p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace velocitycopy;
CopyPlan make_plan(int first, int count) {
    CopyPlan plan;
    plan.destination_root = "C:/destination";
    const std::string long_name(80, 'x');
    plan.source_roots.emplace_back("C:/source/" + long_name + std::to_string(first));
    plan.directories.push_back({plan.destination_root / (long_name + std::to_string(first))});
    for (int i = first; i < first + count; ++i) {
        const auto name = long_name + std::to_string(i);
        plan.files.push_back({static_cast<std::uint64_t>(i + 1), "C:/source/" + name,
                              plan.destination_root / name, 11});
    }
    plan.total_bytes = static_cast<std::uint64_t>(count) * 11;
    plan.largest_file_bytes = 11;
    return plan;
}

int main() {
    int failed_appends = 0;
    bool succeeded = false;
    for (int allocation = 0; allocation < 600; ++allocation) {
        LiveCopyPlan live(make_plan(0, 1));
        auto incoming = make_plan(1, 40);
        const auto before = live.export_remaining_plan();
        fail_after = allocation;
        const auto result = live.append(std::move(incoming));
        fail_after = -1;
        if (result == LivePlanAppendResult::InternalFailure) {
            ++failed_appends;
            const auto after = live.export_remaining_plan();
            if (after.files.size() != before.files.size() ||
                after.directories.size() != before.directories.size() ||
                after.source_roots != before.source_roots || after.total_bytes != before.total_bytes ||
                live.total_files() != 1 || live.resolution_view().counters.resolution_total != 11) {
                std::cerr << "Failed append changed live state at allocation " << allocation << '\n';
                return 1;
            }
            if (live.append(make_plan(1, 40)) != LivePlanAppendResult::Appended ||
                live.total_files() != 41 || live.remaining_files() != 41) return 2;
        } else if (result == LivePlanAppendResult::Appended) {
            succeeded = true;
            if (live.total_files() != 41 || live.remaining_files() != 41) return 3;
            break;
        } else return 4;
    }
    if (failed_appends < 10 || !succeeded) return 5;

    // A failed acquisition must retain the item in Pending, and be retryable.
    int failed_acquires = 0;
    for (int allocation = 0; allocation < 20; ++allocation) {
        LiveCopyPlan live(make_plan(0, 1));
        bool failed = false;
        fail_after = allocation;
        try { (void)live.acquire_next(); } catch (const std::bad_alloc&) { failed = true; }
        fail_after = -1;
        if (!failed) break;
        ++failed_acquires;
        const auto state = live.snapshot();
        if (state.pending_files.size() != 1 || !state.active_files.empty() || !live.acquire_next()) return 6;
    }
    if (!failed_acquires) return 7;

    LiveCopyPlan release_plan(make_plan(0, 64));
    while (release_plan.acquire_next()) {}
    int failed_releases = 0;
    for (std::uint64_t id = 1; id <= 64; ++id) {
        const auto before = release_plan.snapshot();
        bool failed = false;
        fail_after = 0;
        try { release_plan.release_active(id); } catch (const std::bad_alloc&) { failed = true; }
        fail_after = -1;
        if (failed) {
            ++failed_releases;
            const auto after = release_plan.snapshot();
            if (after.active_files.size() != before.active_files.size() ||
                after.pending_files.size() != before.pending_files.size()) return 8;
            release_plan.release_active(id);
        }
    }
    // MSVC deque retains spare blocks, so returning previously acquired items
    // may need no allocation. Other implementations exercise the rollback path.
    if (release_plan.remaining_files() != 64) return 9;

    SourceRemovalRecovery archived;
    archived.source = make_plan(0, 1).files[0].source;
    archived.destination = make_plan(0, 1).files[0].destination;
    archived.attempt_count = 1;
    archived.source_fingerprint.size = 11;
    int failed_restores = 0;
    for (int allocation = 0; allocation < 100; ++allocation) {
        CopyPlan empty;
        empty.destination_root = "C:/destination";
        LiveCopyPlan live(std::move(empty));
        fail_after = allocation;
        const bool restored = live.restore_parked_source_removal(archived);
        fail_after = -1;
        if (restored) break;
        ++failed_restores;
        const auto view = live.resolution_view();
        if (live.total_files() || view.counters.resolution_total || view.counters.resolution_weight ||
            !live.parked_source_removals().empty() || !live.restore_parked_source_removal(archived)) return 10;
    }
    if (failed_restores < 5) return 11;

    int failed_unparks = 0;
    for (int allocation = 0; allocation < 30; ++allocation) {
        LiveCopyPlan live(make_plan(0, 1));
        const auto file = live.acquire_next();
        if (!file || !live.park_active(file->id, -1, false, RecoveryAction::RetryTransfer)) return 12;
        fail_after = allocation;
        const bool unparked = live.unpark(file->id);
        fail_after = -1;
        if (unparked) break;
        ++failed_unparks;
        const auto view = live.queue_view(256);
        if (view.pending_count || view.parked_count != 1 || !live.unpark(file->id)) return 13;
    }
    if (!failed_unparks) return 14;

    LiveCopyPlan wrappers(make_plan(0, 2));
    fail_after = 0;
    const bool up = wrappers.move_pending_file_up(2);
    fail_after = 0;
    const bool down = wrappers.move_pending_file_down(1);
    fail_after = 0;
    const bool removed = wrappers.remove_pending_file(1);
    fail_after = -1;
    if (up || down || removed || wrappers.remaining_files() != 2) return 15;
    std::cout << "Live-plan allocation failures preserve state and allow retry\n";
    return 0;
}
