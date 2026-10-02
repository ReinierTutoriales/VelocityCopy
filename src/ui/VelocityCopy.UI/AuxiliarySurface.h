#pragma once

#include <windows.h>

#include <string>

namespace velocitycopy::ui {

enum class NativeDecision : unsigned char {
    Cancel,
    Primary,
    Secondary,
};

struct NativeDecisionOptions {
    HWND owner{};
    std::wstring title;
    std::wstring message;
    std::wstring primary_label;
    std::wstring secondary_label;
    std::wstring cancel_label;
    std::wstring verification_label;
    bool include_cancel{};
};

[[nodiscard]] NativeDecision show_native_decision(
    const NativeDecisionOptions& options,
    bool* verification_checked = nullptr) noexcept;

} // namespace velocitycopy::ui
