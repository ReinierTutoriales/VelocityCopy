#include "velocitycopy/destination_catalog.hpp"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <string>
#include <unordered_set>

namespace velocitycopy {
namespace {

std::wstring normalized_key(const std::filesystem::path& input) noexcept {
    try {
        if (input.empty()) {
            return {};
        }

        std::array<wchar_t, 32768> buffer{};
        const DWORD length = GetFullPathNameW(
            input.c_str(),
            static_cast<DWORD>(buffer.size()),
            buffer.data(),
            nullptr);
        if (length == 0 || length >= buffer.size()) {
            return {};
        }

        std::wstring result(buffer.data(), length);
        while (result.size() > 3 && (result.back() == L'\\' || result.back() == L'/')) {
            result.pop_back();
        }
        std::transform(result.begin(), result.end(), result.begin(), [](wchar_t value) {
            return static_cast<wchar_t>(std::towlower(value));
        });
        return result;
    } catch (...) {
        return {};
    }
}

bool is_same_or_child(const std::wstring& candidate, const std::wstring& parent, bool& same) noexcept {
    same = candidate == parent;
    if (same || parent.empty() || candidate.size() <= parent.size()) {
        return same;
    }
    if (candidate.compare(0, parent.size(), parent) != 0) {
        return false;
    }
    const wchar_t separator = candidate[parent.size()];
    return separator == L'\\' || separator == L'/';
}

void append_unique(
    std::vector<DestinationEntry>& entries,
    std::unordered_set<std::wstring>& seen,
    DestinationEntry entry) {
    const auto key = normalized_key(entry.path);
    if (key.empty() || !seen.insert(key).second) {
        return;
    }
    entries.push_back(std::move(entry));
}

void append_known_folder(
    std::vector<DestinationEntry>& entries,
    std::unordered_set<std::wstring>& seen,
    REFKNOWNFOLDERID id,
    const wchar_t* label) noexcept {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) || raw == nullptr) {
        return;
    }
    try {
        append_unique(entries, seen, {DestinationKind::KnownFolder, label, raw, 0});
    } catch (...) {
    }
    CoTaskMemFree(raw);
}

} // namespace

std::vector<DestinationEntry> DestinationCatalog::enumerate(
    std::span<const std::filesystem::path> recent) const noexcept {
    try {
        std::vector<DestinationEntry> entries;
        entries.reserve(8 + recent.size());
        std::unordered_set<std::wstring> seen;
        seen.reserve(8 + recent.size());

        append_known_folder(entries, seen, FOLDERID_Desktop, L"Desktop");
        append_known_folder(entries, seen, FOLDERID_Documents, L"Documents");
        append_known_folder(entries, seen, FOLDERID_Downloads, L"Downloads");

        const DWORD required = GetLogicalDriveStringsW(0, nullptr);
        if (required != 0) {
            std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 1);
            if (GetLogicalDriveStringsW(required, buffer.data()) != 0) {
                for (const wchar_t* drive = buffer.data(); *drive != L'\0'; drive += std::wcslen(drive) + 1) {
                    const UINT type = GetDriveTypeW(drive);
                    std::wstring label = drive;
                    if (!label.empty() && label.back() == L'\\') {
                        label.pop_back();
                    }
                    append_unique(entries, seen, {
                        DestinationKind::Drive,
                        std::move(label),
                        drive,
                        static_cast<std::uint32_t>(type),
                    });
                }
            }
        }

        for (const auto& path : recent) {
            if (!path.empty()) {
                append_unique(entries, seen, {DestinationKind::Recent, path.filename().wstring(), path, 0});
            }
        }

        return entries;
    } catch (...) {
        return {};
    }
}

DestinationValidation DestinationCatalog::validate(
    std::span<const std::filesystem::path> sources,
    const std::filesystem::path& destination) noexcept {
    const auto destination_key = normalized_key(destination);
    if (destination_key.empty()) {
        return DestinationValidation::Empty;
    }

    for (const auto& source : sources) {
        const auto source_key = normalized_key(source);
        if (source_key.empty()) {
            continue;
        }

        bool same = false;
        if (is_same_or_child(destination_key, source_key, same)) {
            return same ? DestinationValidation::SameAsSource : DestinationValidation::InsideSource;
        }
    }

    return DestinationValidation::Valid;
}

} // namespace velocitycopy
