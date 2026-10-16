#include "shell_data.hpp"
#include "velocitycopy/ipc_protocol.hpp"
#include <cstring>

namespace velocitycopy::shell {
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
    // The loop consumes each pathname's NUL. At this point pos refers to
    // the *final* list NUL, so only one more character is required.
    // The bounded read rejects a missing final terminator.
    return !paths.empty() && pos <= bytes && bytes - pos >= unit &&
                   character(pos) == 0
               ? S_OK : E_INVALIDARG;
}

HRESULT clipboard_sources_from_data(IDataObject* object, velocitycopy::ShellRequest& request) {
    if (!object) return E_POINTER;
    HRESULT hr = S_OK;
    request.sources.clear();
    hr = read_sources(object, request.sources);
    if (FAILED(hr)) return hr;
    request.operation = velocitycopy::FileOperation::Copy;
    const UINT id = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    if (!id) return HRESULT_FROM_WIN32(GetLastError());
    FORMATETC fmt{static_cast<CLIPFORMAT>(id), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    Medium medium;
    hr = object->GetData(&fmt, &medium.value);
    if (hr == DV_E_FORMATETC) return S_OK;
    if (FAILED(hr)) return hr;
    if (medium.value.tymed != TYMED_HGLOBAL || !medium.value.hGlobal ||
        GlobalSize(medium.value.hGlobal) < sizeof(DWORD)) return E_INVALIDARG;
    GlobalView view(medium.value.hGlobal);
    if (!view.data) return E_OUTOFMEMORY;
    DWORD effect{};
    std::memcpy(&effect, view.data, sizeof(effect));
    if (effect & DROPEFFECT_MOVE) request.operation = velocitycopy::FileOperation::Move;
    else if (!(effect & DROPEFFECT_COPY)) return E_INVALIDARG;
    return S_OK;
}

}
