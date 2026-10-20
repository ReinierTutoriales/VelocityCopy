#include "velocitycopy/destination_catalog.hpp"
#include "velocitycopy/path_key.hpp"

namespace velocitycopy {
DestinationValidation DestinationCatalog::validate(
    std::span<const std::filesystem::path> sources,
    const std::filesystem::path& destination) noexcept {
    try {
        PathKeyFactory keys;
        const auto destination_key = keys.make(destination);
        if (destination_key.empty()) {
            return DestinationValidation::Empty;
        }

        for (const auto& source : sources) {
            const auto source_key = keys.make(source);
            if (source_key.empty()) continue;
            if (path_key_is_prefix(source_key, destination_key)) {
                return path_key_equal(source_key, destination_key)
                    ? DestinationValidation::SameAsSource : DestinationValidation::InsideSource;
            }
        }

        return DestinationValidation::Valid;
    } catch (...) {
        return DestinationValidation::Empty;
    }
}

} // namespace velocitycopy
