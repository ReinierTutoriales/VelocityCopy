#include "pch.h"
#include "MainWindow.xaml.h"

#include <ole2.h>
#include <shlobj_core.h>

// The custom title bar (logo, file name and the empty band up to the caption
// buttons) is a non-client drag region: Windows routes pointer input there to
// the top-level window, not to the XAML content, so the XAML Drop handlers
// never see a drag over it. A native OLE drop target on the top-level window
// accepts Explorer files there and hands them to the same append path as the
// rest of the window. Drops over the content keep reaching the XAML handlers
// because OLE resolves the innermost registered window under the pointer.

namespace winrt::VelocityCopyUI::implementation {
namespace {

// CLSID_DragDropHelper; spelled out because the shell headers only declare it
// in the full (non-LEAN) shobjidl.h and it would otherwise need uuid.lib.
constexpr CLSID kDragDropHelper{0x4657278A, 0x411B, 0x11D2, {0x83, 0x9A, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0}};

std::vector<std::filesystem::path> dropped_paths(IDataObject* data) {
    std::vector<std::filesystem::path> paths;
    if (data == nullptr) return paths;
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&format, &medium))) return paths;
    if (auto* drop = static_cast<HDROP>(GlobalLock(medium.hGlobal))) {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        paths.reserve(count);
        for (UINT index = 0; index < count; ++index) {
            const UINT length = DragQueryFileW(drop, index, nullptr, 0);
            if (length == 0) continue;
            std::wstring path(length + 1, L'\0');
            if (DragQueryFileW(drop, index, path.data(), length + 1) == length) {
                path.resize(length);
                paths.emplace_back(std::move(path));
            }
        }
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    return paths;
}

bool has_files(IDataObject* data) {
    if (data == nullptr) return false;
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return data->QueryGetData(&format) == S_OK;
}

struct CaptionDropTarget : winrt::implements<CaptionDropTarget, ::IDropTarget> {
    CaptionDropTarget(winrt::weak_ref<MainWindow> owner, HWND hwnd) : owner_(std::move(owner)), hwnd_(hwnd) {
        // Explorer's drag image follows the pointer over the title bar too.
        (void)CoCreateInstance(kDragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(helper_.put()));
    }

    HRESULT __stdcall DragEnter(IDataObject* data, DWORD, POINTL point, DWORD* effect) noexcept override {
        files_ = has_files(data);
        *effect = Decide(*effect);
        if (helper_) {
            POINT at{point.x, point.y};
            (void)helper_->DragEnter(hwnd_, data, &at, *effect);
        }
        return S_OK;
    }

    HRESULT __stdcall DragOver(DWORD, POINTL point, DWORD* effect) noexcept override {
        *effect = Decide(*effect);
        if (helper_) {
            POINT at{point.x, point.y};
            (void)helper_->DragOver(&at, *effect);
        }
        return S_OK;
    }

    HRESULT __stdcall DragLeave() noexcept override {
        files_ = false;
        if (helper_) (void)helper_->DragLeave();
        return S_OK;
    }

    HRESULT __stdcall Drop(IDataObject* data, DWORD, POINTL point, DWORD* effect) noexcept override {
        *effect = Decide(*effect);
        if (helper_) {
            POINT at{point.x, point.y};
            (void)helper_->Drop(data, &at, *effect);
        }
        files_ = false;
        if (*effect == DROPEFFECT_NONE) return S_OK;
        try {
            auto paths = dropped_paths(data);
            if (auto owner = owner_.get(); owner && !owner->AppendDroppedSources(std::move(paths))) {
                *effect = DROPEFFECT_NONE;
            }
        } catch (...) {
            *effect = DROPEFFECT_NONE;
        }
        return S_OK;
    }

private:
    DWORD Decide(const DWORD allowed) const noexcept {
        if (!files_ || (allowed & DROPEFFECT_COPY) == 0) return DROPEFFECT_NONE;
        auto owner = owner_.get();
        return owner && owner->AcceptsDroppedSources() ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    }

    winrt::weak_ref<MainWindow> owner_;
    HWND hwnd_{};
    winrt::com_ptr<IDropTargetHelper> helper_;
    bool files_{};
};

} // namespace

void MainWindow::RegisterCaptionDropTarget() noexcept {
    if (hwnd_ == nullptr || caption_drop_target_) return;
    try {
        // Ref-counted; the WinUI thread is already an OLE STA.
        if (FAILED(OleInitialize(nullptr))) return;
        auto target = winrt::make_self<CaptionDropTarget>(get_weak(), hwnd_);
        const HRESULT registered = RegisterDragDrop(hwnd_, target.as<::IDropTarget>().get());
        if (FAILED(registered)) {
            OleUninitialize();
            return;
        }
        caption_drop_target_ = target.as<::IUnknown>();
    } catch (...) {
    }
}

void MainWindow::RevokeCaptionDropTarget() noexcept {
    if (!caption_drop_target_) return;
    if (hwnd_ != nullptr) (void)RevokeDragDrop(hwnd_);
    caption_drop_target_ = nullptr;
    OleUninitialize();
}

bool MainWindow::AcceptsDroppedSources() const noexcept {
    return !queue_drag_active_ && !tray_exit_requested_ && !active_destination_.empty() &&
        (execution_control_ || live_plan_);
}

bool MainWindow::AppendDroppedSources(std::vector<std::filesystem::path> sources) {
    if (!AcceptsDroppedSources() || !append_gate_ || cancel_requested_.load(std::memory_order_relaxed)) return false;
    std::erase_if(sources, [](const std::filesystem::path& path) { return path.empty(); });
    if (sources.empty()) return false;
    velocitycopy::CopyJob job{};
    job.id = next_job_id_++;
    job.sources = std::move(sources);
    job.destination = active_destination_;
    job.operation = active_operation_;
    AppendTransfer(std::move(job));
    return true;
}

} // namespace winrt::VelocityCopyUI::implementation
