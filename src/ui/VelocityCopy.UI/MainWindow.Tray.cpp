#include "pch.h"
#include "MainWindow.xaml.h"

#include <commctrl.h>
#include <exdisp.h>
#include <servprov.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shobjidl_core.h>

using namespace winrt;

namespace winrt::VelocityCopyUI::implementation {
namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 0x51;
constexpr UINT_PTR kTraySubclassId = 0x56434F50;
constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayOpenCommand = 1;
constexpr UINT kTrayExitCommand = 2;

MainWindow* g_explorer_keyboard_owner = nullptr;

bool is_explorer_process(HWND window) noexcept {
    if (window == nullptr) return false;

    DWORD process_id{};
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == 0) return false;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == nullptr) return false;

    std::array<wchar_t, 32768> image{};
    DWORD size = static_cast<DWORD>(image.size());
    const bool have_image = QueryFullProcessImageNameW(process, 0, image.data(), &size) != FALSE;
    CloseHandle(process);
    if (!have_image || size == 0) return false;

    std::filesystem::path executable(std::wstring_view(image.data(), size));
    auto name = executable.filename().wstring();
    std::transform(name.begin(), name.end(), name.begin(), [](const wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return name == L"explorer.exe";
}

bool focus_is_text_input(HWND foreground) noexcept {
    if (foreground == nullptr) return false;

    const DWORD thread_id = GetWindowThreadProcessId(foreground, nullptr);
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (!GetGUIThreadInfo(thread_id, &info) || info.hwndFocus == nullptr) {
        return false;
    }

    std::array<wchar_t, 256> class_name{};
    if (GetClassNameW(info.hwndFocus, class_name.data(), static_cast<int>(class_name.size())) == 0) {
        return false;
    }

    std::wstring value(class_name.data());
    std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value.find(L"edit") != std::wstring::npos ||
           value.find(L"richedit") != std::wstring::npos;
}

std::optional<std::filesystem::path> explorer_folder_for_window(HWND foreground) noexcept {
    try {
        winrt::com_ptr<IShellWindows> shell_windows;
        if (FAILED(CoCreateInstance(
                CLSID_ShellWindows,
                nullptr,
                CLSCTX_LOCAL_SERVER,
                IID_PPV_ARGS(shell_windows.put())))) {
            return std::nullopt;
        }

        long count{};
        if (FAILED(shell_windows->get_Count(&count))) {
            return std::nullopt;
        }

        for (long index = 0; index < count; ++index) {
            VARIANT item_index{};
            VariantInit(&item_index);
            item_index.vt = VT_I4;
            item_index.lVal = index;

            winrt::com_ptr<IDispatch> dispatch;
            if (FAILED(shell_windows->Item(item_index, dispatch.put())) || !dispatch) {
                continue;
            }

            winrt::com_ptr<IWebBrowserApp> browser;
            if (FAILED(dispatch->QueryInterface(IID_PPV_ARGS(browser.put()))) || !browser) {
                continue;
            }

            SHANDLE_PTR browser_hwnd{};
            if (FAILED(browser->get_HWND(&browser_hwnd)) ||
                reinterpret_cast<HWND>(browser_hwnd) != foreground) {
                continue;
            }

            winrt::com_ptr<IServiceProvider> provider;
            if (FAILED(browser->QueryInterface(IID_PPV_ARGS(provider.put()))) || !provider) {
                return std::nullopt;
            }

            winrt::com_ptr<IShellBrowser> shell_browser;
            if (FAILED(provider->QueryService(
                    SID_STopLevelBrowser,
                    IID_PPV_ARGS(shell_browser.put()))) || !shell_browser) {
                return std::nullopt;
            }

            winrt::com_ptr<IShellView> shell_view;
            if (FAILED(shell_browser->QueryActiveShellView(shell_view.put())) || !shell_view) {
                return std::nullopt;
            }

            winrt::com_ptr<IFolderView> folder_view;
            if (FAILED(shell_view->QueryInterface(IID_PPV_ARGS(folder_view.put()))) || !folder_view) {
                return std::nullopt;
            }

            winrt::com_ptr<IPersistFolder2> persist_folder;
            if (FAILED(folder_view->GetFolder(IID_PPV_ARGS(persist_folder.put()))) || !persist_folder) {
                return std::nullopt;
            }

            PIDLIST_ABSOLUTE pidl{};
            if (FAILED(persist_folder->GetCurFolder(&pidl)) || pidl == nullptr) {
                return std::nullopt;
            }

            std::array<wchar_t, 32768> path{};
            const bool converted = SHGetPathFromIDListEx(
                pidl,
                path.data(),
                static_cast<DWORD>(path.size()),
                GPFIDL_DEFAULT) != FALSE;
            CoTaskMemFree(pidl);

            if (!converted || path[0] == L'\0') {
                return std::nullopt;
            }
            return std::filesystem::path(path.data());
        }
    } catch (...) {
    }
    return std::nullopt;
}

} // namespace

MainWindow::~MainWindow() {
    RemoveTrayIntegration();
}

void MainWindow::InitializeTrayIntegration() {
    if (hwnd_ != nullptr) {
        return;
    }

    try {
        auto window_native = this->m_inner.as<::IWindowNative>();
        if (FAILED(window_native->get_WindowHandle(&hwnd_)) || hwnd_ == nullptr) {
            hwnd_ = nullptr;
            return;
        }

        if (!SetWindowSubclass(
                hwnd_,
                &MainWindow::TraySubclassProc,
                kTraySubclassId,
                reinterpret_cast<DWORD_PTR>(this))) {
            hwnd_ = nullptr;
            return;
        }

        (void)AddClipboardFormatListener(hwnd_);
        InitializeExplorerPasteInterception();

        std::array<wchar_t, 32768> module_path{};
        SHFILEINFOW shell_info{};
        const DWORD module_length = GetModuleFileNameW(
            nullptr,
            module_path.data(),
            static_cast<DWORD>(module_path.size()));
        if (module_length != 0 && module_length < module_path.size() &&
            SHGetFileInfoW(
                module_path.data(),
                FILE_ATTRIBUTE_NORMAL,
                &shell_info,
                sizeof(shell_info),
                SHGFI_ICON | SHGFI_SMALLICON) != 0) {
            tray_icon_ = shell_info.hIcon;
        }
        if (tray_icon_ == nullptr) {
            tray_icon_ = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
        }

        tray_data_ = {};
        tray_data_.cbSize = sizeof(tray_data_);
        tray_data_.hWnd = hwnd_;
        tray_data_.uID = kTrayIconId;
        tray_data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        tray_data_.uCallbackMessage = kTrayCallbackMessage;
        tray_data_.hIcon = tray_icon_;
        wcscpy_s(tray_data_.szTip, L"VelocityCopy");

        tray_added_ = Shell_NotifyIconW(NIM_ADD, &tray_data_) != FALSE;
    } catch (...) {
        RemoveTrayIntegration();
    }
}

void MainWindow::RemoveTrayIntegration() noexcept {
    RemoveExplorerPasteInterception();

    if (hwnd_ != nullptr) {
        (void)RemoveClipboardFormatListener(hwnd_);
    }

    if (tray_added_) {
        (void)Shell_NotifyIconW(NIM_DELETE, &tray_data_);
        tray_added_ = false;
    }

    if (hwnd_ != nullptr) {
        (void)RemoveWindowSubclass(hwnd_, &MainWindow::TraySubclassProc, kTraySubclassId);
    }

    if (tray_icon_ != nullptr) {
        DestroyIcon(tray_icon_);
        tray_icon_ = nullptr;
    }

    tray_data_ = {};
    hwnd_ = nullptr;
}

void MainWindow::HideToTray() noexcept {
    if (tray_exit_requested_ || hwnd_ == nullptr) {
        return;
    }

    try {
        AppWindow().IsShownInSwitchers(false);
    } catch (...) {
    }

    ShowWindow(hwnd_, SW_HIDE);
    tray_window_hidden_ = true;
}

void MainWindow::ShowFromTray() {
    if (hwnd_ == nullptr) {
        InitializeTrayIntegration();
    }

    try {
        AppWindow().IsShownInSwitchers(true);
    } catch (...) {
    }

    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_SHOW);
        ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
    }
    Activate();
    tray_window_hidden_ = false;
}

void MainWindow::ShowTrayMenu() noexcept {
    if (hwnd_ == nullptr) {
        return;
    }

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    std::wstring open_text = L"Open VelocityCopy";
    std::wstring exit_text = L"Exit";
    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        open_text = loader.GetString(L"TrayOpen").c_str();
        exit_text = loader.GetString(L"TrayExit").c_str();
    } catch (...) {
    }

    (void)AppendMenuW(menu, MF_STRING, kTrayOpenCommand, open_text.c_str());
    (void)AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    (void)AppendMenuW(menu, MF_STRING, kTrayExitCommand, exit_text.c_str());

    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(hwnd_);
    const UINT command = TrackPopupMenuEx(
        menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        point.x,
        point.y,
        hwnd_,
        nullptr);
    DestroyMenu(menu);

    if (command == kTrayOpenCommand) {
        ShowFromTray();
    } else if (command == kTrayExitCommand) {
        ExitFromTray();
    }
}

void MainWindow::ExitFromTray() noexcept {
    tray_exit_requested_ = true;
    if (tray_added_) {
        (void)Shell_NotifyIconW(NIM_DELETE, &tray_data_);
        tray_added_ = false;
    }
    if (hwnd_ != nullptr) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
}

void MainWindow::CaptureClipboardFileSelection() noexcept {
    if (hwnd_ == nullptr || !OpenClipboard(hwnd_)) {
        return;
    }

    std::vector<std::filesystem::path> sources;
    velocitycopy::FileOperation operation = velocitycopy::FileOperation::Copy;

    if (const auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP)); drop != nullptr) {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        try {
            sources.reserve(count);
            for (UINT index = 0; index < count; ++index) {
                const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                if (length == 0) {
                    continue;
                }
                std::wstring path(length + 1, L'\0');
                if (DragQueryFileW(drop, index, path.data(), length + 1) != 0) {
                    path.resize(length);
                    sources.emplace_back(std::move(path));
                }
            }
        } catch (...) {
            sources.clear();
        }
    }

    const UINT preferred_effect_format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    if (preferred_effect_format != 0) {
        if (const auto effect_data = static_cast<HGLOBAL>(GetClipboardData(preferred_effect_format));
            effect_data != nullptr) {
            if (const auto effect = static_cast<const DWORD*>(GlobalLock(effect_data)); effect != nullptr) {
                if ((*effect & DROPEFFECT_MOVE) != 0) {
                    operation = velocitycopy::FileOperation::Move;
                }
                GlobalUnlock(effect_data);
            }
        }
    }

    CloseClipboard();

    if (!sources.empty()) {
        try {
            shell_session_.stage_sources(std::move(sources), operation);
        } catch (...) {
        }
    }
}

void MainWindow::InitializeExplorerPasteInterception() noexcept {
    if (explorer_keyboard_hook_ != nullptr) {
        return;
    }

    g_explorer_keyboard_owner = this;
    explorer_keyboard_hook_ = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        &MainWindow::ExplorerKeyboardProc,
        GetModuleHandleW(nullptr),
        0);
    if (explorer_keyboard_hook_ == nullptr) {
        g_explorer_keyboard_owner = nullptr;
    }
}

void MainWindow::RemoveExplorerPasteInterception() noexcept {
    if (explorer_keyboard_hook_ != nullptr) {
        UnhookWindowsHookEx(explorer_keyboard_hook_);
        explorer_keyboard_hook_ = nullptr;
    }
    if (g_explorer_keyboard_owner == this) {
        g_explorer_keyboard_owner = nullptr;
    }
    paste_key_down_ = false;
}

bool MainWindow::TryInterceptExplorerPaste() noexcept {
    try {
        const HWND foreground = GetForegroundWindow();
        if (!is_explorer_process(foreground) ||
            focus_is_text_input(foreground) ||
            !IsClipboardFormatAvailable(CF_HDROP)) {
            return false;
        }

        CaptureClipboardFileSelection();
        if (shell_session_.staged_sources().empty()) {
            return false;
        }

        const auto destination = explorer_folder_for_window(foreground);
        if (!destination || destination->empty()) {
            return false;
        }

        velocitycopy::ShellRequest request{};
        request.action = velocitycopy::ShellAction::PasteToFolder;
        request.destination = *destination;
        HandleShellRequest(request);
        return true;
    } catch (...) {
        return false;
    }
}

LRESULT CALLBACK MainWindow::ExplorerKeyboardProc(
    const int code,
    const WPARAM wparam,
    const LPARAM lparam) {
    auto* self = g_explorer_keyboard_owner;
    if (code < 0 || self == nullptr || lparam == 0) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
    if (key->vkCode != 'V') {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    const bool key_down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
    const bool key_up = wparam == WM_KEYUP || wparam == WM_SYSKEYUP;
    const bool control_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt_down = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    if (key_down && control_down && !alt_down) {
        if (self->paste_key_down_) {
            return 1;
        }
        if (self->TryInterceptExplorerPaste()) {
            self->paste_key_down_ = true;
            return 1;
        }
    }

    if (key_up && self->paste_key_down_) {
        self->paste_key_down_ = false;
        return 1;
    }

    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK MainWindow::TraySubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR,
    DWORD_PTR ref_data) {
    auto* self = reinterpret_cast<MainWindow*>(ref_data);
    if (self == nullptr) {
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }

    if (message == kTrayCallbackMessage && wparam == kTrayIconId) {
        if (lparam == WM_LBUTTONUP || lparam == WM_LBUTTONDBLCLK) {
            self->ShowFromTray();
            return 0;
        }
        if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
            self->ShowTrayMenu();
            return 0;
        }
    }

    switch (message) {
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0) == SC_MINIMIZE && !self->tray_exit_requested_) {
            self->HideToTray();
            return 0;
        }
        break;

    case WM_CLOSE:
        if (!self->tray_exit_requested_) {
            self->HideToTray();
            return 0;
        }
        break;

    case WM_CLIPBOARDUPDATE:
        self->CaptureClipboardFileSelection();
        return 0;

    case WM_DESTROY:
        self->RemoveTrayIntegration();
        break;
    }

    return DefSubclassProc(hwnd, message, wparam, lparam);
}

} // namespace winrt::VelocityCopyUI::implementation
