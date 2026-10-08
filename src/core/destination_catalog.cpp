#include "velocitycopy/destination_catalog.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <string>

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

} // namespace

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
