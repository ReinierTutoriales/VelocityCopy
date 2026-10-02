#include "velocitycopy/item_result.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\\n", __FILE__, __LINE__, #condition); \
            std::abort();                                                       \
        }                                                                       \
    } while (false)

int main() {
    using namespace velocitycopy;

    static_assert(item_resolution_weight(0) == kMinimumResolutionWeight);
    static_assert(item_resolution_weight(1) == 1);
    static_assert(item_resolution_weight(50ull * 1024 * 1024) == 50ull * 1024 * 1024);

    static_assert(is_valid_item_transition(ItemState::Pending, ItemState::Active));
    static_assert(is_valid_item_transition(ItemState::Pending, ItemState::Terminal));
    static_assert(is_valid_item_transition(ItemState::Active, ItemState::Pending));
    static_assert(is_valid_item_transition(ItemState::Active, ItemState::Parked));
    static_assert(is_valid_item_transition(ItemState::Active, ItemState::Terminal));
    static_assert(is_valid_item_transition(ItemState::Parked, ItemState::Pending));
    static_assert(is_valid_item_transition(ItemState::Parked, ItemState::Terminal));
    static_assert(!is_valid_item_transition(ItemState::Pending, ItemState::Parked));
    static_assert(!is_valid_item_transition(ItemState::Terminal, ItemState::Pending));

    static_assert(is_successful_transfer(ItemOutcome::Succeeded));
    static_assert(is_successful_transfer(ItemOutcome::CopiedSourceRetained));
    static_assert(!is_successful_transfer(ItemOutcome::Skipped));
    static_assert(!is_successful_transfer(ItemOutcome::Failed));

    constexpr std::uint64_t size = 100;
    static_assert(resolution_high_water(0, size, 25, false) == 25);
    static_assert(resolution_high_water(25, size, 10, false) == 25);
    static_assert(resolution_high_water(25, size, 80, false) == 80);
    static_assert(resolution_high_water(80, size, 0, true) == 100);
    static_assert(resolution_high_water(0, 0, 0, true) == kMinimumResolutionWeight);

    ItemResult result{};
    result.file_id = 7;
    result.outcome = ItemOutcome::Skipped;
    result.hresult = 0;
    result.destination_preexisted = true;
    CHECK(result.file_id == 7);
    CHECK(result.outcome == ItemOutcome::Skipped);
    CHECK(result.destination_preexisted);

    ItemIncident incident{};
    incident.file_id = 9;
    incident.hresult = -1;
    incident.destination_preexisted = false;
    incident.recovery_action = RecoveryAction::RetrySourceRemoval;
    incident.attempt_count = 3;
    CHECK(incident.file_id == 9);
    CHECK(incident.hresult == -1);
    CHECK(incident.recovery_action == RecoveryAction::RetrySourceRemoval);
    CHECK(incident.attempt_count == 3);

    TransferCounters counters{};
    counters.bytes_written_physical = 150;
    counters.bytes_succeeded = 100;
    counters.resolution_weight = 100;
    counters.resolution_total = 100;
    CHECK(counters.bytes_written_physical >= counters.bytes_succeeded);
    CHECK(counters.resolution_weight <= counters.resolution_total);

    return 0;
}
