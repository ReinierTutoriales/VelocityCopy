#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

#include <winrt/Windows.Foundation.h>

namespace velocitycopy::ui {

enum class DecisionChoice : std::int32_t { Cancel = 0, Primary = 1, Secondary = 2 };

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
};

struct DecisionResult {
    DecisionChoice choice{DecisionChoice::Cancel};
    bool verification_checked{};
};

// Non-blocking WinUI decision surface. Closing the surface is Cancel and every
// invocation completes exactly once. Callers serialize requests per owner.
winrt::Windows::Foundation::IAsyncOperation<std::int32_t> show_decision_async(
    const DecisionOptions& options,
    bool* verification_checked = nullptr);

} // namespace velocitycopy::ui
