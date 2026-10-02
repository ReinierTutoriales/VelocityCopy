#include "velocitycopy/live_copy_plan.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

// CI builds Release (NDEBUG), which compiles assert() out. Checks must stay live.
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            std::abort();                                                       \
        }                                                                       \
    } while (false)

namespace {

using namespace velocitycopy;

constexpr std::int32_t kAccessDenied = static_cast<std::int32_t>(0x80070005);
constexpr std::int32_t kSharingViolation = static_cast<std::int32_t>(0x80070020);

CopyPlan make_plan() {
    CopyPlan plan{};
    plan.source_roots.push_back(L"C:/src");
    plan.destination_root = L"D:/dst";
    plan.directories.push_back({L"D:/dst"});
    plan.files.push_back({1, L"C:/src/a.bin", L"D:/dst/a.bin", 100});
    plan.files.push_back({2, L"C:/src/b.bin", L"D:/dst/b.bin", 50});
    plan.files.push_back({3, L"C:/src/empty.txt", L"D:/dst/empty.txt", 0});
    plan.files.push_back({4, L"C:/src/c.bin", L"D:/dst/c.bin", 30});
    plan.total_bytes = 180;
    plan.largest_file_bytes = 100;
    return plan;
}

constexpr std::uint64_t kTotalWeight = 181;

CopyPlan make_single(const wchar_t* destination, std::uint64_t size) {
    CopyPlan plan{};
    plan.source_roots.push_back(L"C:/more");
    plan.destination_root = L"D:/dst";
    plan.files.push_back({1, L"C:/more/x.bin", destination, size});
    plan.total_bytes = size;
    plan.largest_file_bytes = size;
    return plan;
}

void resolution_total_counts_every_item_with_minimum_weight() {
    LiveCopyPlan plan{make_plan()};
    const auto view = plan.resolution_view();
    CHECK(view.counters.resolution_total == kTotalWeight);
    CHECK(view.counters.resolution_weight == 0);
    CHECK(view.unresolved_files() == 4);
}

void succeeded_is_counters_only_and_keeps_reservation() {
    LiveCopyPlan plan{make_plan()};
    const auto file = plan.acquire_next();
    CHECK(file && file->id == 1);
    plan.record_attempt_bytes(1, 40);
    CHECK(plan.resolve_active(1, ItemOutcome::Succeeded, 0, false));

    const auto view = plan.resolution_view();
    CHECK(view.outcomes.succeeded == 1);
    CHECK(view.counters.bytes_succeeded == 100);
    CHECK(view.counters.resolution_weight == 100);
    CHECK(view.counters.bytes_written_physical == 40);
    CHECK(plan.retained_results().empty());
    CHECK(plan.completed_bytes() == 100);
    CHECK(plan.completed_files() == 1);
    CHECK(plan.append(make_single(L"D:/dst/a.bin", 1)) == LivePlanAppendResult::DestinationCollision);
}

void skipped_and_failed_keep_denominator_retain_result_and_release_destination() {
    LiveCopyPlan plan{make_plan()};
    (void)plan.acquire_next();
    plan.record_attempt_bytes(1, 60);
    CHECK(plan.resolve_active(1, ItemOutcome::Failed, kAccessDenied, true));
    (void)plan.acquire_next();
    CHECK(plan.resolve_active(2, ItemOutcome::Skipped, 0, false));

    const auto view = plan.resolution_view();
    CHECK(view.counters.resolution_total == kTotalWeight);
    CHECK(view.counters.resolution_weight == 150);
    CHECK(view.counters.bytes_succeeded == 0);
    CHECK(view.counters.bytes_written_physical == 60);
    CHECK(view.outcomes.failed == 1 && view.outcomes.skipped == 1);
    CHECK(plan.completed_bytes() == 0);

    const auto results = plan.retained_results();
    CHECK(results.size() == 2);
    CHECK(results[0].file_id == 1 && results[0].outcome == ItemOutcome::Failed);
    CHECK(results[0].hresult == kAccessDenied && results[0].destination_preexisted);
    CHECK(results[0].destination == std::filesystem::path(L"D:/dst/a.bin"));
    CHECK(results[1].outcome == ItemOutcome::Skipped);

    CHECK(plan.append(make_single(L"D:/dst/a.bin", 1)) == LivePlanAppendResult::Appended);
}

void copied_source_retained_is_a_successful_transfer_with_retained_result() {
    LiveCopyPlan plan{make_plan()};
    (void)plan.acquire_next();
    CHECK(plan.resolve_active(1, ItemOutcome::CopiedSourceRetained, kSharingViolation, false));
    const auto view = plan.resolution_view();
    CHECK(view.counters.bytes_succeeded == 100);
    CHECK(view.outcomes.copied_source_retained == 1);
    CHECK(plan.retained_results().size() == 1);
    CHECK(plan.append(make_single(L"D:/dst/a.bin", 1)) == LivePlanAppendResult::DestinationCollision);
}

void parked_keeps_session_alive_and_is_never_acquired() {
    LiveCopyPlan plan{make_plan()};
    (void)plan.acquire_next();
    CHECK(plan.park_active(1, kSharingViolation, false));
    CHECK(!plan.park_active(1, kSharingViolation, false));

    const auto incidents = plan.parked_incidents();
    CHECK(incidents.size() == 1 && incidents[0].file_id == 1);
    CHECK(incidents[0].hresult == kSharingViolation);

    for (std::uint64_t id : {2u, 3u, 4u}) {
        const auto file = plan.acquire_next();
        CHECK(file && file->id == id);
        CHECK(plan.resolve_active(id, ItemOutcome::Succeeded, 0, false));
    }
    CHECK(!plan.acquire_next());
    CHECK(plan.remaining_files() == 0);
    CHECK(plan.unresolved_files() == 1);
    CHECK(plan.resolution_view().parked_files == 1);
    CHECK(plan.queue_view(10).parked_count == 1);
    CHECK(plan.append(make_single(L"D:/dst/new.bin", 5)) == LivePlanAppendResult::Appended);
}

void retry_never_moves_visible_progress_backwards() {
    LiveCopyPlan plan{make_plan()};
    (void)plan.acquire_next();
    plan.record_attempt_bytes(1, 70);
    CHECK(plan.resolution_view().counters.resolution_weight == 70);
    CHECK(plan.park_active(1, kSharingViolation, false));
    CHECK(plan.unpark(1));

    const auto again = plan.acquire_next();
    CHECK(again && again->id == 1);
    plan.record_attempt_bytes(1, 20);
    auto view = plan.resolution_view();
    CHECK(view.counters.resolution_weight == 70);
    CHECK(view.counters.bytes_written_physical == 90);
    plan.record_attempt_bytes(1, 95);
    view = plan.resolution_view();
    CHECK(view.counters.resolution_weight == 95);
    CHECK(view.counters.bytes_written_physical == 165);
    CHECK(plan.resolve_active(1, ItemOutcome::Succeeded, 0, false));
    CHECK(plan.resolution_view().counters.resolution_weight == 100);
}

void retry_attempt_count_advances_across_unpark() {
    LiveCopyPlan plan{make_plan()};
    (void)plan.acquire_next();
    CHECK(plan.park_active(1, kSharingViolation, false, RecoveryAction::RetryTransfer));
    auto incidents = plan.parked_incidents();
    CHECK(incidents.size() == 1 && incidents[0].attempt_count == 1);
    CHECK(plan.unpark(1));
    const auto retry = plan.acquire_next();
    CHECK(retry && retry->id == 1);
    CHECK(plan.park_active(1, kAccessDenied, false, RecoveryAction::RetryTransfer));
    incidents = plan.parked_incidents();
    CHECK(incidents.size() == 1);
    CHECK(incidents[0].hresult == kAccessDenied);
    CHECK(incidents[0].attempt_count == 2);
}

void source_removal_retry_attempt_advances_while_parked() {
    LiveCopyPlan plan{make_plan()};
    (void)plan.acquire_next();
    CHECK(plan.park_active(1, kAccessDenied, true, RecoveryAction::RetrySourceRemoval));
    auto incidents = plan.parked_incidents();
    CHECK(incidents.size() == 1 && incidents[0].attempt_count == 1);
    CHECK(plan.begin_parked_retry(1, RecoveryAction::RetrySourceRemoval));
    CHECK(plan.record_parked_retry_failure(1, kSharingViolation));
    incidents = plan.parked_incidents();
    CHECK(incidents.size() == 1 && incidents[0].attempt_count == 2);
    CHECK(incidents[0].hresult == kSharingViolation);
    CHECK(incidents[0].recovery_action == RecoveryAction::RetrySourceRemoval);
    CHECK(!plan.begin_parked_retry(1, RecoveryAction::RetryTransfer));
}

void pending_can_resolve_terminally_without_becoming_active() {
    LiveCopyPlan plan{make_plan()};
    CHECK(plan.resolve_pending(4, ItemOutcome::Failed, kAccessDenied, false));
    CHECK(!plan.resolve_pending(4, ItemOutcome::Failed, kAccessDenied, false));
    const auto view = plan.resolution_view();
    CHECK(view.outcomes.failed == 1);
    CHECK(view.counters.resolution_weight == 30);
    CHECK(view.pending_files == 3 && view.active_files == 0);
}

void removing_pending_is_a_plan_edit() {
    LiveCopyPlan plan{make_plan()};
    CHECK(plan.remove_pending_file(2));
    const auto view = plan.resolution_view();
    CHECK(view.counters.resolution_total == kTotalWeight - 50);
    CHECK(view.outcomes.skipped == 0);
    CHECK(plan.retained_results().empty());
}

void invalid_transitions_are_rejected() {
    LiveCopyPlan plan{make_plan()};
    CHECK(!plan.resolve_active(1, ItemOutcome::Succeeded, 0, false));
    CHECK(!plan.park_active(1, 0, false));
    CHECK(!plan.unpark(1));
    CHECK(!plan.resolve_parked(1, ItemOutcome::Skipped, 0, false));
    (void)plan.acquire_next();
    CHECK(plan.resolve_active(1, ItemOutcome::Succeeded, 0, false));
    CHECK(!plan.resolve_active(1, ItemOutcome::Succeeded, 0, false));
    CHECK(!plan.unpark(1));
    plan.record_attempt_bytes(1, 10);
    CHECK(plan.resolution_view().counters.bytes_written_physical == 0);
}

void parked_items_export_as_pending() {
    LiveCopyPlan plan{make_plan()};
    (void)plan.acquire_next();
    CHECK(plan.park_active(1, kSharingViolation, false));
    (void)plan.acquire_next();
    const auto exported = plan.export_remaining_plan();
    CHECK(exported.files.size() == 4);
    CHECK(exported.files[0].source == std::filesystem::path(L"C:/src/b.bin"));
    CHECK(exported.files[1].source == std::filesystem::path(L"C:/src/a.bin"));
}

void source_removal_retry_is_never_exported_as_transfer_work() {
    LiveCopyPlan plan{make_plan()};
    (void)plan.acquire_next();
    CHECK(plan.park_active(1, kSharingViolation, false, RecoveryAction::RetrySourceRemoval));
    (void)plan.acquire_next();
    CHECK(plan.park_active(2, kSharingViolation, false, RecoveryAction::RetryTransfer));
    const auto exported = plan.export_remaining_plan();
    bool has_source_removal = false;
    bool has_transfer_retry = false;
    for (const auto& file : exported.files) {
        if (file.source == std::filesystem::path(L"C:/src/a.bin")) has_source_removal = true;
        if (file.source == std::filesystem::path(L"C:/src/b.bin")) has_transfer_retry = true;
    }
    CHECK(!has_source_removal);  // completed copy must not be re-queued as a Move
    CHECK(has_transfer_retry);
    CHECK(exported.files.size() == 3);
}

void full_resolution_reaches_total() {
    LiveCopyPlan plan{make_plan()};
    (void)plan.acquire_next();
    CHECK(plan.resolve_active(1, ItemOutcome::Succeeded, 0, false));
    (void)plan.acquire_next();
    CHECK(plan.park_active(2, kSharingViolation, false));
    CHECK(plan.resolve_parked(2, ItemOutcome::Skipped, 0, false));
    CHECK(plan.resolve_pending(3, ItemOutcome::Failed, kAccessDenied, false));
    (void)plan.acquire_next();
    CHECK(plan.resolve_active(4, ItemOutcome::CopiedSourceRetained, kSharingViolation, false));

    const auto view = plan.resolution_view();
    CHECK(view.counters.resolution_weight == view.counters.resolution_total);
    CHECK(view.unresolved_files() == 0);
    CHECK(view.outcomes.succeeded == 1 && view.outcomes.skipped == 1);
    CHECK(view.outcomes.failed == 1 && view.outcomes.copied_source_retained == 1);
    CHECK(view.counters.bytes_succeeded == 130);
    CHECK(plan.retained_results().size() == 3);
}

} // namespace

int main() {
    resolution_total_counts_every_item_with_minimum_weight();
    succeeded_is_counters_only_and_keeps_reservation();
    skipped_and_failed_keep_denominator_retain_result_and_release_destination();
    copied_source_retained_is_a_successful_transfer_with_retained_result();
    parked_keeps_session_alive_and_is_never_acquired();
    retry_never_moves_visible_progress_backwards();
    retry_attempt_count_advances_across_unpark();
    source_removal_retry_attempt_advances_while_parked();
    pending_can_resolve_terminally_without_becoming_active();
    removing_pending_is_a_plan_edit();
    invalid_transitions_are_rejected();
    parked_items_export_as_pending();
    full_resolution_reaches_total();
    source_removal_retry_is_never_exported_as_transfer_work();
    return 0;
}
