#include "velocitycopy/storage_topology.hpp"

#include <algorithm>

namespace velocitycopy {

PhysicalStorageRelationship physical_storage_relationship(
    const StorageProfile& left,
    const StorageProfile& right) noexcept {
    if (!left.physical_disk_extents_available || !right.physical_disk_extents_available) {
        return PhysicalStorageRelationship::Unknown;
    }
    for (const auto disk : left.physical_disk_numbers) {
        if (std::find(right.physical_disk_numbers.begin(), right.physical_disk_numbers.end(), disk) != right.physical_disk_numbers.end()) {
            return PhysicalStorageRelationship::SharedDisk;
        }
    }
    return PhysicalStorageRelationship::DisjointDisks;
}

} // namespace velocitycopy
