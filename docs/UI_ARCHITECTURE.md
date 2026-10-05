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

See [UI audit and Microsoft references](UI_AUDIT_20261005.md).
