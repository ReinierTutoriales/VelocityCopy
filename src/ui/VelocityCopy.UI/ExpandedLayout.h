#pragma once

#include <algorithm>

namespace velocitycopy::ui::layout {

struct ExpandedHeight {
    double queue_height;
    double viewport_height;
    bool scrolls;
};

inline double client_work_extent(double work_extent, double margin, double native_frame) {
    return (std::max)(1.0, work_extent - margin * 2.0 - (std::max)(0.0, native_frame));
}

// Keep useful section extents; the outer viewport owns constrained overflow.
inline ExpandedHeight expanded_height(
    double queue_minimum, double queue_chrome, double pending_minimum,
    double details_height, double padding, double cap, bool side_by_side) {
    const double queue = (std::max)(queue_minimum, queue_chrome + pending_minimum);
    const double content = side_by_side ? (std::max)(queue, details_height)
                                       : queue + details_height;
    const double required = content + padding;
    return {side_by_side ? content : queue, (std::min)(required, cap), required > cap};
}

} // namespace velocitycopy::ui::layout
