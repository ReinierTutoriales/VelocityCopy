#include "velocitycopy/explorer_paste.hpp"
#include "velocitycopy/ipc_transport.hpp"
#include "velocitycopy/job_executor.hpp"
#include "../src/shell/shell_data.hpp"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <exdisp.h>
#include <servprov.h>
#include <wrl/client.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
}

bool focus_folder(const fs::path& path, const fs::path& selection = {}) {
    PIDLIST_ABSOLUTE pidl{};
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) return false;
    PIDLIST_ABSOLUTE selected{};
    PCUITEMID_CHILD child{};
    if (!selection.empty() && SUCCEEDED(SHParseDisplayName(selection.c_str(), nullptr, &selected, 0, nullptr)))
        child = ILFindLastID(selected);
    HRESULT opened = E_FAIL;
    if (FAILED(opened) && selection.empty()) {
        SHELLEXECUTEINFOW launch{sizeof(launch)};
        launch.fMask = SEE_MASK_IDLIST; launch.lpVerb = L"open"; launch.lpIDList = pidl; launch.nShow = SW_SHOWNORMAL;
        opened = ShellExecuteExW(&launch) ? S_OK : E_FAIL;
    }
    if (FAILED(opened)) opened = SHOpenFolderAndSelectItems(pidl, child ? 1 : 0, child ? &child : nullptr, 0);
    CoTaskMemFree(selected); CoTaskMemFree(pidl);
    if (FAILED(opened)) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    do {
        ComPtr<IShellWindows> windows;
        if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows)))) return false;
        long count{}; windows->get_Count(&count);
        for (long i = 0; i < count; ++i) {
            VARIANT index{}; index.vt = VT_I4; index.lVal = i;
            ComPtr<IDispatch> item;
            ComPtr<IServiceProvider> services;
            ComPtr<IShellBrowser> browser;
            ComPtr<IShellView> view;
            ComPtr<IFolderView> fv;
            ComPtr<IPersistFolder2> folder;
            if (FAILED(windows->Item(index, &item)) || FAILED(item.As(&services)) ||
                FAILED(services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) ||
                FAILED(browser->QueryActiveShellView(&view)) || FAILED(view.As(&fv)) ||
                FAILED(fv->GetFolder(IID_PPV_ARGS(&folder)))) continue;
            PIDLIST_ABSOLUTE current{};
            if (FAILED(folder->GetCurFolder(&current))) continue;
            wchar_t buffer[32768]{};
            const bool matches = SHGetPathFromIDListEx(current, buffer, 32768, GPFIDL_DEFAULT) &&
                _wcsicmp(buffer, path.c_str()) == 0;
            CoTaskMemFree(current);
            if (!matches) continue;
            if (!selection.empty()) {
                ComPtr<IDataObject> chosen;
                std::vector<fs::path> paths;
                if (FAILED(view->GetItemObject(SVGIO_SELECTION, IID_PPV_ARGS(&chosen))) ||
                    FAILED(velocitycopy::shell::read_sources(chosen.Get(), paths)) ||
                    paths != std::vector<fs::path>{selection}) continue;
            }
            ComPtr<IWebBrowserApp> web;
            SHANDLE_PTR value{};
            if (FAILED(item.As(&web)) || FAILED(web->get_HWND(&value))) continue;
            HWND hwnd = reinterpret_cast<HWND>(value);
            const DWORD foreground_thread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
            const bool attached = foreground_thread && foreground_thread != GetCurrentThreadId() &&
                AttachThreadInput(GetCurrentThreadId(), foreground_thread, TRUE);
            SetForegroundWindow(GetAncestor(hwnd, GA_ROOT));
            if (attached) AttachThreadInput(GetCurrentThreadId(), foreground_thread, FALSE);
            view->UIActivate(SVUIA_ACTIVATE_FOCUS);
            pump();
            if (GetForegroundWindow() == GetAncestor(hwnd, GA_ROOT)) return true;
        }
        pump();
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

bool shortcut(WORD key) {
    INPUT input[4]{};
    for (auto& item : input) item.type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_LCONTROL;
    input[1].ki.wVk = key;
    input[2].ki.wVk = key; input[2].ki.dwFlags = KEYEVENTF_KEYUP;
    input[3].ki.wVk = VK_LCONTROL; input[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(4, input, sizeof(INPUT)) == 4;
}

int wmain(int argc, wchar_t** argv) {
    // Normal CTest proves lifecycle only; interactive mode exercises actual Explorer.
    velocitycopy::ExplorerPaste adapter;
    if (argc == 2 && wcscmp(argv[1], L"--measure-idle") == 0) {
        if (!adapter.start()) return 1;
        FILETIME created{}, exited{}, kernel_before{}, user_before{}, kernel_after{}, user_after{};
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel_before, &user_before);
        Sleep(2000);
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel_after, &user_after);
        auto ticks = [](FILETIME value) { return (static_cast<unsigned long long>(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
        std::cout << "Idle process CPU over 2000 ms: " <<
            (ticks(kernel_after) + ticks(user_after) - ticks(kernel_before) - ticks(user_before)) / 10000.0 << " ms\n";
        adapter.stop();
        return 0;
    }
    if (argc == 1) {
        for (int i = 0; i < 3; ++i) {
            if (!adapter.start()) return 1;
            adapter.stop();
        }
        return 0;
    }
    if (argc != 2 || wcscmp(argv[1], L"--interactive") || FAILED(OleInitialize(nullptr))) return 2;
    const auto base = fs::temp_directory_path() / (L"VelocityCopyExplorer-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(base / L"source" / L"Folder" / L"Nested");
    fs::create_directories(base / L"copy"); fs::create_directories(base / L"move");
    std::ofstream(base / L"source" / L"Folder" / L"Nested" / L"proof.txt") << "Explorer keyboard transfer";
    if (!adapter.start()) return 3;
    bool ok = true;
    for (const WORD key : {WORD('C'), WORD('X')}) {
        const auto source = base / (key == 'C' ? L"source" : L"copy") / L"Folder";
        const auto target = base / (key == 'C' ? L"copy" : L"move");
        velocitycopy::ShellIpcServer server;
        if (!server.valid()) { std::cerr << "Close the resident app before this test\n"; ok = false; break; }
        std::promise<bool> completed;
        auto result = completed.get_future();
        std::jthread receiver([&] {
            auto request = server.receive_and_ack([](const auto&) { return true; });
            if (!request) { completed.set_value(false); return; }
            const bool expected = request->destination == target && request->sources == std::vector<fs::path>{source} &&
                request->operation == (key == 'X' ? velocitycopy::FileOperation::Move : velocitycopy::FileOperation::Copy);
            velocitycopy::CopyJob job;
            job.id = 1; job.sources = request->sources; job.destination = request->destination;
            job.operation = request->operation; job.layout = request->layout;
            velocitycopy::JobExecutor executor;
            completed.set_value(expected && executor.execute(job).success);
        });
        const DWORD sequence = GetClipboardSequenceNumber();
        bool sent = focus_folder(source.parent_path(), source);
        if (!sent) std::cerr << "Source Explorer window could not take focus\n";
        sent = sent && shortcut(key);
        const auto clip_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (sent && GetClipboardSequenceNumber() == sequence && std::chrono::steady_clock::now() < clip_deadline) pump();
        if (GetClipboardSequenceNumber() == sequence) std::cerr << "Explorer did not publish clipboard selection\n";
        sent = sent && GetClipboardSequenceNumber() != sequence;
        if (sent && !focus_folder(target)) { std::cerr << "Destination Explorer window could not take focus\n"; sent = false; }
        const auto pressed = std::chrono::steady_clock::now();
        sent = sent && shortcut('V');
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (sent && result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
               std::chrono::steady_clock::now() < deadline) pump();
        if (!sent || result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) server.stop();
        receiver.join();
        ok = sent && result.get() && fs::exists(target / L"Folder" / L"Nested" / L"proof.txt") &&
            (key == 'C' ? fs::exists(source) : !fs::exists(source));
        if (!ok) { std::cerr << "Explorer shortcut failed: " << char(key) << "\n"; break; }
        std::cout << "Explorer Ctrl+" << char(key) << " -> Ctrl+V -> IPC -> JobExecutor passed in " <<
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pressed).count() << " ms\n";
    }
    adapter.stop();
    if (OpenClipboard(nullptr)) { EmptyClipboard(); CloseClipboard(); }
    OleUninitialize();
    // Keep the disposable fixture for inspection; never delete a path from clipboard.
    std::wcout << L"Fixture: " << base << L'\n';
    return ok ? 0 : 4;
}
