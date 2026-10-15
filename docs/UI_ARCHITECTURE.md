# UI architecture guardrails

VelocityCopy keeps its visual layer replaceable and lightweight.

- Core copy logic never depends on WinUI/XAML.
- UI receives throttled immutable snapshots; copy callbacks never update controls directly.
- Window metrics, spacing, typography and motion are resource-driven.
- Explorer shell integration stays native and does not load WinUI or Windows App SDK.
- App strings use Windows localization resources; shell strings use native resource tables.
- The collapsed surface stays intentionally small: current item, one global progress bar, throughput/ETA, and essential controls.
- The expanded queue is virtualized and editable; no control instance per queued file.
- New visual features must not create polling loops or background timers when idle.

## Audited UI ownership and resource bounds

- Each execution session owns one latest-snapshot mailbox. At most one progress callback is pending in DispatcherQueue; it consumes the newest snapshot and verifies that the originating ExecutionControl is still current. Terminal completions remain separately dispatched. Paused/control/terminal states reject queued progress.
- UISettings events capture the dispatcher on the UI thread and marshal before resolving the window or touching its text-scale cache/layout. Focus restoration queries the current XamlRoot instead of the deprecated global FocusManager overload.
- QueueList stores immutable QueueItem data, never prebuilt row UIElements. Typed x:Bind data templates preserve the existing normal/narrow layout and let the bounded native ListView virtualize/recycle visuals. The current 256-item preview bound, fixed row-height budgets, stable-ID selection/reorder and native container styles remain authoritative.
- RefreshQueue does not enumerate or create queue items while Details is collapsed. Existing lightweight items/selection can remain retained between disclosure toggles; the next expansion refreshes from the authoritative plan.
- Source/destination tooltips mutate only when their displayed path changes. Inactive live telemetry is cleared in both normal and expanded views.
- Async external drops capture gate/destination/operation before reading StorageItems and revalidate after suspension. A retired/cancelled session cannot redirect a pending drop into its replacement.

## x64 queue stabilization — 2026-10-14

- Active rows are a separate, at-most-eight projection of `active_queue_view`, refreshed on every accepted throttled snapshot, including intervals in which no file completes. Pending rows remain data-only and bounded to 256. Pause immediately updates the active progress visual state.
- Native reorder captures the live-plan identity. A planning preview cannot start a reorder, and completion cannot edit a replacement session even if its file IDs match. Session reset defers pending visual mutations through RefreshQueue while native drag owns the Items collection.
- Queue header and pagination occupy separate rows; title and count use constrained Grid columns and ellipsis. Edit buttons remain visible and use enabled state. New pagination glyphs inherit FontIcon's native font fallback.
- Expanded width retains the measured compact command-row minimum. Client work-area budgets subtract the measured native frame before layout, so the final outer-HWND clamp does not silently clip measured content. Only bounded Queue chrome (header and active region) is measured at infinite height; the pending ListView never is. The active viewport has a 96 epx maximum and the pending viewport reserves 80 epx. The outer viewport owns overflow in either composition when measured sections exceed the monitor/token budget; this supersedes the earlier Narrow-only overflow rule.
- TaskbarButtonCreated replays the existing taskbar state/fraction when Explorer creates or recreates a button, including interrupted sessions with no new engine callbacks. The existing per-window bounded COM retry remains the fallback.
