#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

#include <winrt/Windows.Foundation.h>

namespace velocitycopy::ui {

enum class DecisionChoice : std::uint32_t { Cancel = 0, Primary = 1, Secondary = 2, Tertiary = 3 };
enum class DecisionTone : std::uint8_t { Neutral = 0, Warning = 1, Error = 2 };

struct DecisionOptions {
    HWND owner{};
    std::wstring title;
    std::wstring message;
    std::wstring detail;
    std::wstring primary_label;
    std::wstring secondary_label;
    std::wstring cancel_label;
    std::wstring verification_label;
    bool include_cancel{};
    DecisionTone tone{DecisionTone::Neutral};
    // Optional third action shown between Secondary and Cancel.
    std::wstring tertiary_label;
};

struct DecisionResult {
    DecisionChoice choice{DecisionChoice::Cancel};
    bool verification_checked{};
};

[[nodiscard]] constexpr std::uint32_t encode_decision(const DecisionResult result) noexcept {
    return static_cast<std::uint32_t>(result.choice) | (result.verification_checked ? 0x100u : 0u);
}

[[nodiscard]] constexpr DecisionResult decode_decision(const std::uint32_t encoded) noexcept {
    return {static_cast<DecisionChoice>(encoded & 0xffu), (encoded & 0x100u) != 0};
}

// Non-blocking WinUI decision surface. Closing/cancelling the operation is Cancel
// and every invocation completes exactly once. Callers serialize requests per owner
// and cancel the returned operation before destroying that owner.
winrt::Windows::Foundation::IAsyncOperation<std::uint32_t> show_decision_async(DecisionOptions options);
winrt::Windows::Foundation::IAsyncOperation<std::uint32_t> await_decision(
    winrt::Windows::Foundation::IAsyncOperation<std::uint32_t> operation);

} // namespace velocitycopy::ui
