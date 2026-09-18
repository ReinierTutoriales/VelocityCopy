#include "velocitycopy/destination_browser.hpp"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <system_error>

namespace velocitycopy {
namespace {

bool less_name(const DestinationFolderEntry& left, const DestinationFolderEntry& right) {
    const auto left_name = left.name.wstring();
    const auto right_name = right.name.wstring();
    return std::lexicographical_compare(
        left_name.begin(), left_name.end(),
        right_name.begin(), right_name.end(),
        [](wchar_t a, wchar_t b) {
            return std::towlower(a) < std::towlower(b);
        });
}

bool valid_new_folder_name(const std::filesystem::path& name) noexcept {
    try {
        if (name.empty() || name.has_parent_path() || name.is_absolute()) {
            return false;
        }
        const auto text = name.native();
        if (text == L"." || text == L"..") {
            return false;
        }
        constexpr wchar_t invalid[] = L"<>:\"/\\|?*";
        return text.find_first_of(invalid) == std::wstring::npos;
    } catch (...) {
        return false;
    }
}

} // namespace

DestinationFolderListing DestinationBrowser::list_children(
    const std::filesystem::path& folder) const noexcept {
    try {
        DestinationFolderListing result;
        std::error_code ec;
        std::filesystem::directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::directory_iterator end;
        if (ec) {
            return result;
        }
        result.available = true;

        for (; it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            const auto status = it->symlink_status(ec);
            if (ec) {
                ec.clear();
                continue;
            }
            if (std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status)) {
                result.children.push_back({it->path(), it->path().filename()});
            }
        }

        std::sort(result.children.begin(), result.children.end(), less_name);
        return result;
    } catch (...) {
        return {};
    }
}

DestinationCapacity DestinationBrowser::capacity(
    const std::filesystem::path& folder) const noexcept {
    ULARGE_INTEGER available{};
    ULARGE_INTEGER total{};
    ULARGE_INTEGER free{};
    if (!GetDiskFreeSpaceExW(folder.c_str(), &available, &total, &free)) {
        return {};
    }
    return {
        true,
        static_cast<std::uint64_t>(available.QuadPart),
        static_cast<std::uint64_t>(total.QuadPart),
    };
}

bool DestinationBrowser::create_folder(
    const std::filesystem::path& parent,
    const std::filesystem::path& name,
    std::filesystem::path& created) const noexcept {
    try {
        created.clear();
        if (!valid_new_folder_name(name)) {
            return false;
        }
        const auto candidate = parent / name;
        std::error_code ec;
        if (!std::filesystem::create_directory(candidate, ec) || ec) {
            return false;
        }
        created = candidate;
        return true;
    } catch (...) {
        created.clear();
        return false;
    }
}

} // namespace velocitycopy
