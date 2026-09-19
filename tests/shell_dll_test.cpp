#include "../src/shell/drop_handler.hpp"
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
    DWORD preferred{DROPEFFECT_MOVE};
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
        if (!drop && f->cfFormat != RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)) return DV_E_FORMATETC;
        const wchar_t paths[] = L"C:\\Source\\a.txt\0C:\\Source\\Folder\0";
        const char narrow[] = "C:\\Source\\a.txt\0C:\\Source\\Folder\0";
        SIZE_T bytes = drop ? sizeof(DROPFILES) + (ansi ? sizeof(narrow) : sizeof(paths)) : sizeof(DWORD);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        if (!memory) return E_OUTOFMEMORY;
        auto* raw = static_cast<unsigned char*>(GlobalLock(memory));
        if (!raw) { GlobalFree(memory); return E_OUTOFMEMORY; }
        if (drop) {
            DROPFILES header{};
            header.pFiles = malformed ? static_cast<DWORD>(bytes + 8) : sizeof(DROPFILES);
            header.fWide = !ansi;
            std::memcpy(raw, &header, sizeof(header));
            if (ansi) std::memcpy(raw + sizeof(header), narrow, sizeof(narrow));
            else std::memcpy(raw + sizeof(header), paths, sizeof(paths));
        } else std::memcpy(raw, &preferred, sizeof(preferred));
        GlobalUnlock(memory);
        out->tymed = TYMED_HGLOBAL; out->hGlobal = memory;
        return S_OK;
    }
    IFACEMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    IFACEMETHODIMP QueryGetData(FORMATETC* f) override {
        return f && f->cfFormat == RegisterClipboardFormatW(CFSTR_SHELLIDLIST) ? S_OK : DV_E_FORMATETC;
    }
    IFACEMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    IFACEMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { ++sets; return E_NOTIMPL; }
    IFACEMETHODIMP EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    IFACEMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    IFACEMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    IFACEMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};
bool run(GetClass get, CanUnload unload) {
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
    // Exercise the exact historical default switching separately from a claim
    // about whether Windows 11 Explorer calls this path for Ctrl+V.
    for (UINT native : {1u, 2u, 3u, 99u}) {
        for (bool ansi : {false, true}) {
            data->ansi = ansi;
            ok &= check(SUCCEEDED(init->Initialize(target, data, nullptr)), "Initialize");
            InsertMenuW(menu, 0, MF_BYPOSITION | MF_STRING, native, L"Native");
            SetMenuDefaultItem(menu, native, FALSE);
            HRESULT hr = context->QueryContextMenu(menu, 1, 100, 101, 0);
            ok &= check(SUCCEEDED(hr) && HRESULT_CODE(hr) == 2, "two menu IDs");
            ok &= check(GetMenuDefaultItem(menu, FALSE, 0) == (native <= 2 ? 100 + native - 1 : native), "default selection");
            char verb[40]{};
            ok &= check(SUCCEEDED(context->GetCommandString(0, GCS_VERBA, nullptr, verb, 40)) &&
                std::strcmp(verb, "velocitycopy.copy") == 0, "canonical verb");
            if (native <= 2) {
                velocitycopy::ShellIpcServer server;
                if (!server.valid()) return false;
                std::optional<velocitycopy::ShellRequest> received;
                std::jthread receiver([&] { received = server.receive(); });
                CMINVOKECOMMANDINFO info{};
                info.cbSize = sizeof(info);
                info.lpVerb = MAKEINTRESOURCEA(native - 1);
                HRESULT result = context->InvokeCommand(&info);
                if (FAILED(result)) server.stop();
                receiver.join();
                ok &= check(SUCCEEDED(result) && received &&
                    received->sources.size() == 2 && received->destination == path &&
                    received->sources[0] == L"C:\\Source\\a.txt" &&
                    received->operation == (native == 2 ? velocitycopy::FileOperation::Move : velocitycopy::FileOperation::Copy),
                    "COM to IPC Copy/Move snapshot");
                ok &= check(FAILED(context->InvokeCommand(&info)), "duplicate invocation rejected");
                ok &= check(data->sets == 0, "no premature completion or source deletion");
            }
            while (GetMenuItemCount(menu) > 0) DeleteMenu(menu, 0, MF_BYPOSITION);
        }
    }
    ok &= check(SUCCEEDED(init->Initialize(target, data, nullptr)), "reset");
    ok &= check(HRESULT_CODE(context->QueryContextMenu(menu, 0, 100, 100, 0)) == 0, "insufficient IDs");
    ok &= check(HRESULT_CODE(context->QueryContextMenu(menu, 0, 100, 101, CMF_DEFAULTONLY)) == 0, "default only");
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
    if (argc != 2 || FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 10;
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) return 11;
    auto get = reinterpret_cast<GetClass>(GetProcAddress(dll, "DllGetClassObject"));
    auto unload = reinterpret_cast<CanUnload>(GetProcAddress(dll, "DllCanUnloadNow"));
    bool ok = get && unload && run(get, unload);
    FreeLibrary(dll); CoUninitialize();
    return ok ? 0 : 1;
}
