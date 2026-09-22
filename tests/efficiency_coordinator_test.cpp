#include "velocitycopy/efficiency_coordinator.hpp"

int main() {
    velocitycopy::EfficiencyCoordinator c;
    if (!c.desired()) return 1;
    if (!c.update(1, true)) return 2;
    if (c.update(2, false)) return 3;
    if (!c.update(2, true)) return 4;
    if (c.update(2, false)) return 5;
    if (!c.remove(2)) return 6;
    if (c.set_shutting_down(true)) return 7;
    if (!c.set_shutting_down(false)) return 8;
    if (!c.remove(1)) return 9;
    if (!c.desired()) return 10;
    return 0;
}
