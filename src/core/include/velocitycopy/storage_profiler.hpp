#pragma once

#include <cstdint>
#include <filesystem>

namespace velocitycopy {

enum class StorageKind {
    Unknown,
    Fixed,
    Removable,
    Network,
    Optical,
    RamDisk,
};

struct StorageProfile {
    StorageKind kind{StorageKind::Unknown};
    std::filesystem::path volume_root;
    std::uint32_t logical_sector_bytes{};
    std::uint32_t physical_sector_bytes{};
    std::uint32_t device_type{};
    std::uint32_t device_number{};
    bool remote{};
    bool sector_info_available{};
    bool device_number_available{};
    bool incurs_seek_penalty{};
    bool seek_penalty_available{};
};

class StorageProfiler final {
public:
    [[nodiscard]] StorageProfile inspect(const std::filesystem::path& path) const noexcept;
};

} // namespace velocitycopy
