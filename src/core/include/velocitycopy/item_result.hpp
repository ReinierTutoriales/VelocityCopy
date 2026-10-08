#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>

namespace velocitycopy {

enum class ItemOutcome {
    Succeeded,
    Skipped,
    Failed,
    CopiedSourceRetained,
};

enum class ItemState {
    Pending,
    Active,
    Parked,
    Terminal,
};

struct ItemResult {
    std::uint64_t file_id{};
    ItemOutcome outcome{ItemOutcome::Succeeded};
    std::int32_t hresult{};
    std::filesystem::path source;
    std::filesystem::path destination;
    bool destination_preexisted{};
};

enum class RecoveryAction {
    RetryTransfer,
    RetrySourceRemoval,
};

struct ItemIncident {
    std::uint64_t file_id{};
    std::int32_t hresult{};
    std::filesystem::path source;
    std::filesystem::path destination;
    bool destination_preexisted{};
    RecoveryAction recovery_action{RecoveryAction::RetryTransfer};
    std::uint32_t attempt_count{1};
};

struct TransferCounters {
    std::uint64_t bytes_written_physical{};
    std::uint64_t bytes_succeeded{};
    std::uint64_t resolution_weight{};
    std::uint64_t resolution_total{};
};

inline constexpr std::uint64_t kMinimumResolutionWeight = 1;

[[nodiscard]] constexpr std::uint64_t item_resolution_weight(std::uint64_t size_bytes) noexcept {
    return std::max(size_bytes, kMinimumResolutionWeight);
}

[[nodiscard]] constexpr bool is_successful_transfer(ItemOutcome outcome) noexcept {
    return outcome == ItemOutcome::Succeeded || outcome == ItemOutcome::CopiedSourceRetained;
}

[[nodiscard]] constexpr bool is_valid_item_transition(ItemState from, ItemState to) noexcept {
    switch (from) {
    case ItemState::Pending:
        return to == ItemState::Active || to == ItemState::Terminal;
    case ItemState::Active:
        return to == ItemState::Pending || to == ItemState::Parked || to == ItemState::Terminal;
    case ItemState::Parked:
        return to == ItemState::Pending || to == ItemState::Terminal;
    case ItemState::Terminal:
        return false;
    }
    return false;
}

// Progress is monotonic per item. Active work contributes bytes transferred up
// to the item's full weight; terminal work contributes its full weight. A retry
// may restart physical I/O at zero without moving the visible bar backwards.
[[nodiscard]] constexpr std::uint64_t resolution_high_water(
    std::uint64_t previous_high_water,
    std::uint64_t size_bytes,
    std::uint64_t current_attempt_bytes,
    bool terminal) noexcept {
    const auto weight = item_resolution_weight(size_bytes);
    const auto current = terminal ? weight : std::min(current_attempt_bytes, weight);
    return std::max(previous_high_water, current);
}

// Removing a Pending item is a plan edit: it removes that item's weight from
// resolution_total and creates no ItemResult. Skipping is a terminal resolution:
// it preserves the denominator and contributes the item's full weight.
//
// Cancellation is not a terminal item transition. Pending/Parked items remain
// unprocessed and must be reported separately by the session summary.
//
// Queue archive v3 persists RetrySourceRemoval separately from CopyPlan.
// RetryTransfer remains restartable transfer work; terminal results are not
// persisted as unresolved work.

} // namespace velocitycopy
