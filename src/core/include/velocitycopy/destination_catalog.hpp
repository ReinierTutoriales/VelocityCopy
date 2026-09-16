#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace velocitycopy {

enum class DestinationKind : std::uint8_t {
    KnownFolder,
    Drive,
    Recent,
};

enum class DestinationValidation : std::uint8_t {
    Valid,
    Empty,
    SameAsSource,
    InsideSource,
};

struct DestinationEntry {
    DestinationKind kind{DestinationKind::Drive};
    std::wstring label;
    std::filesystem::path path;
    std::uint32_t drive_type{};
};

class DestinationCatalog final {
public:
    [[nodiscard]] std::vector<DestinationEntry> enumerate(
        std::span<const std::filesystem::path> recent = {}) const noexcept;

    [[nodiscard]] static DestinationValidation validate(
        std::span<const std::filesystem::path> sources,
        const std::filesystem::path& destination) noexcept;
};

} // namespace velocitycopy
