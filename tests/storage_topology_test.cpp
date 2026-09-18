#include "velocitycopy/storage_topology.hpp"

int main() {
    using namespace velocitycopy;
    StorageProfile a{}, b{};
    if (physical_storage_relationship(a, b) != PhysicalStorageRelationship::Unknown) return 1;
    a.physical_disk_extents_available = b.physical_disk_extents_available = true;
    a.physical_disk_numbers = {1, 3}; b.physical_disk_numbers = {2, 3};
    if (physical_storage_relationship(a, b) != PhysicalStorageRelationship::SharedDisk) return 2;
    b.physical_disk_numbers = {2, 4};
    if (physical_storage_relationship(a, b) != PhysicalStorageRelationship::DisjointDisks) return 3;
    return 0;
}
