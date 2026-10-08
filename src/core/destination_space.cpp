#include "velocitycopy/destination_space.hpp"

#include <windows.h>

#include <array>
#include <cwctype>
#include <string>

namespace velocitycopy {
namespace {

// Nearest existing directory at or above `path` (the destination root may
// not exist yet: the transfer creates it).
std::filesystem::path existing_ancestor(std::filesystem::path path) {
    for (int depth = 0; depth < 512 && !path.empty(); ++depth) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) return path;
        const auto parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

std::wstring volume_of(const std::filesystem::path& path) {
    std::array<wchar_t, 32768> volume{};
    if (!GetVolumePathNameW(path.c_str(), volume.data(), static_cast<DWORD>(volume.size()))) return {};
    std::wstring result(volume.data());
    for (auto& ch : result) ch = static_cast<wchar_t>(std::towlower(ch));
    return result;
}

} // namespace

std::optional<SpaceShortage> find_space_shortage(
    const std::vector<std::filesystem::path>& source_roots,
    const std::filesystem::path& destination_root,
    const FileOperation operation,
    const std::uint64_t required_bytes) noexcept {
    try {
        if (required_bytes == 0 || destination_root.empty()) return std::nullopt;
        const auto anchor = existing_ancestor(destination_root);
        if (anchor.empty()) return std::nullopt;

        if (operation == FileOperation::Move && !source_roots.empty()) {
            const auto destination_volume = volume_of(anchor);
            bool all_same_volume = !destination_volume.empty();
            for (const auto& source : source_roots) {
                if (!all_same_volume) break;
                all_same_volume = volume_of(source) == destination_volume;
            }
            if (all_same_volume) return std::nullopt;
        }

        ULARGE_INTEGER available{};
        if (!GetDiskFreeSpaceExW(anchor.c_str(), &available, nullptr, nullptr)) return std::nullopt;
        if (required_bytes <= available.QuadPart) return std::nullopt;
        return SpaceShortage{required_bytes, available.QuadPart};
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace velocitycopy
