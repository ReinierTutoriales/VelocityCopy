#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

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

DWORD write_current_user_startup(const std::wstring& executable) noexcept {
    HKEY key{};
    const auto open = RegCreateKeyExW(
        HKEY_CURRENT_USER,
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

DWORD remove_current_user_startup(const std::wstring& executable) noexcept {
    HKEY key{};
    const auto open = RegOpenKeyExW(
        HKEY_CURRENT_USER,
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

DWORD run_with_shell_token(
    const wchar_t* self,
    const std::wstring_view apply_mode,
    const std::wstring& executable) noexcept {
    const DWORD shell_pid = same_session_shell_pid();
    if (shell_pid == 0) return kNoInteractiveShell;

    HANDLE shell_process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shell_pid);
    if (shell_process == nullptr) return GetLastError();

    HANDLE shell_token{};
    if (!OpenProcessToken(
            shell_process,
            TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY,
            &shell_token)) {
        const DWORD error = GetLastError();
        CloseHandle(shell_process);
        return error;
    }
    CloseHandle(shell_process);

    HANDLE primary_token{};
    if (!DuplicateTokenEx(
            shell_token,
            TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY,
            nullptr,
            SecurityImpersonation,
            TokenPrimary,
            &primary_token)) {
        const DWORD error = GetLastError();
        CloseHandle(shell_token);
        return error;
    }
    CloseHandle(shell_token);

    wchar_t module_path[32768]{};
    const DWORD module_length = GetModuleFileNameW(nullptr, module_path, static_cast<DWORD>(std::size(module_path)));
    if (module_length == 0 || module_length >= std::size(module_path)) {
        return GetLastError() == ERROR_SUCCESS ? ERROR_FILE_NOT_FOUND : GetLastError();
    }
    const std::wstring canonical_self{module_path, module_length};

    std::wstring command = quote(canonical_self);
    command += L" ";
    command += apply_mode;
    command += L" ";
    command += quote(executable);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessWithTokenW(
            primary_token,
            LOGON_WITH_PROFILE,
            canonical_self.c_str(),
            command.data(),
            0,
            nullptr,
            nullptr,
            &startup,
            &process)) {
        const DWORD error = GetLastError();
        CloseHandle(primary_token);
        return error;
    }
    CloseHandle(primary_token);
    CloseHandle(process.hThread);

    const DWORD wait = WaitForSingleObject(process.hProcess, 15000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, ERROR_TIMEOUT);
        CloseHandle(process.hProcess);
        return wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
    }

    DWORD exit_code = ERROR_GEN_FAILURE;
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        exit_code = GetLastError();
    }
    CloseHandle(process.hProcess);
    return exit_code;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return ERROR_INVALID_PARAMETER;

    const std::wstring_view mode{argv[1]};
    const std::wstring executable{argv[2]};

    if (mode == L"--apply-install") {
        return static_cast<int>(write_current_user_startup(executable));
    }
    if (mode == L"--apply-remove") {
        return static_cast<int>(remove_current_user_startup(executable));
    }
    if (mode == L"--install") {
        return static_cast<int>(run_with_shell_token(
            argv[0], L"--apply-install", executable));
    }
    if (mode == L"--remove") {
        return static_cast<int>(run_with_shell_token(
            argv[0], L"--apply-remove", executable));
    }
    return ERROR_INVALID_PARAMETER;
}
