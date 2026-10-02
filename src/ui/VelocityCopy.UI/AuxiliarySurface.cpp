#include "pch.h"
#include "AuxiliarySurface.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace velocitycopy::ui {
namespace {

struct TaskDialogThemeContext {
    BOOL dark_mode{};
};

HRESULT CALLBACK task_dialog_theme_callback(
    HWND hwnd,
    UINT notification,
    WPARAM,
    LPARAM,
    LONG_PTR callback_data) noexcept {
    if (notification != TDN_CREATED) return S_OK;

    const auto* context = reinterpret_cast<const TaskDialogThemeContext*>(callback_data);
    if (context == nullptr) return S_OK;

    (void)DwmSetWindowAttribute(
        hwnd,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &context->dark_mode,
        sizeof(context->dark_mode));

    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    (void)DwmSetWindowAttribute(
        hwnd,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &corner,
        sizeof(corner));

    (void)SetWindowTheme(
        hwnd,
        context->dark_mode ? L"DarkMode_Explorer" : L"Explorer",
        nullptr);
    return S_OK;
}

} // namespace

NativeDecision show_native_decision(
    const NativeDecisionOptions& options,
    bool* verification_checked) noexcept {
    if (verification_checked != nullptr) *verification_checked = false;

    constexpr int kPrimary = 1001;
    constexpr int kSecondary = 1002;

    try {
        HMODULE module = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module != nullptr) {
            using TaskDialogIndirectFn = HRESULT (WINAPI*)(
                const TASKDIALOGCONFIG*, int*, int*, BOOL*);
            const auto task_dialog = reinterpret_cast<TaskDialogIndirectFn>(
                GetProcAddress(module, "TaskDialogIndirect"));

            if (task_dialog != nullptr) {
                TASKDIALOG_BUTTON buttons[] = {
                    {kPrimary, options.primary_label.c_str()},
                    {kSecondary, options.secondary_label.c_str()},
                    {IDCANCEL, options.cancel_label.c_str()},
                };

                TaskDialogThemeContext theme{};
                if (options.owner != nullptr) {
                    (void)DwmGetWindowAttribute(
                        options.owner,
                        DWMWA_USE_IMMERSIVE_DARK_MODE,
                        &theme.dark_mode,
                        sizeof(theme.dark_mode));
                }

                TASKDIALOGCONFIG config{};
                config.cbSize = sizeof(config);
                config.hwndParent = options.owner;
                config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
                config.cxWidth = verification_checked != nullptr ? 240 : 260;
                config.pszWindowTitle = L"VelocityCopy";
                config.pszMainInstruction = options.title.c_str();
                config.pszContent = options.message.c_str();
                config.cButtons = options.include_cancel ? 3u : 2u;
                config.pButtons = buttons;
                config.nDefaultButton = kPrimary;
                config.pfCallback = &task_dialog_theme_callback;
                config.lpCallbackData = reinterpret_cast<LONG_PTR>(&theme);
                if (verification_checked != nullptr) {
                    config.pszVerificationText = options.verification_label.c_str();
                }

                int selected = IDCANCEL;
                BOOL checked = FALSE;
                const HRESULT hr = task_dialog(
                    &config,
                    &selected,
                    nullptr,
                    verification_checked != nullptr ? &checked : nullptr);
                FreeLibrary(module);

                if (SUCCEEDED(hr)) {
                    if (verification_checked != nullptr) *verification_checked = checked != FALSE;
                    if (selected == kPrimary) return NativeDecision::Primary;
                    if (selected == kSecondary) return NativeDecision::Secondary;
                    return NativeDecision::Cancel;
                }
            } else {
                FreeLibrary(module);
            }
        }

        const UINT flags = options.include_cancel
            ? (MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1)
            : (MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1);
        const int fallback = MessageBoxW(
            options.owner,
            options.message.c_str(),
            options.title.c_str(),
            flags);
        if (fallback == IDYES) return NativeDecision::Primary;
        if (fallback == IDNO) return NativeDecision::Secondary;
    } catch (...) {
    }

    return NativeDecision::Cancel;
}

} // namespace velocitycopy::ui
