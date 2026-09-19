#include "drop_handler.hpp"
#include "resource.h"
#include "velocitycopy/ipc_transport.hpp"
#include "velocitycopy/process_activation.hpp"
#include "velocitycopy/ipc_protocol.hpp"
#include <shlobj.h>
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

struct Medium {
    STGMEDIUM value{};
    ~Medium() { if (value.tymed != TYMED_NULL) ReleaseStgMedium(&value); }
};
struct GlobalView {
    HGLOBAL handle;
    const void* data;
    explicit GlobalView(HGLOBAL h) : handle(h), data(GlobalLock(h)) {}
    ~GlobalView() { if (data) GlobalUnlock(handle); }
};

// Parse the bounded HGLOBAL, not arbitrary null-terminated memory supplied by a
// third-party data object. Snapshot paths; never retain the IDataObject in IPC.
HRESULT read_sources(IDataObject* data, std::vector<std::filesystem::path>& paths) {
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    Medium medium;
    HRESULT hr = data->GetData(&format, &medium.value);
    if (FAILED(hr)) return hr;
    if (medium.value.tymed != TYMED_HGLOBAL || !medium.value.hGlobal) return DV_E_TYMED;
    const SIZE_T bytes = GlobalSize(medium.value.hGlobal);
    if (bytes < sizeof(DROPFILES) || bytes > velocitycopy::kMaxShellMessageBytes) return E_INVALIDARG;
    GlobalView view(medium.value.hGlobal);
    if (!view.data) return E_OUTOFMEMORY;
    DROPFILES header{};
    std::memcpy(&header, view.data, sizeof(header));
    const size_t unit = header.fWide ? sizeof(wchar_t) : sizeof(char);
    if (header.pFiles < sizeof(header) || header.pFiles > bytes - unit * 2 ||
        (header.fWide && header.pFiles % sizeof(wchar_t))) return E_INVALIDARG;
    const auto* raw = static_cast<const unsigned char*>(view.data);
    size_t pos = header.pFiles;
    auto character = [&](size_t offset) {
        wchar_t value{};
        std::memcpy(&value, raw + offset, unit);
        return value;
    };
    while (pos + unit <= bytes && character(pos) != 0) {
        const size_t start = pos;
        while (pos + unit <= bytes && character(pos) != 0) pos += unit;
        if (pos + unit > bytes || (pos - start) / unit > velocitycopy::kMaxShellPathChars ||
            paths.size() >= velocitycopy::kMaxShellSources) return E_INVALIDARG;
        std::wstring path;
        if (header.fWide) {
            path.resize((pos - start) / unit);
            std::memcpy(path.data(), raw + start, pos - start);
        } else {
            const auto* ansi = reinterpret_cast<const char*>(raw + start);
            const int length = static_cast<int>(pos - start);
            const int wide = MultiByteToWideChar(CP_ACP, 0, ansi, length, nullptr, 0);
            if (wide <= 0) return E_INVALIDARG;
            path.resize(wide);
            if (!MultiByteToWideChar(CP_ACP, 0, ansi, length, path.data(), wide)) return E_INVALIDARG;
        }
        std::filesystem::path source(std::move(path));
        if (!source.is_absolute()) return E_INVALIDARG;
        paths.push_back(std::move(source));
        pos += unit;
    }
    return !paths.empty() && pos + unit <= bytes && character(pos) == 0 ? S_OK : E_INVALIDARG;
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
    bool shell_source_{};
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
        ready_ = offered_ = invoked_ = false;
        shell_source_ = false;
        request_ = {};
        if (!folder || !data) return E_INVALIDARG;
        try {
            std::array<wchar_t, 32768> target{};
            if (!SHGetPathFromIDListEx(folder, target.data(), static_cast<DWORD>(target.size()), GPFIDL_DEFAULT))
                return E_INVALIDARG;
            request_.action = velocitycopy::ShellAction::Transfer;
            request_.destination = target.data();
            FORMATETC shell_items{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_SHELLIDLIST)),
                nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            shell_source_ = data->QueryGetData(&shell_items) == S_OK;
            HRESULT hr = read_sources(data, request_.sources);
            if (FAILED(hr)) { request_ = {}; return hr; }
            // Explicit commands decide Copy/Move. Preference is only a hint for
            // the historical default-command path, never permission to delete.
            FORMATETC effect{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)),
                nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            Medium medium;
            if (SUCCEEDED(data->GetData(&effect, &medium.value)) && medium.value.tymed == TYMED_HGLOBAL &&
                medium.value.hGlobal && GlobalSize(medium.value.hGlobal) >= sizeof(DWORD)) {
                GlobalView view(medium.value.hGlobal);
                DWORD preferred{};
                if (view.data) std::memcpy(&preferred, view.data, sizeof(preferred));
                if (preferred == DROPEFFECT_MOVE) request_.operation = velocitycopy::FileOperation::Move;
            }
            ready_ = velocitycopy::shell_request_valid(request_);
            return ready_ ? S_OK : E_INVALIDARG;
        } catch (...) { request_ = {}; return E_OUTOFMEMORY; }
    }
    IFACEMETHODIMP QueryContextMenu(HMENU menu, UINT index, UINT first, UINT last, UINT flags) override {
        offered_ = false;
        if (!ready_ || invoked_ || (flags & CMF_DEFAULTONLY) || first > last || last - first < 1)
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        // SuperCopier2's compatibility technique: Explorer historically used
        // default command 1 for Copy and 2 for Move. These IDs are NOT a public
        // Windows contract. Unknown IDs leave the native default untouched.
        const UINT native_default = GetMenuDefaultItem(menu, FALSE, 0);
        std::array<wchar_t, 128> copy{}, move{};
        if (!LoadStringW(module, IDS_SHELL_COPY_HERE, copy.data(), static_cast<int>(copy.size())) ||
            !LoadStringW(module, IDS_SHELL_MOVE_HERE, move.data(), static_cast<int>(move.size()))) return E_FAIL;
        if (!InsertMenuW(menu, index, MF_BYPOSITION | MF_STRING, first, copy.data())) return E_FAIL;
        if (!InsertMenuW(menu, index + 1, MF_BYPOSITION | MF_STRING, first + 1, move.data())) {
            DeleteMenu(menu, index, MF_BYPOSITION); return E_FAIL;
        }
        offered_ = true;
        if (shell_source_ && (native_default == 1 || native_default == 2)) {
            (void)SetMenuDefaultItem(menu, first + (native_default - 1), FALSE);
        }
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 2);
    }
    IFACEMETHODIMP GetCommandString(UINT_PTR command, UINT flags, UINT*, LPSTR text, UINT count) override {
        if (command > 1) return E_INVALIDARG;
        if (flags == GCS_VALIDATEA || flags == GCS_VALIDATEW) return offered_ ? S_OK : S_FALSE;
        if (!text || !count) return E_POINTER;
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
                if (!lstrcmpiW(ex->lpVerbW, L"velocitycopy.copy")) command = 0;
                else if (!lstrcmpiW(ex->lpVerbW, L"velocitycopy.move")) command = 1;
            }
        } else {
            if (!lstrcmpiA(info->lpVerb, "velocitycopy.copy")) command = 0;
            else if (!lstrcmpiA(info->lpVerb, "velocitycopy.move")) command = 1;
        }
        if (command > 1) return E_INVALIDARG;
        request_.operation = command ? velocitycopy::FileOperation::Move : velocitycopy::FileOperation::Copy;
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
