// The shell automation headers (exdisp.h/shldisp.h) need the full COM/OLE
// declarations that WIN32_LEAN_AND_MEAN strips from windows.h; with it, MSVC
// rejects their interface typedefs (C2371). This helper is tiny, so build it
// against the complete header set.
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <exdisp.h>
#include <shldisp.h>
#include <wrl/client.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace {

constexpr wchar_t kRunSubkey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"VelocityCopy";
constexpr DWORD kNoInteractiveShell = 10;

std::wstring quote(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size() + 2);
    result.push_back(L'"');
    for (const wchar_t ch : value) {
        if (ch == L'"') result.push_back(L'\\');
        result.push_back(ch);
    }
    result.push_back(L'"');
    return result;
}

DWORD write_startup(HKEY current_user, const std::wstring& executable) noexcept {
    HKEY key{};
    const auto open = RegCreateKeyExW(
        current_user,
        kRunSubkey,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE | KEY_QUERY_VALUE,
        nullptr,
        &key,
        nullptr);
    if (open != ERROR_SUCCESS) return static_cast<DWORD>(open);

    const std::wstring command = quote(executable) + L" --startup";
    const auto bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const auto set = RegSetValueExW(
        key,
        kRunValue,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        bytes);
    RegCloseKey(key);
    return static_cast<DWORD>(set);
}

DWORD remove_startup(HKEY current_user, const std::wstring& executable) noexcept {
    HKEY key{};
    const auto open = RegOpenKeyExW(
        current_user,
        kRunSubkey,
        0,
        KEY_SET_VALUE | KEY_QUERY_VALUE,
        &key);
    if (open == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    if (open != ERROR_SUCCESS) return static_cast<DWORD>(open);

    wchar_t value[32768]{};
    DWORD type{};
    DWORD bytes = sizeof(value);
    const auto query = RegQueryValueExW(
        key,
        kRunValue,
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(value),
        &bytes);
    if (query == ERROR_FILE_NOT_FOUND) {
        RegCloseKey(key);
        return ERROR_SUCCESS;
    }
    if (query != ERROR_SUCCESS) {
        RegCloseKey(key);
        return static_cast<DWORD>(query);
    }

    const std::wstring expected = quote(executable) + L" --startup";
    if ((type == REG_SZ || type == REG_EXPAND_SZ) && expected == value) {
        const auto removed = RegDeleteValueW(key, kRunValue);
        RegCloseKey(key);
        return removed == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : static_cast<DWORD>(removed);
    }

    RegCloseKey(key);
    return ERROR_SUCCESS;
}

DWORD apply_for_current_token(
    const bool install,
    const std::wstring& executable) noexcept {
    HKEY current_user{};
    const LSTATUS opened = RegOpenCurrentUser(
        KEY_SET_VALUE | KEY_QUERY_VALUE,
        &current_user);
    if (opened != ERROR_SUCCESS) return static_cast<DWORD>(opened);

    const DWORD result = install
        ? write_startup(current_user, executable)
        : remove_startup(current_user, executable);
    RegCloseKey(current_user);
    return result;
}

DWORD same_session_shell_pid() noexcept {
    const HWND shell = GetShellWindow();
    if (shell == nullptr) return 0;

    DWORD shell_pid{};
    GetWindowThreadProcessId(shell, &shell_pid);
    if (shell_pid == 0) return 0;

    DWORD current_session{};
    DWORD shell_session{};
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &current_session) ||
        !ProcessIdToSessionId(shell_pid, &shell_session) ||
        current_session != shell_session) {
        return 0;
    }
    return shell_pid;
}

DWORD apply_with_shell_token(
    const bool install,
    const std::wstring& executable) noexcept {
    const DWORD shell_pid = same_session_shell_pid();
    if (shell_pid == 0) return kNoInteractiveShell;

    HANDLE shell_process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shell_pid);
    if (shell_process == nullptr) return GetLastError();

    HANDLE shell_token{};
    if (!OpenProcessToken(
            shell_process,
            TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE,
            &shell_token)) {
        const DWORD error = GetLastError();
        CloseHandle(shell_process);
        return error;
    }
    CloseHandle(shell_process);

    if (!ImpersonateLoggedOnUser(shell_token)) {
        const DWORD error = GetLastError();
        CloseHandle(shell_token);
        return error;
    }
    CloseHandle(shell_token);

    HKEY current_user{};
    const LSTATUS opened = RegOpenCurrentUser(
        KEY_SET_VALUE | KEY_QUERY_VALUE,
        &current_user);
    if (opened != ERROR_SUCCESS) {
        RevertToSelf();
        if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND) {
            return kNoInteractiveShell;
        }
        return static_cast<DWORD>(opened);
    }

    const DWORD result = install
        ? write_startup(current_user, executable)
        : remove_startup(current_user, executable);
    RegCloseKey(current_user);
    RevertToSelf();
    return result;
}

// The installer runs elevated, and Exec/ExecShell would hand VelocityCopy the
// elevated token. Ask the interactive desktop shell (already running with the
// user's normal token) to start it instead, through the documented
// IShellDispatch2::ShellExecute of the desktop folder view. Unlike
// `explorer.exe <path>`, this forwards arguments, so the app starts with
// --startup: resident in the notification area, no window left open.
class ComBstr final {
public:
    explicit ComBstr(const wchar_t* value) noexcept : value_(SysAllocString(value)) {}
    ~ComBstr() { SysFreeString(value_); }
    ComBstr(const ComBstr&) = delete;
    ComBstr& operator=(const ComBstr&) = delete;
    [[nodiscard]] BSTR get() const noexcept { return value_; }

private:
    BSTR value_{};
};

HRESULT shell_execute_unelevated(const std::wstring& executable, const wchar_t* arguments) noexcept {
    using Microsoft::WRL::ComPtr;

    ComPtr<IShellWindows> windows;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows));
    if (FAILED(hr)) return hr;

    VARIANT location{};
    location.vt = VT_I4;
    location.lVal = CSIDL_DESKTOP;
    VARIANT empty{};
    long desktop_hwnd{};
    ComPtr<IDispatch> desktop;
    hr = windows->FindWindowSW(&location, &empty, SWC_DESKTOP, &desktop_hwnd, SWFO_NEEDDISPATCH, &desktop);
    if (hr == S_FALSE || !desktop) return E_FAIL;
    if (FAILED(hr)) return hr;

    ComPtr<IShellBrowser> browser;
    hr = IUnknown_QueryService(desktop.Get(), SID_STopLevelBrowser, IID_PPV_ARGS(&browser));
    if (FAILED(hr)) return hr;
    ComPtr<IShellView> view;
    hr = browser->QueryActiveShellView(&view);
    if (FAILED(hr)) return hr;
    ComPtr<IDispatch> background;
    hr = view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background));
    if (FAILED(hr)) return hr;
    ComPtr<IShellFolderViewDual> folder_view;
    hr = background.As(&folder_view);
    if (FAILED(hr)) return hr;
    ComPtr<IDispatch> application;
    hr = folder_view->get_Application(&application);
    if (FAILED(hr)) return hr;
    ComPtr<IShellDispatch2> shell;
    hr = application.As(&shell);
    if (FAILED(hr)) return hr;

    std::wstring directory;
    try {
        directory = std::filesystem::path(executable).parent_path().wstring();
    } catch (...) {
        return E_OUTOFMEMORY;
    }
    const ComBstr file(executable.c_str());
    const ComBstr argument_text(arguments);
    const ComBstr directory_text(directory.c_str());
    const ComBstr operation_text(L"open");
    if (!file.get() || !argument_text.get() || !directory_text.get() || !operation_text.get()) {
        return E_OUTOFMEMORY;
    }
    VARIANT argument_value{};
    argument_value.vt = VT_BSTR;
    argument_value.bstrVal = argument_text.get();
    VARIANT directory_value{};
    directory_value.vt = VT_BSTR;
    directory_value.bstrVal = directory_text.get();
    VARIANT operation_value{};
    operation_value.vt = VT_BSTR;
    operation_value.bstrVal = operation_text.get();
    VARIANT show_value{};
    show_value.vt = VT_I4;
    show_value.lVal = SW_SHOWNORMAL;
    return shell->ShellExecute(file.get(), argument_value, directory_value, operation_value, show_value);
}

DWORD launch_resident(const std::wstring& executable) noexcept {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized)) return static_cast<DWORD>(initialized);
    const HRESULT launched = shell_execute_unelevated(executable, L"--startup --installed");
    CoUninitialize();
    return SUCCEEDED(launched) ? ERROR_SUCCESS : static_cast<DWORD>(launched);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return ERROR_INVALID_PARAMETER;

    const std::wstring_view mode{argv[1]};
    const std::wstring executable{argv[2]};

    if (mode == L"--apply-install") {
        return static_cast<int>(apply_for_current_token(true, executable));
    }
    if (mode == L"--apply-remove") {
        return static_cast<int>(apply_for_current_token(false, executable));
    }
    if (mode == L"--install") {
        return static_cast<int>(apply_with_shell_token(true, executable));
    }
    if (mode == L"--launch") {
        return static_cast<int>(launch_resident(executable));
    }
    if (mode == L"--remove") {
        return static_cast<int>(apply_with_shell_token(false, executable));
    }
    return ERROR_INVALID_PARAMETER;
}
