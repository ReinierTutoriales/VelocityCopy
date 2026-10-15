#include "../src/ui/VelocityCopy.UI/ExpandedLayout.h"

int main() {
    using velocitycopy::ui::layout::expanded_height;
    using velocitycopy::ui::layout::client_work_extent;
    if (client_work_extent(800, 16, 16) != 752 || client_work_extent(600, 16, 40) != 528 ||
        client_work_extent(20, 16, 40) != 1) return 6;
    const auto idle = expanded_height(176, 70, 80, 128, 24, 340, true);
    if (idle.queue_height != 176 || idle.viewport_height != 200 || idle.scrolls) return 1;
    const auto busy = expanded_height(176, 210, 80, 128, 24, 340, true);
    if (busy.queue_height != 290 || busy.viewport_height != 314 || busy.scrolls) return 2;
    const auto narrow = expanded_height(176, 210, 80, 128, 24, 340, false);
    if (narrow.queue_height != 290 || narrow.viewport_height != 340 || !narrow.scrolls) return 3;
    const auto scaled = expanded_height(176, 350, 80, 200, 24, 240, true);
    if (scaled.queue_height != 430 || scaled.viewport_height != 240 || !scaled.scrolls) return 4;
    const auto short_monitor = expanded_height(176, 70, 80, 128, 24, 40, false);
    if (short_monitor.queue_height != 176 || short_monitor.viewport_height != 40 || !short_monitor.scrolls) return 5;
    return 0;
}
