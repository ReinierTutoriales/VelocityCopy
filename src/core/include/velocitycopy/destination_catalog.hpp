#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace velocitycopy {

enum class DestinationValidation : std::uint8_t {
    Valid,
    Empty,
    SameAsSource,
    InsideSource,
};

class DestinationCatalog final {
public:
    [[nodiscard]] static DestinationValidation validate(
        std::span<const std::filesystem::path> sources,
        const std::filesystem::path& destination) noexcept;
};

} // namespace velocitycopy
