#pragma once

#include "velocitycopy/storage_profiler.hpp"

namespace velocitycopy {

enum class PhysicalStorageRelationship { Unknown, SharedDisk, DisjointDisks };

[[nodiscard]] PhysicalStorageRelationship physical_storage_relationship(
    const StorageProfile& left,
    const StorageProfile& right) noexcept;

} // namespace velocitycopy
