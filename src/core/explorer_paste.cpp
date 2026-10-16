#include "velocitycopy/explorer_paste.hpp"
#include "velocitycopy/ipc_transport.hpp"
#include "velocitycopy/diagnostics.hpp"
#include "../shell/shell_data.hpp"
#include <windows.h>
#include <exdisp.h>
#include <servprov.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstring>
#include <future>
#include <thread>

namespace velocitycopy {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT paste_message = WM_APP + 0x37;
constexpr ULONG_PTR replay_marker = 0x56435056;
struct Paste {
    HWND window;
    HWND focus;
    DWORD sequence;
};

bool same_context(const Paste& paste) noexcept {
    GUITHREADINFO info{sizeof(info)};
    return GetForegroundWindow() == paste.window &&
        GetGUIThreadInfo(GetWindowThreadProcessId(paste.window, nullptr), &info) &&
        info.hwndFocus == paste.focus && GetClipboardSequenceNumber() == paste.sequence;
}

// Replay only a rejected, undelivered keystroke, and only in its original context.
void replay(const Paste& paste) noexcept {
    if (!same_context(paste)) {
        log_diagnostic(L"Explorer paste cancelled: focus or clipboard changed");
        return;
    }
    std::array<INPUT, 4> keys{};
    for (auto& key : keys) { key.type = INPUT_KEYBOARD; key.ki.dwExtraInfo = replay_marker; }
    keys[0].ki.wVk = VK_CONTROL;
    keys[1].ki.wVk = 'V';
    keys[2].ki.wVk = 'V'; keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
    keys[3].ki.wVk = VK_CONTROL; keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
    const bool control_held = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const UINT count = control_held ? 2u : 4u;
    if (SendInput(count, keys.data() + (control_held ? 1 : 0), sizeof(INPUT)) != count)
        log_diagnostic(L"Explorer native paste replay failed");
}

std::filesystem::path destination(const Paste& paste) {
    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER,
                               IID_PPV_ARGS(&windows)))) return {};
    long count{};
    if (FAILED(windows->get_Count(&count))) return {};
    for (long i = 0; i < count; ++i) {
        VARIANT index{}; index.vt = VT_I4; index.lVal = i;
        ComPtr<IDispatch> item;
        if (FAILED(windows->Item(index, &item))) continue;
        ComPtr<IWebBrowserApp> web;
        if (FAILED(item.As(&web))) continue;
        SHANDLE_PTR window{};
        if (FAILED(web->get_HWND(&window)) ||
            GetAncestor(reinterpret_cast<HWND>(window), GA_ROOT) != paste.window) continue;
        ComPtr<IServiceProvider> services;
        ComPtr<IShellBrowser> browser;
        ComPtr<IShellView> view;
        if (FAILED(item.As(&services)) ||
            FAILED(services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) ||
            FAILED(browser->QueryActiveShellView(&view))) continue;
        HWND view_window{};
        if (FAILED(view->GetWindow(&view_window)) || !IsWindowVisible(view_window) ||
            (paste.focus != view_window && !IsChild(view_window, paste.focus))) continue;
        // Rename editors belong to the view too; preserve their normal text paste.
        std::array<wchar_t, 128> focus_class{};
        GetClassNameW(paste.focus, focus_class.data(), static_cast<int>(focus_class.size()));
        if (_wcsicmp(focus_class.data(), L"Edit") == 0 ||
            wcsstr(focus_class.data(), L"RichEdit")) return {};
        ComPtr<IFolderView> folder_view;
        ComPtr<IPersistFolder2> folder;
        if (FAILED(view.As(&folder_view)) || FAILED(folder_view->GetFolder(IID_PPV_ARGS(&folder)))) continue;
        PIDLIST_ABSOLUTE pidl{};
        if (FAILED(folder->GetCurFolder(&pidl))) continue;
        std::array<wchar_t, 32768> path{};
        const bool valid = SHGetPathFromIDListEx(pidl, path.data(), static_cast<DWORD>(path.size()), GPFIDL_DEFAULT) != FALSE;
        CoTaskMemFree(pidl);
        if (valid) return path.data();
    }
    return {};
}

void transfer(const Paste& paste) noexcept {
    try {
        if (!same_context(paste)) return;
        ShellRequest request;
        request.action = ShellAction::Transfer;
        request.destination = destination(paste);
        if (request.destination.empty()) { replay(paste); return; }
        ComPtr<IDataObject> data;
        if (FAILED(OleGetClipboard(&data)) ||
            FAILED(shell::clipboard_sources_from_data(data.Get(), request)) ||
            !shell_request_valid(request)) { replay(paste); return; }
        if (!same_context(paste)) return;
        GUID identity{};
        if (FAILED(CoCreateGuid(&identity))) { replay(paste); return; }
        std::memcpy(&request.request_id, &identity, sizeof(request.request_id));
        if (!request.request_id) request.request_id = 1;
        request.version = 3;
        const auto result = send_shell_request(request, 2000);
        if (!result) {
            // An uncertain admission must never run the same Move in two engines.
            if (result.delivery_ambiguous) log_diagnostic(L"Explorer paste: uncertain admission; native paste not replayed");
            else replay(paste);
        }
    } catch (...) { log_diagnostic(L"Explorer paste failed before admission"); replay(paste); }
}
}

struct ExplorerPaste::State {
    std::jthread worker;
    std::jthread keyboard;
    std::atomic<HWND> target{};
    DWORD keyboard_id{};
    std::atomic_bool stopping{};
    std::array<bool, 256> keys{};
    bool swallowed{};
    static inline thread_local State* current{};

    static LRESULT CALLBACK key(int code, WPARAM message, LPARAM value) noexcept {
        auto* self = current;
        if (code < 0 || !self || !value) return CallNextHookEx(nullptr, code, message, value);
        const auto& event = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(value);
        if (event.dwExtraInfo == replay_marker) return CallNextHookEx(nullptr, code, message, value);
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        if (event.vkCode < self->keys.size()) self->keys[event.vkCode] = down;
        if (event.vkCode != 'V') return CallNextHookEx(nullptr, code, message, value);
        if (self->swallowed) { if (!down) self->swallowed = false; return 1; }
        const auto& keys = self->keys;
        if (!down || !(keys[VK_LCONTROL] || keys[VK_RCONTROL]) || keys[VK_LMENU] || keys[VK_RMENU] ||
            keys[VK_LSHIFT] || keys[VK_RSHIFT] || keys[VK_LWIN] || keys[VK_RWIN] || self->stopping)
            return CallNextHookEx(nullptr, code, message, value);
        const HWND foreground = GetForegroundWindow();
        std::array<wchar_t, 32> name{};
        if (!GetClassNameW(foreground, name.data(), static_cast<int>(name.size())) ||
            wcscmp(name.data(), L"CabinetWClass") != 0 || !IsClipboardFormatAvailable(CF_HDROP))
            return CallNextHookEx(nullptr, code, message, value);
        GUITHREADINFO info{sizeof(info)};
        if (!GetGUIThreadInfo(GetWindowThreadProcessId(foreground, nullptr), &info) || !info.hwndFocus)
            return CallNextHookEx(nullptr, code, message, value);
        auto* paste = new (std::nothrow) Paste{foreground, info.hwndFocus, GetClipboardSequenceNumber()};
        if (!paste) return CallNextHookEx(nullptr, code, message, value);
        if (!PostMessageW(self->target, paste_message, 0, reinterpret_cast<LPARAM>(paste))) {
            delete paste; return CallNextHookEx(nullptr, code, message, value);
        }
        self->swallowed = true;
        return 1;
    }

    static LRESULT CALLBACK window(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == paste_message) {
            std::unique_ptr<Paste> paste(reinterpret_cast<Paste*>(lparam));
            if (state && !state->stopping) transfer(*paste);
            return 0;
        }
        if (message == WM_CLOSE) { DestroyWindow(hwnd); PostQuitMessage(0); return 0; }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
};

ExplorerPaste::ExplorerPaste() : state_(std::make_unique<State>()) {}
ExplorerPaste::~ExplorerPaste() { stop(); }

bool ExplorerPaste::start() noexcept {
    if (state_->keyboard.joinable()) return true;
    auto* state = state_.get();
    state->stopping = false;
    state->swallowed = false;
    try {
        std::promise<bool> ready;
        auto started = ready.get_future();
        state->worker = std::jthread([state, &ready] {
            if (FAILED(OleInitialize(nullptr))) { ready.set_value(false); return; }
            WNDCLASSW cls{};
            cls.lpfnWndProc = State::window; cls.hInstance = GetModuleHandleW(nullptr);
            cls.lpszClassName = L"VelocityCopy.ExplorerPaste";
            if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                ready.set_value(false); OleUninitialize(); return;
            }
            state->target = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0,
                                           HWND_MESSAGE, nullptr, cls.hInstance, state);
            ready.set_value(state->target != nullptr);
            MSG message{};
            if (state->target) while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            state->target = nullptr;
            OleUninitialize();
        });
        if (!started.get()) { stop(); return false; }
        std::promise<bool> hooked;
        auto installed = hooked.get_future();
        state->keyboard = std::jthread([state, &hooked] {
            State::current = state;
            state->keyboard_id = GetCurrentThreadId();
            MSG message{}; PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
            for (int i = 0; i < 256; ++i) state->keys[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
            HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, State::key, GetModuleHandleW(nullptr), 0);
            hooked.set_value(hook != nullptr);
            if (hook) {
                while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
                UnhookWindowsHookEx(hook);
            }
            State::current = nullptr;
        });
        if (installed.get()) return true;
    } catch (...) {}
    stop(); return false;
}

void ExplorerPaste::stop() noexcept {
    state_->stopping = true;
    if (state_->keyboard.joinable()) {
        PostThreadMessageW(state_->keyboard_id, WM_QUIT, 0, 0);
        state_->keyboard.join();
    }
    if (state_->worker.joinable()) {
        if (state_->target) PostMessageW(state_->target, WM_CLOSE, 0, 0);
        state_->worker.join();
    }
}
}
