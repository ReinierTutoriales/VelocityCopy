#include "../src/shell/drop_handler.hpp"
#include "../src/shell/shell_data.hpp"
#include "velocitycopy/ipc_transport.hpp"
#include <windows.h>
#include <shlobj.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace {
using GetClass = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
using CanUnload = HRESULT(WINAPI*)();
class Data final : public IDataObject {
    std::atomic<ULONG> refs_{1};
public:
    bool malformed{};
    bool ansi{};
    bool truncated{};
    bool clipboard_effect{};
    DWORD preferred_effect{DROPEFFECT_COPY};
    unsigned sets{};
    IFACEMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (id != IID_IUnknown && id != IID_IDataObject) return E_NOINTERFACE;
        *out = static_cast<IDataObject*>(this); AddRef(); return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override { ULONG n=--refs_; if(!n) delete this; return n; }
    IFACEMETHODIMP GetData(FORMATETC* f, STGMEDIUM* out) override {
        if (!f || !out) return E_POINTER;
        *out = {};
        const bool drop = f->cfFormat == CF_HDROP;
        const bool effect = clipboard_effect && f->cfFormat == RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        if (!drop && !effect) return DV_E_FORMATETC;
        if (effect) {
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
            if (!memory) return E_OUTOFMEMORY;
            void* bytes = GlobalLock(memory);
            if (!bytes) { GlobalFree(memory); return E_OUTOFMEMORY; }
            std::memcpy(bytes, &preferred_effect, sizeof(DWORD));
            GlobalUnlock(memory);
            out->tymed = TYMED_HGLOBAL;
            out->hGlobal = memory;
            return S_OK;
        }
        const wchar_t paths[] = L"C:\\Source\\a.txt\0C:\\Source\\Folder\0";
        const char narrow[] = "C:\\Source\\a.txt\0C:\\Source\\Folder\0";
        SIZE_T bytes = sizeof(DROPFILES) + (ansi ? sizeof(narrow) : sizeof(paths)) - (truncated ? (ansi ? 2 : 2 * sizeof(wchar_t)) : 0);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        if (!memory) return E_OUTOFMEMORY;
        auto* raw = static_cast<unsigned char*>(GlobalLock(memory));
        if (!raw) { GlobalFree(memory); return E_OUTOFMEMORY; }
        if (drop) {
            DROPFILES header{};
            header.pFiles = malformed ? static_cast<DWORD>(bytes + 8) : sizeof(DROPFILES);
            header.fWide = !ansi;
            std::memcpy(raw, &header, sizeof(header));
            const SIZE_T payload_bytes = bytes - sizeof(header);
            if (ansi) std::memcpy(raw + sizeof(header), narrow, payload_bytes);
            else std::memcpy(raw + sizeof(header), paths, payload_bytes);
        }
        GlobalUnlock(memory);
        out->tymed = TYMED_HGLOBAL; out->hGlobal = memory;
        return S_OK;
    }
    IFACEMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    IFACEMETHODIMP QueryGetData(FORMATETC*) override { return DV_E_FORMATETC; }
    IFACEMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    IFACEMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { ++sets; return E_NOTIMPL; }
    IFACEMETHODIMP EnumFormatEtc(DWORD direction, IEnumFORMATETC** out) override {
        if (direction != DATADIR_GET) return E_NOTIMPL;
        FORMATETC formats[] = {
            {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
            {static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)),
             nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}};
        return SHCreateStdEnumFmtEtc(clipboard_effect ? 2 : 1, formats, out);
    }
    IFACEMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    IFACEMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    IFACEMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};
bool run(GetClass get, CanUnload unload, bool live_clipboard) {
    IClassFactory* factory{};
    if (FAILED(get(CLSID_VelocityCopyDropHandler, IID_PPV_ARGS(&factory)))) return false;
    IShellExtInit* init{};
    if (FAILED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&init)))) { factory->Release(); return false; }
    factory->Release();
    IContextMenu* context{};
    if (FAILED(init->QueryInterface(IID_PPV_ARGS(&context)))) { init->Release(); return false; }
    auto* data = new Data;
    PIDLIST_ABSOLUTE target{};
    if (FAILED(SHGetKnownFolderIDList(FOLDERID_Profile, 0, nullptr, &target))) return false;
    wchar_t path[32768]{};
    SHGetPathFromIDListEx(target, path, 32768, GPFIDL_DEFAULT);
    auto check = [&](bool condition, const char* message) {
        if (!condition) std::cerr << message << '\n';
        return condition;
    };
    bool ok = true;
    HMENU menu = CreatePopupMenu();
    // The supported DragDropHandlers contract exposes explicit Copy/Move
    // commands only. VelocityCopy must never replace Explorer's native default
    // using undocumented numeric command IDs.
    for (bool ansi : {false, true}) {
        for (UINT command : {0u, 1u}) {
            data->ansi = ansi;
            ok &= check(SUCCEEDED(init->Initialize(target, data, nullptr)), "Initialize");
            InsertMenuW(menu, 0, MF_BYPOSITION | MF_STRING, 77, L"Native");
            SetMenuDefaultItem(menu, 77, FALSE);
            HRESULT hr = context->QueryContextMenu(menu, 1, 100, 101, 0);
            ok &= check(SUCCEEDED(hr) && HRESULT_CODE(hr) == 2, "two menu IDs");
            ok &= check(GetMenuDefaultItem(menu, FALSE, 0) == 77, "native default preserved");
            char verb[40]{};
            ok &= check(SUCCEEDED(context->GetCommandString(command, GCS_VERBA, nullptr, verb, 40)) &&
                std::strcmp(verb, command ? "velocitycopy.move" : "velocitycopy.copy") == 0,
                "canonical verb");

            wchar_t wide_verb[40]{};
            ok &= check(SUCCEEDED(context->GetCommandString(command, GCS_VERBW, nullptr,
                reinterpret_cast<LPSTR>(wide_verb), 40)) &&
                std::wcscmp(wide_verb, command ? L"velocitycopy.move" : L"velocitycopy.copy") == 0,
                "canonical Unicode verb");
            ok &= check(FAILED(context->GetCommandString(2, GCS_VERBA, nullptr, verb, 40)),
                "reject out-of-range command string");

            velocitycopy::ShellIpcServer server;
            if (!server.valid()) return false;
            std::optional<velocitycopy::ShellRequest> received;
            std::jthread receiver([&] { received = server.receive(); });
            CMINVOKECOMMANDINFO info{};
            info.cbSize = sizeof(info);
            info.lpVerb = MAKEINTRESOURCEA(command);
            HRESULT result = context->InvokeCommand(&info);
            if (FAILED(result)) server.stop();
            receiver.join();
            ok &= check(SUCCEEDED(result) && received &&
                received->sources.size() == 2 && received->destination == path &&
                received->sources[0] == L"C:\\Source\\a.txt" &&
                received->operation == (command ? velocitycopy::FileOperation::Move : velocitycopy::FileOperation::Copy),
                "COM to IPC explicit Copy/Move snapshot");
            ok &= check(FAILED(context->InvokeCommand(&info)), "duplicate invocation rejected");
            ok &= check(data->sets == 0, "no premature completion or source deletion");
            while (GetMenuItemCount(menu) > 0) DeleteMenu(menu, 0, MF_BYPOSITION);
        }
    }
    // Exercise the same decoder used after OleGetClipboard without relying
    // on a desktop clipboard being available in a headless CI session.
    data->clipboard_effect = true;
    for (DWORD effect : {DROPEFFECT_COPY, DROPEFFECT_MOVE}) {
        data->preferred_effect = effect;
        velocitycopy::ShellRequest request;
        HRESULT hr = velocitycopy::shell::clipboard_sources_from_data(data, request);
        ok &= check(SUCCEEDED(hr) && request.sources.size() == 2 &&
            request.operation == (effect == DROPEFFECT_MOVE ?
                velocitycopy::FileOperation::Move : velocitycopy::FileOperation::Copy),
            "clipboard copy/move decode");
    }
    if (live_clipboard) {
        {
            for (DWORD effect : {DROPEFFECT_COPY, DROPEFFECT_MOVE}) {
                data->preferred_effect = effect;
                const HRESULT placed = OleSetClipboard(data);
                if (FAILED(placed)) {
                    std::cerr << "OleSetClipboard HRESULT=" << std::hex << placed << std::dec << '\n';
                    ok = false; break;
                }
                ok &= check(SUCCEEDED(init->Initialize(target, nullptr, nullptr)), "live paste initialize");
                ok &= check(HRESULT_CODE(context->QueryContextMenu(menu, 0, 100, 100, 0)) == 1,
                            "live paste menu");
                velocitycopy::ShellIpcServer server;
                if (!server.valid()) { ok = false; break; }
                std::optional<velocitycopy::ShellRequest> received;
                std::jthread receiver([&] { received = server.receive(); });
                CMINVOKECOMMANDINFO info{};
                info.cbSize = sizeof(info);
                info.lpVerb = "velocitycopy.paste";
                HRESULT result = context->InvokeCommand(&info);
                if (FAILED(result)) server.stop();
                receiver.join();
                ok &= check(SUCCEEDED(result) && received && received->sources.size() == 2 &&
                    received->destination == path && received->operation ==
                    (effect == DROPEFFECT_MOVE ? velocitycopy::FileOperation::Move : velocitycopy::FileOperation::Copy),
                    "OLE clipboard through COM paste and IPC");
                while (GetMenuItemCount(menu) > 0) DeleteMenu(menu, 0, MF_BYPOSITION);
            }
            ok &= check(SUCCEEDED(OleSetClipboard(nullptr)), "clear isolated clipboard");
        }
    }
    data->preferred_effect = DROPEFFECT_LINK;
    velocitycopy::ShellRequest invalid;
    ok &= check(FAILED(velocitycopy::shell::clipboard_sources_from_data(data, invalid)),
                "reject link-only clipboard effect");
    ok &= check(FAILED(velocitycopy::shell::clipboard_sources_from_data(nullptr, invalid)),
                "reject missing data object");
    data->clipboard_effect = false;
    ok &= check(SUCCEEDED(velocitycopy::shell::clipboard_sources_from_data(data, invalid)) &&
                invalid.operation == velocitycopy::FileOperation::Copy, "default clipboard copy");
    ok &= check(SUCCEEDED(init->Initialize(target, nullptr, nullptr)), "background initialize");
    ok &= check(HRESULT_CODE(context->QueryContextMenu(menu, 0, 100, 100, 0)) == 1,
                "background paste uses one menu ID");
    char paste_verb[40]{};
    ok &= check(SUCCEEDED(context->GetCommandString(0, GCS_VERBA, nullptr, paste_verb, 40)) &&
                std::strcmp(paste_verb, "velocitycopy.paste") == 0, "background paste verb");
    CMINVOKECOMMANDINFO wrong_verb{};
    wrong_verb.cbSize = sizeof(wrong_verb);
    wrong_verb.lpVerb = "velocitycopy.copy";
    ok &= check(context->InvokeCommand(&wrong_verb) == E_INVALIDARG,
                "background rejects drag-copy verb before reading clipboard");
    while (GetMenuItemCount(menu) > 0) DeleteMenu(menu, 0, MF_BYPOSITION);
    ok &= check(SUCCEEDED(init->Initialize(target, data, nullptr)), "reset");
    ok &= check(HRESULT_CODE(context->QueryContextMenu(menu, 0, 100, 100, 0)) == 0, "insufficient IDs");
    ok &= check(HRESULT_CODE(context->QueryContextMenu(menu, 0, 100, 101, CMF_DEFAULTONLY)) == 0, "default only");
    data->truncated = true;
    for (bool use_ansi : {false, true}) {
        data->ansi = use_ansi;
        ok &= check(FAILED(init->Initialize(target, data, nullptr)),
                    use_ansi ? "truncated ANSI CF_HDROP" : "truncated Unicode CF_HDROP");
    }
    data->truncated = false;
    data->malformed = true;
    ok &= check(FAILED(init->Initialize(target, data, nullptr)), "malformed DROPFILES");
    ok &= check(HRESULT_CODE(context->QueryContextMenu(menu, 0, 100, 101, 0)) == 0, "no stale paths");
    ok &= check(FAILED(init->Initialize(nullptr, data, nullptr)), "no destination");
    DestroyMenu(menu);
    CoTaskMemFree(target);
    data->Release(); context->Release(); init->Release();
    return ok && unload() == S_OK;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) return 10;
    const bool live_clipboard = argc == 3 && std::wcscmp(argv[2], L"--clipboard") == 0;
    HWINSTA original = GetProcessWindowStation();
    HWINSTA isolated = live_clipboard ? CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr) : nullptr;
    if (live_clipboard && (!isolated || !SetProcessWindowStation(isolated))) return 12;
    if (FAILED(OleInitialize(nullptr))) return 10;
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) return 11;
    auto get = reinterpret_cast<GetClass>(GetProcAddress(dll, "DllGetClassObject"));
    auto unload = reinterpret_cast<CanUnload>(GetProcAddress(dll, "DllCanUnloadNow"));
    bool ok = !GetProcAddress(dll, "VelocityCopyDecodeClipboardForTest") && get && unload && run(get, unload, live_clipboard);
    FreeLibrary(dll); OleUninitialize();
    if (isolated) { SetProcessWindowStation(original); CloseWindowStation(isolated); }
    return ok ? 0 : 1;
}
