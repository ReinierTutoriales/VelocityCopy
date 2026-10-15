#include "drop_handler.hpp"
#include "resource.h"
#include "shell_data.hpp"
#include "velocitycopy/ipc_transport.hpp"
#include "velocitycopy/process_activation.hpp"
#include "velocitycopy/ipc_protocol.hpp"
#include <shlobj.h>
#include <ole2.h>
#include <strsafe.h>
#include <array>
#include <atomic>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {
std::atomic<long> objects{0};
HINSTANCE module{};

HRESULT clipboard_sources(velocitycopy::ShellRequest& request) {
    IDataObject* object = nullptr;
    HRESULT hr = OleGetClipboard(&object);
    if (FAILED(hr)) return hr;
    struct Owner { IDataObject* p; ~Owner() { if (p) p->Release(); } } owner{object};
    return velocitycopy::shell::clipboard_sources_from_data(object, request);
}

bool dispatch(const velocitycopy::ShellRequest& request) noexcept {
    if (velocitycopy::send_shell_request(request, 25)) return true;
    try {
        std::array<wchar_t, 32768> path{};
        DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) return false;
        const auto exe = std::filesystem::path(path.data()).parent_path() / L"VelocityCopy.WinUI.exe";
        return velocitycopy::launch_velocitycopy_with_request(exe, request);
    } catch (...) { return false; }
}

class DropHandler final : public IShellExtInit, public IContextMenu {
    std::atomic<ULONG> refs_{1};
    velocitycopy::ShellRequest request_;
    bool ready_{};
    bool offered_{};
    bool invoked_{};
    bool clipboard_mode_{};
public:
    DropHandler() { ++objects; }
    ~DropHandler() { --objects; }
    IFACEMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IShellExtInit)) *out = static_cast<IShellExtInit*>(this);
        else if (iid == __uuidof(IContextMenu)) *out = static_cast<IContextMenu*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG n = --refs_; if (!n) delete this; return n;
    }
    IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE folder, IDataObject* data, HKEY) override {
        ready_ = offered_ = invoked_ = clipboard_mode_ = false;
        request_ = {};
        if (!folder) return E_INVALIDARG;
        try {
            std::array<wchar_t, 32768> target{};
            if (!SHGetPathFromIDListEx(folder, target.data(), static_cast<DWORD>(target.size()), GPFIDL_DEFAULT))
                return E_INVALIDARG;
            request_.action = velocitycopy::ShellAction::Transfer;
            request_.destination = target.data();
            if (!data) {
                clipboard_mode_ = true;
                ready_ = true;
                return S_OK;
            }
            HRESULT hr = velocitycopy::shell::read_sources(data, request_.sources);
            if (FAILED(hr)) { request_ = {}; return hr; }
            ready_ = velocitycopy::shell_request_valid(request_);
            return ready_ ? S_OK : E_INVALIDARG;
        } catch (...) { request_ = {}; return E_OUTOFMEMORY; }
    }
    IFACEMETHODIMP QueryContextMenu(HMENU menu, UINT index, UINT first, UINT last, UINT flags) override {
        offered_ = false;
        if (!ready_ || invoked_ || menu == nullptr || (flags & CMF_DEFAULTONLY) ||
            first > last || (!clipboard_mode_ && last - first < 1) || index == UINT_MAX)
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        if (clipboard_mode_) {
            std::array<wchar_t, 128> paste{};
            if (!LoadStringW(module, IDS_SHELL_PASTE_HERE, paste.data(), static_cast<int>(paste.size()))) return E_FAIL;
            if (!InsertMenuW(menu, index, MF_BYPOSITION | MF_STRING, first, paste.data())) return E_FAIL;
            offered_ = true;
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 1);
        }
        std::array<wchar_t, 128> copy{}, move{};
        if (!LoadStringW(module, IDS_SHELL_COPY_HERE, copy.data(), static_cast<int>(copy.size())) ||
            !LoadStringW(module, IDS_SHELL_MOVE_HERE, move.data(), static_cast<int>(move.size()))) return E_FAIL;
        if (!InsertMenuW(menu, index, MF_BYPOSITION | MF_STRING, first, copy.data())) return E_FAIL;
        if (!InsertMenuW(menu, index + 1, MF_BYPOSITION | MF_STRING, first + 1, move.data())) {
            DeleteMenu(menu, index, MF_BYPOSITION); return E_FAIL;
        }
        offered_ = true;
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 2);
    }
    IFACEMETHODIMP GetCommandString(UINT_PTR command, UINT flags, UINT*, LPSTR text, UINT count) override {
        if (command > (clipboard_mode_ ? 0u : 1u)) return E_INVALIDARG;
        if (flags == GCS_VALIDATEA || flags == GCS_VALIDATEW) return offered_ ? S_OK : S_FALSE;
        if (!text || !count) return E_POINTER;
        if (clipboard_mode_ && flags == GCS_VERBW) return StringCchCopyW(reinterpret_cast<LPWSTR>(text), count, L"velocitycopy.paste");
        if (clipboard_mode_ && flags == GCS_VERBA) return StringCchCopyA(text, count, "velocitycopy.paste");
        if (flags == GCS_VERBW)
            return StringCchCopyW(reinterpret_cast<LPWSTR>(text), count, command ? L"velocitycopy.move" : L"velocitycopy.copy");
        if (flags == GCS_VERBA)
            return StringCchCopyA(text, count, command ? "velocitycopy.move" : "velocitycopy.copy");
        return E_NOTIMPL;
    }
    IFACEMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO info) override {
        if (!info || info->cbSize < sizeof(CMINVOKECOMMANDINFO)) return E_INVALIDARG;
        if (!ready_ || !offered_ || invoked_) return E_UNEXPECTED;
        UINT command = 2;
        if (HIWORD(reinterpret_cast<ULONG_PTR>(info->lpVerb)) == 0) command = LOWORD(reinterpret_cast<ULONG_PTR>(info->lpVerb));
        // CMIC_MASK_UNICODE is SEE_MASK_UNICODE (0x4000). Current SDKs dropped the name.
        else if (info->cbSize >= sizeof(CMINVOKECOMMANDINFOEX) && (info->fMask & 0x00004000u)) {
            auto* ex = reinterpret_cast<const CMINVOKECOMMANDINFOEX*>(info);
            if (ex->lpVerbW && HIWORD(reinterpret_cast<ULONG_PTR>(ex->lpVerbW))) {
                if (!lstrcmpiW(ex->lpVerbW, clipboard_mode_ ? L"velocitycopy.paste" : L"velocitycopy.copy")) command = 0;
                else if (!lstrcmpiW(ex->lpVerbW, L"velocitycopy.move")) command = 1;
            }
        } else {
            if (!lstrcmpiA(info->lpVerb, clipboard_mode_ ? "velocitycopy.paste" : "velocitycopy.copy")) command = 0;
            else if (!lstrcmpiA(info->lpVerb, "velocitycopy.move")) command = 1;
        }
        if (command > (clipboard_mode_ ? 0u : 1u)) return E_INVALIDARG;
        if (clipboard_mode_) {
            try {
                HRESULT hr = clipboard_sources(request_);
                if (FAILED(hr) || !velocitycopy::shell_request_valid(request_)) return FAILED(hr) ? hr : E_INVALIDARG;
            } catch (...) { return E_OUTOFMEMORY; }
        } else request_.operation = command ? velocitycopy::FileOperation::Move : velocitycopy::FileOperation::Copy;
        if (!dispatch(request_)) return HRESULT_FROM_WIN32(ERROR_OPEN_FAILED);
        invoked_ = true;
        // Queue admission is not completion. Do not send PASTESUCCEEDED or a
        // performed MOVE to the source: only the engine owns source deletion.
        return S_OK;
    }
};

class Factory final : public IClassFactory {
    std::atomic<ULONG> refs_{1};
public:
    Factory() { ++objects; }
    ~Factory() { --objects; }
    IFACEMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override { ULONG n = --refs_; if (!n) delete this; return n; }
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* handler = new (std::nothrow) DropHandler;
        if (!handler) return E_OUTOFMEMORY;
        HRESULT hr = handler->QueryInterface(iid, out); handler->Release(); return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override { if (lock) ++objects; else --objects; return S_OK; }
};
}

STDAPI DllCanUnloadNow() { return objects.load() == 0 ? S_OK : S_FALSE; }
STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (clsid != CLSID_VelocityCopyDropHandler) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) Factory;
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid, out); factory->Release(); return hr;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { module = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}
