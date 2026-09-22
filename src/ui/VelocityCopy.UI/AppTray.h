#pragma once

#include <windows.h>
#include <shellapi.h>

namespace winrt::VelocityCopyUI::implementation {
struct App;

class AppTray final {
public:
    AppTray() = default;
    ~AppTray();
    AppTray(const AppTray&) = delete;
    AppTray& operator=(const AppTray&) = delete;

    bool Initialize(App* owner) noexcept;
    void Remove() noexcept;
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    void ShowMenu(POINT anchor) noexcept;
    void RestoreIcon() noexcept;

    App* owner_{};
    HWND hwnd_{};
    HICON icon_{};
    NOTIFYICONDATAW data_{};
    UINT taskbar_created_message_{};
    bool added_{};
    bool v4_{};
};
}
