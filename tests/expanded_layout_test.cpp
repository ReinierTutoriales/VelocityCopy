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
    // Effective-pixel policy only: this does not emulate the WinUI text engine.
    for (const double dpi : {96., 120., 144., 168., 192., 216., 240., 288.}) {
        for (const double text_scale : {1., 1.25, 1.5, 1.75, 2., 2.25}) {
            for (const double height_px : {720., 1080., 1440., 2160.}) {
                const double cap = client_work_extent(height_px * 96 / dpi, 16, 40);
                for (const bool side_by_side : {false, true}) {
                    const auto result = expanded_height(176, 210 * text_scale, 80,
                        128 * text_scale, 24, cap, side_by_side);
                    const double content = side_by_side
                        ? std::max(result.queue_height, 128 * text_scale)
                        : result.queue_height + 128 * text_scale;
                    if (result.viewport_height > cap || result.viewport_height <= 0 ||
                        result.queue_height < 176 ||
                        result.scrolls != (content + 24 > cap)) return 7;
                }
            }
        }
    }
    return 0;
}
