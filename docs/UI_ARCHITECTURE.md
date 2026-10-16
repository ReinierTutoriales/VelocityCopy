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
- Queue row templates share one root Grid per row rather than nesting an icon/name Grid. Active ProgressBar names identify the file and remain stable while native RangeValue conveys changing progress. Do not rewrite accessibility names on every snapshot to encode a second copy of the percentage.
- RefreshQueue does not enumerate or create queue items while Details is collapsed. Existing lightweight items/selection can remain retained between disclosure toggles; the next expansion refreshes from the authoritative plan.
- Source/destination tooltips mutate only when their displayed path changes. Inactive live telemetry is cleared in both normal and expanded views.
- Async external drops capture gate/destination/operation before reading StorageItems and revalidate after suspension. A retired/cancelled session cannot redirect a pending drop into its replacement.

## Adaptive decision dialogs — 2026-10-15

- Measure the native horizontal action row after templates load. If it exceeds the available client width after content padding and monitor work-area limits, stack actions vertically and allow each label to wrap. Preserve action order, all four choices, explicit automation names, primary focus and Escape cancellation; do not add a global Enter handler.
- The Loaded subscription uses a scoped C++/WinRT event revoker and references to coroutine-owned controls. Revoke before closing; stack unwinding also revokes before those controls are destroyed. The root must not own a handler that strongly captures itself or its Window.
- These changes add no runtime dependencies and do not reduce the packaged Windows App SDK payload. Source architecture checks and CI builds do not establish visual correctness or a measured memory reduction. Long translations, maximum Windows Text Size, small work areas and repeated dialog open/close still require physical Windows acceptance outside the developer's user session.
- Progress snapshots synchronize expanded telemetry only while Details is open, reusing the compact view's already formatted values and paths. Opening Details synchronizes immediately, including paused/completed sessions without another snapshot. Terminal clearing and bounded performance-history sampling remain independent of disclosure state.
- Follow-up: defer the expanded subtree safely and consolidate repeated statistics. The current Window-root namescope requires a binding-based x:Load design; simply adding x:Load=false and relying on FindName is insufficient. This step does not claim those changes are implemented.
- The attempted Window-root x:Load binding was rejected by Package x64/ARM64 compilation: generated MainWindow.xaml.g.hpp calls FindName on the projected MainWindow, which has no such member (C2039). The experiment and its CI-only probe were withdrawn together; do not expose a fake/public FindName workaround or describe deferral as shipped. A separately scoped native UserControl is a candidate for a future change, with first-open/reopen runtime coverage before acceptance. See upstream [Window namescope limitation](https://github.com/microsoft/microsoft-ui-xaml/issues/9842) and [C++/WinRT x:Load generation issue](https://github.com/microsoft/microsoft-ui-xaml/issues/7579).

References: [C++/WinRT event revocation](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/handle-events), [x:Load restrictions](https://learn.microsoft.com/en-us/windows/apps/develop/platform/xaml/x-load-attribute).

## Remaining presentation and resource assessment

The source review supports retaining native WinUI and the existing compact/expanded layouts, not calling the UI finished. No physical visual acceptance or process-resource benchmark has been performed for these changes.

| Area | Concrete remaining work | Acceptance evidence |
| --- | --- | --- |
| Information hierarchy | Show each statistic once; keep progress/actions primary and full paths accessible. Consolidate expanded information without losing keyboard or queue operations. | Rendered normal/narrow layouts, English/Spanish, DPI/Text Size, keyboard/UIA checks. |
| Conflict decisions | Clearly distinguish the affected source/destination and the consequence of each existing choice. Preserve retry/recovery semantics. | Real collision/error scenarios; no global Enter shortcut for destructive choices. |
| Startup and memory | Separate the expanded subtree into a native namescope suitable for deferred creation. Preserve controls after first disclosure. | Same-machine cold/warm startup, private bytes/working set before disclosure, first-open latency and reopen state. |
| Transfer-time UI cost | Keep the current coalesced progress mailbox, bounded queue projection and history. Profile expensive text/layout/graph updates before adding more caching. | UI-thread CPU/frame times during the same large queue/workload; unchanged transfer correctness. |
| Distribution size | Audit the self-contained runtime inventory and dynamic activation/resource dependencies before trimming anything. | x64/ARM64 package, PRI, launch, upgrade/install/uninstall gates plus actual byte comparison. |

The preceding Package inventory measured 65,592,652 uncompressed bytes across 244 x64 files and 70,618,228 bytes across 244 ARM64 files. These are payload figures, not installer sizes or process memory. The x64 application EXE accounted for 1,963,008 bytes; most payload bytes belonged to runtime/components. Native UI edits alone do not establish a significant distribution-size reduction. Preserve the current acrylic preference; compare materials only with rendered and GPU/power evidence.

## x64 queue stabilization — 2026-10-14

- Active rows are a separate, at-most-eight projection of `active_queue_view`, refreshed on every accepted throttled snapshot, including intervals in which no file completes. Pending rows remain data-only and bounded to 256. Pause immediately updates the active progress visual state.
- Native reorder captures the live-plan identity. A planning preview cannot start a reorder, and completion cannot edit a replacement session even if its file IDs match. Session reset defers pending visual mutations through RefreshQueue while native drag owns the Items collection.
- Queue header and pagination occupy separate rows; title and count use constrained Grid columns and ellipsis. Edit buttons remain visible and use enabled state. New pagination glyphs inherit FontIcon's native font fallback.
- Expanded width retains the measured compact command-row minimum. Client work-area budgets subtract the measured native frame before layout, so the final outer-HWND clamp does not silently clip measured content. Only bounded Queue chrome (header and active region) is measured at infinite height; the pending ListView never is. The active viewport has a 96 epx maximum and the pending viewport reserves 80 epx. The outer viewport owns overflow in either composition when measured sections exceed the monitor/token budget; this supersedes the earlier Narrow-only overflow rule.
- TaskbarButtonCreated replays the existing taskbar state/fraction when Explorer creates or recreates a button, including interrupted sessions with no new engine callbacks. The existing per-window bounded COM retry remains the fallback.
