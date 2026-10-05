#include "architecture_support.hpp"

#include <iostream>
#include <regex>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
bool contains(const std::string& text, const std::string& value) { return text.find(value) != std::string::npos; }
int fail(const int code, const char* message) {
    std::cerr << "queue density architecture contract " << code << ": " << message << '\n';
    return code;
}
} // namespace

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const std::filesystem::path ui = root / "src/ui/VelocityCopy.UI";
    const auto xaml = read_source(ui / "MainWindow.xaml");
    const auto queue = read_source(ui / "MainWindow.Queue.cpp");
    const auto window = read_source(ui / "MainWindow.xaml.cpp");
    const auto header = read_source(ui / "MainWindow.xaml.h");
    const auto tokens = read_source(root / "src/ui/DesignTokens.xaml");
    if (xaml.empty() || queue.empty() || window.empty() || header.empty() || tokens.empty()) return fail(1, "required source missing");

    // 1. No compact container style may be global to the list: ThreeColumn keeps the native ListViewItem.
    if (contains(xaml, "ListView.ItemContainerStyle") || contains(xaml, "ItemContainerStyle=")) {
        return fail(2, "QueueList must not declare a global ItemContainerStyle");
    }
    // The compact style exists only as a resource derived from the native style (16,0,12,0 / MinHeight 40 in 2.4.0).
    const auto style_at = xaml.find("x:Key=\"QueueNarrowListViewItemStyle\"");
    if (style_at == std::string::npos) return fail(3, "Narrow container style resource missing");
    const auto style_end = xaml.find("</Style>", style_at);
    const auto style = xaml.substr(style_at, style_end - style_at);
    if (!contains(style, "BasedOn=\"{StaticResource DefaultListViewItemStyle}\"") ||
        !contains(style, "{StaticResource QueueNarrowItemContainerMinHeight}") ||
        !contains(style, "{StaticResource QueueNarrowItemContainerPadding}")) {
        return fail(4, "Narrow style must derive from DefaultListViewItemStyle and use the Narrow tokens");
    }
    for (const char* retired : {"QueueItemContainerMinHeight", "QueueItemContainerPadding"}) {
        if (contains(xaml, retired) || contains(tokens, retired)) return fail(5, "global container tokens returned");
    }
    if (!contains(tokens, "x:Key=\"QueueNarrowItemContainerMinHeight\"") ||
        !contains(tokens, "x:Key=\"QueueNarrowItemContainerPadding\"")) {
        return fail(6, "Narrow container tokens missing");
    }

    // 2. The style is applied by mode, and a style change rebuilds the visuals (existing containers keep the old style).
    const auto apply = body_of(queue, "bool MainWindow::ApplyQueueItemStyle(");
    if (apply.empty() || !contains(apply, "QueueNarrowListViewItemStyle") ||
        !contains(apply, "ClearValue(ItemsControl::ItemContainerStyleProperty())") ||
        !contains(apply, "queue_item_style_narrow_")) {
        return fail(7, "ApplyQueueItemStyle must set the Narrow style and clear it for ThreeColumn");
    }
    const auto refresh = body_of(queue, "void MainWindow::RefreshQueue(");
    if (!contains(refresh, "ApplyQueueItemStyle(expanded_layout_mode_ == ExpandedLayoutMode::Narrow)") ||
        !contains(refresh, "std::exchange(queue_refresh_force_rebuild_, false)") ||
        !contains(refresh, "unchanged && !rebuild_visuals") || !contains(refresh, "!rebuild_visuals && completed_prefix > 0")) {
        return fail(8, "RefreshQueue must rebuild visuals whenever the container style or the mode changes");
    }
    if (!contains(header, "bool ApplyQueueItemStyle(bool narrow);") || !contains(header, "queue_item_style_narrow_")) {
        return fail(9, "style state not declared");
    }

    // 3. Header: title trims instead of colliding with the counter; the edit commands stay in the header, always visible.
    const auto title_at = xaml.find("x:Name=\"QueueTitle\"");
    if (title_at == std::string::npos) return fail(10, "QueueTitle missing");
    const auto title = xaml.substr(title_at, xaml.find("/>", title_at) - title_at);
    if (!contains(title, "TextTrimming=\"CharacterEllipsis\"") || !contains(title, "MaxLines=\"1\"")) {
        return fail(11, "QueueTitle must trim with an ellipsis");
    }
    const auto header_at = xaml.find("x:Name=\"QueueHeader\"");
    const auto list_at = xaml.find("x:Name=\"QueueList\"");
    if (header_at == std::string::npos || list_at == std::string::npos || list_at < header_at) return fail(12, "queue layout anchors missing");
    const auto header_block = xaml.substr(header_at, list_at - header_at);
    for (const char* name : {"QueueMoveUpButton", "QueueMoveDownButton", "QueueRemoveButton"}) {
        if (!contains(header_block, std::string{"x:Name=\""} + name + "\"")) return fail(13, "edit commands left the Queue header");
    }
    if (contains(header_block, "Visibility=")) return fail(14, "header commands must stay visible (enabled state only)");

    // 4. Queue rows carry no per-item progress and no fixed colors (system ThemeResource only).
    for (const char* forbidden : {"ProgressBar", "SolidColorBrush", "Colors::", "Windows::UI::Color"}) {
        if (contains(queue, forbidden)) return fail(15, "Queue code must not add per-item progress or fixed colors");
    }
    // 5. The queue height budget stays untouched in this block.
    if (!contains(tokens, "<x:Double x:Key=\"QueueExpandedMinHeight\">176</x:Double>")) return fail(16, "QueueExpandedMinHeight changed");
    if (!contains(queue, "find_scroll_viewer(QueueList())") ||
        !contains(queue, "queue_scroll_viewer.VerticalOffset()") ||
        !contains(queue, "QueueList().UpdateLayout()") ||
        !contains(queue, "queue_scroll_viewer.ChangeView(") ||
        !contains(queue, "queue_scroll_viewer.ScrollableHeight()")) {
        return fail(17, "queue visual rebuilds must preserve the vertical scroll position");
    }

    // 6. Internal reorder owns QueueList.Items for the lifetime of the drag. Snapshot refreshes
    // and the RootGrid external-drop path must not mutate or override that gesture.
    if (!contains(xaml, "DragItemsStarting=\"OnQueueDragItemsStarting\"") ||
        !contains(xaml, "DragItemsCompleted=\"OnQueueDragItemsCompleted\"")) {
        return fail(18, "QueueList must bracket internal reorder with starting/completed handlers");
    }
    const auto drag_start = body_of(queue, "void MainWindow::OnQueueDragItemsStarting(");
    const auto drag_done = body_of(queue, "void MainWindow::OnQueueDragItemsCompleted(");
    if (!contains(drag_start, "queue_drag_active_ = true") ||
        !contains(drag_done, "queue_drag_active_ = false") ||
        !contains(drag_done, "std::exchange(queue_refresh_force_rebuild_, false)") ||
        !contains(drag_done, "RefreshQueue(force_visual_rebuild)")) {
        return fail(19, "drag handlers must guard the collection and flush one authoritative refresh");
    }
    if (!contains(refresh, "if (queue_drag_active_)") ||
        !contains(refresh, "queue_refresh_force_rebuild_ = queue_refresh_force_rebuild_ || force_visual_rebuild") ||
        !contains(refresh, "return;")) {
        return fail(20, "RefreshQueue must defer all collection/style mutation while reorder is active");
    }
    if (!contains(header, "bool queue_drag_active_{};") ||
        !contains(header, "bool queue_refresh_force_rebuild_{};")) {
        return fail(21, "queue drag refresh state missing");
    }
    for (const char* signature : {
             "void MainWindow::OnDragEnter(IInspectable const&, DragEventArgs const& args)",
             "void MainWindow::OnDragOver(IInspectable const&, DragEventArgs const& args)",
             "void MainWindow::OnDrop(IInspectable const&, DragEventArgs const& args)"}) {
        const auto handler = body_of(window, signature);
        if (!contains(handler, "if (queue_drag_active_) return;")) {
            return fail(22, "external RootGrid drop handlers must ignore QueueList internal reorder");
        }
    }

    // 7. Pointer wheel over Queue: ListView consumes the routed event. Register
    // with handledEventsToo, keep native inner scrolling until a boundary, then
    // move the outer Narrow viewport with ChangeView.
    const auto wheel = body_of(queue, "void MainWindow::OnQueuePointerWheelChanged(");
    if (wheel.size() < 500 ||
        !contains(wheel, "outer.ScrollableHeight() <= 0.5") ||
        !contains(wheel, "inner_can_scroll_up") ||
        !contains(wheel, "inner_can_scroll_down") ||
        !contains(wheel, "IsHorizontalMouseWheel()") ||
        !contains(wheel, "outer.ChangeView(") ||
        !contains(wheel, "args.Handled(true)") ||
        contains(wheel, "ScrollToVerticalOffset")) {
        return fail(23, "Queue wheel forwarding must preserve native list scrolling and hand off only at its limits");
    }
    if (!contains(window, "QueueList().AddHandler(") ||
        !contains(window, "UIElement::PointerWheelChangedEvent()") ||
        !contains(window, "&MainWindow::OnQueuePointerWheelChanged") ||
        !contains(window, "true);") ||
        contains(xaml, "PointerWheelChanged=")) {
        return fail(24, "Queue wheel handler must use AddHandler with handledEventsToo rather than XAML routing");
    }
    if (!contains(tokens, "<x:Double x:Key=\"QueueWheelForwardStep\">48</x:Double>")) {
        return fail(25, "documented default wheel handoff step token missing");
    }

    // Items are data, so the bounded native ListView can create/recycle templates.
    if (!contains(refresh, "winrt::make<QueueItem>") || contains(refresh, "Grid row") ||
        contains(refresh, "TextBlock name") ||
        !contains(xaml, "x:Key=\"QueueItemTemplate\" x:DataType=\"local:QueueItem\"") ||
        !contains(xaml, "x:Key=\"QueueNarrowItemTemplate\" x:DataType=\"local:QueueItem\"") ||
        !contains(xaml, "Text=\"{x:Bind Name}\"") || !contains(xaml, "Text=\"{x:Bind Location}\"") ||
        !contains(xaml, "ToolTipService.ToolTip=\"{x:Bind FullPath}\"") ||
        !contains(drag_done, "item.Id()") ||
        contains(queue, "FocusManager::GetFocusedElement()") ||
        !contains(queue, "FocusManager::GetFocusedElement(xaml_root)")) return fail(26, "queue must use data templates and stable data IDs");
    const auto collapsed = body_of(refresh, "if (!expanded_)");
    if (collapsed.empty() || !contains(collapsed, "return;") ||
        refresh.find("if (!expanded_)") > refresh.find("live_plan_->queue_view")) {
        return fail(27, "collapsed queue must not enumerate or create rows");
    }

    return 0;
}
