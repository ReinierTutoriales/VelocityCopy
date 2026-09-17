#include "velocitycopy/ipc_transport.hpp"
#include "velocitycopy/process_activation.hpp"
#include "velocitycopy/shell_request.hpp"
#include "resource.h"

#include <windows.h>
#include <servprov.h>
#include <shobjidl.h>

#include <array>
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
constexpr CLSID CLSID_VelocityCopyCopyTo =
    {0xd0b92e7d, 0x7a23, 0x4c9a, {0x9a, 0xe2, 0x2b, 0x2a, 0x1a, 0x6f, 0x3a, 0x0d}};
constexpr CLSID CLSID_VelocityCopyOpen =
    {0xa6209c12, 0x10b0, 0x4d25, {0x8b, 0xf3, 0x2d, 0x3c, 0x3e, 0x6a, 0x7b, 0x11}};

std::atomic<long> g_object_count{0};
HINSTANCE g_module{};

enum class CommandKind {
    Copy,
    Paste,
    CopyTo,
    Open,
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

HRESULT localized_string(
    const UINT resource_id,
    const wchar_t* fallback,
    PWSTR* result) noexcept {
    std::array<wchar_t, 128> buffer{};
    const int length = LoadStringW(
        g_module,
        resource_id,
        buffer.data(),
        static_cast<int>(buffer.size()));
    return duplicate_string(length > 0 ? buffer.data() : fallback, result);
}

std::filesystem::path velocitycopy_executable() noexcept {
    try {
        std::array<wchar_t, 32768> buffer{};
        const DWORD length = GetModuleFileNameW(g_module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return {};
        }
        auto path = std::filesystem::path(std::wstring_view(buffer.data(), length));
        return path.parent_path() / L"VelocityCopy.WinUI.exe";
    } catch (...) {
        return {};
    }
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

HRESULT site_folder_paths(IUnknown* site, std::vector<std::filesystem::path>& paths) noexcept {
    if (site == nullptr) {
        return E_FAIL;
    }

    IServiceProvider* provider = nullptr;
    HRESULT hr = site->QueryInterface(IID_PPV_ARGS(&provider));
    if (FAILED(hr) || provider == nullptr) {
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    IFolderView* folder_view = nullptr;
    hr = provider->QueryService(SID_SFolderView, IID_PPV_ARGS(&folder_view));
    provider->Release();
    if (FAILED(hr) || folder_view == nullptr) {
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    IShellItemArray* folder_items = nullptr;
    hr = folder_view->GetFolder(IID_PPV_ARGS(&folder_items));
    folder_view->Release();
    if (FAILED(hr) || folder_items == nullptr) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    hr = shell_item_paths(folder_items, paths);
    folder_items->Release();
    return hr;
}

bool dispatch_request(const velocitycopy::ShellRequest& request) noexcept {
    if (velocitycopy::send_shell_request(request, 25)) {
        return true;
    }
    const auto executable = velocitycopy_executable();
    return !executable.empty() && velocitycopy::launch_velocitycopy_with_request(executable, request);
}

class ExplorerCommand final : public IExplorerCommand, public IObjectWithSite {
public:
    explicit ExplorerCommand(CommandKind kind) noexcept : kind_(kind) {
        ++g_object_count;
    }

    ~ExplorerCommand() {
        if (site_ != nullptr) {
            site_->Release();
        }
        --g_object_count;
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == IID_IUnknown || riid == __uuidof(IExplorerCommand)) {
            *object = static_cast<IExplorerCommand*>(this);
        } else if (riid == __uuidof(IObjectWithSite)) {
            *object = static_cast<IObjectWithSite*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
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

    IFACEMETHODIMP SetSite(IUnknown* site) override {
        if (site != nullptr) {
            site->AddRef();
        }
        if (site_ != nullptr) {
            site_->Release();
        }
        site_ = site;
        return S_OK;
    }

    IFACEMETHODIMP GetSite(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        return site_ != nullptr ? site_->QueryInterface(riid, object) : E_FAIL;
    }

    IFACEMETHODIMP GetTitle(IShellItemArray*, PWSTR* title) override {
        switch (kind_) {
        case CommandKind::Copy:
            return localized_string(IDS_SHELL_COPY_WITH_VELOCITYCOPY, L"Copy with VelocityCopy", title);
        case CommandKind::Paste:
            return localized_string(IDS_SHELL_PASTE_WITH_VELOCITYCOPY, L"Paste with VelocityCopy", title);
        case CommandKind::CopyTo:
            return localized_string(IDS_SHELL_COPY_TO_VELOCITYCOPY, L"Copy to... with VelocityCopy", title);
        case CommandKind::Open:
            return localized_string(IDS_SHELL_OPEN_VELOCITYCOPY, L"Open VelocityCopy", title);
        }
        return E_UNEXPECTED;
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
        switch (kind_) {
        case CommandKind::Copy:
            *canonical_name = CLSID_VelocityCopyCopy;
            break;
        case CommandKind::Paste:
            *canonical_name = CLSID_VelocityCopyPaste;
            break;
        case CommandKind::CopyTo:
            *canonical_name = CLSID_VelocityCopyCopyTo;
            break;
        case CommandKind::Open:
            *canonical_name = CLSID_VelocityCopyOpen;
            break;
        }
        return S_OK;
    }

    IFACEMETHODIMP GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override {
        if (state == nullptr) {
            return E_POINTER;
        }

        if (kind_ == CommandKind::Open) {
            *state = ECS_ENABLED;
            return S_OK;
        }

        if (items != nullptr) {
            DWORD count = 0;
            const HRESULT hr = items->GetCount(&count);
            *state = SUCCEEDED(hr) && count != 0 ? ECS_ENABLED : ECS_DISABLED;
            return S_OK;
        }

        if (kind_ == CommandKind::Paste) {
            std::vector<std::filesystem::path> paths;
            *state = SUCCEEDED(site_folder_paths(site_, paths)) && !paths.empty()
                ? ECS_ENABLED
                : ECS_DISABLED;
            return S_OK;
        }

        *state = ECS_DISABLED;
        return S_OK;
    }

    IFACEMETHODIMP Invoke(IShellItemArray* items, IBindCtx*) override {
        velocitycopy::ShellRequest request{};
        if (kind_ == CommandKind::Open) {
            request.action = velocitycopy::ShellAction::OpenVelocityCopy;
            return dispatch_request(request) ? S_OK : HRESULT_FROM_WIN32(ERROR_OPEN_FAILED);
        }

        std::vector<std::filesystem::path> paths;
        HRESULT hr = items != nullptr
            ? shell_item_paths(items, paths)
            : (kind_ == CommandKind::Paste ? site_folder_paths(site_, paths) : E_INVALIDARG);
        if (FAILED(hr) || paths.empty()) {
            return FAILED(hr) ? hr : E_INVALIDARG;
        }

        switch (kind_) {
        case CommandKind::Copy:
            request.action = velocitycopy::ShellAction::CopySelection;
            request.sources = std::move(paths);
            break;
        case CommandKind::CopyTo:
            request.action = velocitycopy::ShellAction::CopySelectionPromptDestination;
            request.sources = std::move(paths);
            break;
        case CommandKind::Paste:
            request.action = velocitycopy::ShellAction::PasteToFolder;
            request.destination = paths.front();
            break;
        case CommandKind::Open:
            return E_UNEXPECTED;
        }

        return dispatch_request(request) ? S_OK : HRESULT_FROM_WIN32(ERROR_OPEN_FAILED);
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
    IUnknown* site_{};
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
    } else if (clsid == CLSID_VelocityCopyCopyTo) {
        kind = CommandKind::CopyTo;
    } else if (clsid == CLSID_VelocityCopyOpen) {
        kind = CommandKind::Open;
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
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
