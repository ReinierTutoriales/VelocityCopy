#include "velocitycopy/ipc_transport.hpp"
#include "velocitycopy/shell_request.hpp"

#include <windows.h>
#include <shobjidl.h>

#include <atomic>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <new>
#include <utility>
#include <vector>

namespace {

constexpr CLSID CLSID_VelocityCopyCopy =
    {0x7e1d27a7, 0xba17, 0x4eea, {0x9b, 0x93, 0x96, 0x7e, 0xe7, 0x77, 0xbd, 0x21}};
constexpr CLSID CLSID_VelocityCopyPaste =
    {0xcbba1a7e, 0x35b4, 0x4708, {0x9d, 0x03, 0x94, 0x46, 0xd0, 0x3f, 0xc8, 0x43}};

std::atomic<long> g_object_count{0};

enum class CommandKind {
    Copy,
    Paste,
};

HRESULT duplicate_string(const wchar_t* text, PWSTR* result) noexcept {
    if (result == nullptr) {
        return E_POINTER;
    }
    *result = nullptr;
    const auto length = std::wcslen(text);
    const auto bytes = (length + 1) * sizeof(wchar_t);
    auto* memory = static_cast<wchar_t*>(CoTaskMemAlloc(bytes));
    if (memory == nullptr) {
        return E_OUTOFMEMORY;
    }
    std::memcpy(memory, text, bytes);
    *result = memory;
    return S_OK;
}

HRESULT shell_item_paths(IShellItemArray* items, std::vector<std::filesystem::path>& paths) noexcept {
    if (items == nullptr) {
        return E_INVALIDARG;
    }

    DWORD count = 0;
    HRESULT hr = items->GetCount(&count);
    if (FAILED(hr) || count == 0) {
        return FAILED(hr) ? hr : E_INVALIDARG;
    }

    try {
        paths.clear();
        paths.reserve(count);
        for (DWORD index = 0; index < count; ++index) {
            IShellItem* item = nullptr;
            hr = items->GetItemAt(index, &item);
            if (FAILED(hr)) {
                return hr;
            }

            PWSTR path = nullptr;
            hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
            item->Release();
            if (FAILED(hr)) {
                return hr;
            }

            paths.emplace_back(path);
            CoTaskMemFree(path);
        }
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

class ExplorerCommand final : public IExplorerCommand {
public:
    explicit ExplorerCommand(CommandKind kind) noexcept : kind_(kind) {
        ++g_object_count;
    }

    ~ExplorerCommand() {
        --g_object_count;
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == IID_IUnknown || riid == __uuidof(IExplorerCommand)) {
            *object = static_cast<IExplorerCommand*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override {
        return ++ref_count_;
    }

    IFACEMETHODIMP_(ULONG) Release() override {
        const auto remaining = --ref_count_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    IFACEMETHODIMP GetTitle(IShellItemArray*, PWSTR* title) override {
        return duplicate_string(
            kind_ == CommandKind::Copy ? L"Copiar con VelocityCopy" : L"Pegar con VelocityCopy",
            title);
    }

    IFACEMETHODIMP GetIcon(IShellItemArray*, PWSTR* icon) override {
        if (icon == nullptr) {
            return E_POINTER;
        }
        *icon = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetToolTip(IShellItemArray*, PWSTR* tooltip) override {
        if (tooltip == nullptr) {
            return E_POINTER;
        }
        *tooltip = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetCanonicalName(GUID* canonical_name) override {
        if (canonical_name == nullptr) {
            return E_POINTER;
        }
        *canonical_name = kind_ == CommandKind::Copy ? CLSID_VelocityCopyCopy : CLSID_VelocityCopyPaste;
        return S_OK;
    }

    IFACEMETHODIMP GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override {
        if (state == nullptr) {
            return E_POINTER;
        }
        if (items == nullptr) {
            *state = ECS_DISABLED;
            return S_OK;
        }
        DWORD count = 0;
        const HRESULT hr = items->GetCount(&count);
        *state = SUCCEEDED(hr) && count != 0 ? ECS_ENABLED : ECS_DISABLED;
        return S_OK;
    }

    IFACEMETHODIMP Invoke(IShellItemArray* items, IBindCtx*) override {
        std::vector<std::filesystem::path> paths;
        HRESULT hr = shell_item_paths(items, paths);
        if (FAILED(hr)) {
            return hr;
        }

        velocitycopy::ShellRequest request{};
        if (kind_ == CommandKind::Copy) {
            request.action = velocitycopy::ShellAction::CopySelection;
            request.sources = std::move(paths);
        } else {
            request.action = velocitycopy::ShellAction::PasteToFolder;
            request.destination = paths.front();
        }

        if (!velocitycopy::send_shell_request(request, 50)) {
            return HRESULT_FROM_WIN32(ERROR_PIPE_NOT_CONNECTED);
        }
        return S_OK;
    }

    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override {
        if (flags == nullptr) {
            return E_POINTER;
        }
        *flags = ECF_DEFAULT;
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** commands) override {
        if (commands == nullptr) {
            return E_POINTER;
        }
        *commands = nullptr;
        return E_NOTIMPL;
    }

private:
    std::atomic<ULONG> ref_count_{1};
    CommandKind kind_;
};

class CommandFactory final : public IClassFactory {
public:
    explicit CommandFactory(CommandKind kind) noexcept : kind_(kind) {
        ++g_object_count;
    }

    ~CommandFactory() {
        --g_object_count;
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *object = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override {
        return ++ref_count_;
    }

    IFACEMETHODIMP_(ULONG) Release() override {
        const auto remaining = --ref_count_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** object) override {
        if (outer != nullptr) {
            return CLASS_E_NOAGGREGATION;
        }
        auto* command = new (std::nothrow) ExplorerCommand(kind_);
        if (command == nullptr) {
            return E_OUTOFMEMORY;
        }
        const HRESULT hr = command->QueryInterface(riid, object);
        command->Release();
        return hr;
    }

    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) {
            ++g_object_count;
        } else {
            --g_object_count;
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_count_{1};
    CommandKind kind_;
};

} // namespace

STDAPI DllCanUnloadNow() {
    return g_object_count.load() == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** object) {
    CommandKind kind{};
    if (clsid == CLSID_VelocityCopyCopy) {
        kind = CommandKind::Copy;
    } else if (clsid == CLSID_VelocityCopyPaste) {
        kind = CommandKind::Paste;
    } else {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    auto* factory = new (std::nothrow) CommandFactory(kind);
    if (factory == nullptr) {
        return E_OUTOFMEMORY;
    }
    const HRESULT hr = factory->QueryInterface(riid, object);
    factory->Release();
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
