# VelocityCopy UI specification

VelocityCopy is a compact Windows 11 copy/move utility. The window itself is the copier surface; it must stay compact, direct, and free of destination/layout chooser UI during drag/drop or shell-driven transfers.

## Window geometry

> Superseded phase-0 baseline: the fixed **380 × 72 epx** collapsed surface described the pre-phase-1 compact UI. Phase 1 replaces that fixed-height contract with content-driven sizing; the old values remain in code/tests until the geometry/tokens commit that follows this documentation commit.

- The normal transfer view uses a canonical width token in effective pixels (epx). The geometry commit may retain 380 epx as the minimum/canonical width if the normal layout fits its accessibility and truncation requirements; width is a rule, not a width/height pair.
- Normal-view height is determined by measured content through `ResizeWindowToContent()`; it is not a second fixed design constant.
- Superseded phase-0 wording: "Expanded queue target: ~300 epx, bounded by measured queue content." Phase 1 does **not** redesign the expanded queue view; it preserves the current approximately-300-epx, content-bounded behavior until phase 2. Its existing minimum/maximum tokens remain authoritative during phase 1 except for any mechanical adjustment required to keep the same visible queue capacity after the normal surface becomes content-driven.
- XAML measures content in effective pixels (epx). The native resize helper ultimately supplies **outer HWND dimensions** to `SetWindowPos`; those dimensions include the non-client frame/invisible resize border. The current code converts requested epx directly with `GetDpiForWindow(hwnd)` + `MulDiv(epx, dpi, 96)` and does **not** compensate the client/non-client delta with `GetClientRect` or `AdjustWindowRectExForDpi`.
- The geometry commit must make the content-driven contract explicit: measure the required XAML/client content, then derive an outer HWND size that preserves that measured client extent at the current DPI. It must not assume that measured client height can be passed unchanged as outer-window height.
- The pre-phase-1 `AppWindow.Changed` handler reapplied the title-bar inset only. Phase-1 geometry listens to `XamlRoot.Changed` and re-measures only when `RasterizationScale` actually changes; self-initiated resizes are guarded so their size-change notifications cannot recurse. System text scaling is monitored separately through `UISettings.TextScaleFactorChanged` and marshalled back to the UI dispatcher before re-measurement. Physical 100%/150%/200% DPI and text-scale validation remains a real-Windows gate, not a CI claim.
- Outer content gutter: **8 epx**
- Related-control spacing: **8 epx**
- Tight inline spacing: **4 epx**

`DesignTokens.xaml` is the canonical source for UI design values consumed by live XAML or by the typed `UiTokens.h` accessor. C++ fallbacks must preserve the same value and are guarded by architecture tests; do not mirror unrelated runtime constants there merely for documentation.

Repository inventory before the geometry commit found the legacy `380`/`72` assumptions in `DesignTokens.xaml`, six uses/fallbacks in `MainWindow.xaml.cpp`, plus fixed-height restore calls in `MainWindow.Execution.cpp`, `MainWindow.Queue.cpp`, `MainWindow.QueuePersistence.cpp` and `MainWindow.Conflict.cpp`, and architecture contracts in `compact_ui_architecture_test.cpp` / `ui_token_architecture_test.cpp`. The geometry commit must update those together rather than leaving split sources of truth.

## Collapsed composition

The collapsed window is one transfer surface. Do not place a second copier/card/capsule inside the HWND.

Composition:
1. top row: VelocityCopy brand mark and current item name
2. bottom row: telemetry on the left and actions on the right
3. primary actions: Pause/Resume, Cancel, Options and queue disclosure
4. one native WinUI `ProgressBar` expresses aggregate transfer progress
5. native Windows caption buttons remain visible at the top-right

Skip and Stop live in Options rather than consuming permanent width. Telemetry and actions occupy separate grid columns so long speed/ETA strings cannot overlap Pause/Cancel. The action cluster is right-aligned, not artificially centered across the same row as telemetry.

Telemetry formatting is compact and stable:
- use KiB/s below 1 MiB/s, MiB/s through the normal range, and GiB/s at or above 1 GiB/s;
- represent multi-hour ETA as `H h MM m` instead of hundreds of minutes;
- reserve minimum width for speed and percentage fields to reduce visual movement while values change;
- avoid rewriting XAML text properties when the displayed value has not changed.

## Native progress contract

> Superseded by PR #57: the pre-phase-1 surface-fill `Border` and the rule "No separate ProgressBar" are retired. PR #57 introduced a native WinUI `ProgressBar` so progress exposes UI Automation `RangeValue`, follows system/high-contrast resources, and uses native `ShowPaused` / `ShowError` states without pixel-width bookkeeping.

- Exactly one aggregate transfer `ProgressBar` is present in the normal transfer surface; do not add per-file progress controls.
- `UiSnapshot.fraction` remains the logical source of aggregate progress. The native control contract is `Minimum=0`, `Maximum=100`, and `Value = clamp(fraction, 0, 1) * 100`. `ApplySnapshot()` feeds `SetProgressFraction()`; rendered width is never transfer state.
- `ShowPaused` and `ShowError` are mutually exclusive. Error has precedence: whenever `ShowError=true`, `ShowPaused` must be false. A non-error paused/stopped state may set `ShowPaused=true`.
- The current snapshot contract has no explicit "totals unknown" discriminator. When `total_bytes != 0`, fraction is byte-based; when `total_bytes == 0` but `total_files != 0`, `ProgressPresenter` deliberately falls back to completed-files / total-files; when both totals are zero, the initialized fraction remains `0.0`. Phase 1 therefore does **not** introduce `IsIndeterminate` by guessing from zero totals.
- The progress basis is a job/session contract, not a presentation heuristic: execution supplies the plan's resolution totals before progress callbacks. A byte-total job remains byte-based; a genuine zero-byte job uses the file-count fallback. Phase 1 must not switch bases in the UI or synthesize a different denominator mid-session. If a future planning state has genuinely unknown totals, it requires an explicit presentation-state signal before indeterminate progress is introduced.
- A clean completed state sets aggregate progress to 100% (`ProgressBar.Value = 100`, equivalent logical fraction `1.0`) with both `ShowPaused=false` and `ShowError=false`.
- These visual states must not change execution semantics or button enablement.
- No continuous animation when no progress is occurring.
- Real progress below one percent must not be rounded back to a misleading `0%`. Show sub-percent progress with enough precision to make forward movement visible (`<0.1%`, then one decimal below 10%).

## Phase 1 normal-view data contract

Phase 1 may lay out only data already available to the presentation layer. It must not extend the copy engine, planner or snapshot contract merely to satisfy the visual concept.

| Normal-view datum | Existing presentation source | Phase 1 |
| --- | --- | --- |
| Current file name | `UiSnapshot.current_source.filename()` | Included |
| Aggregate progress | `UiSnapshot.fraction` | Included |
| Transferred / total bytes | `UiSnapshot.transferred_bytes` / `total_bytes` | Available; may be presented without core changes |
| Completed / total files | `UiSnapshot.completed_files` / `total_files` | Available; when shown, use explicit completed-count wording such as **"4,321 completed of 12,481"** (localized equivalent). Do not use "Copying 4,321 of 12,481" or other wording that implies a current-file ordinal |
| Current source path | `UiSnapshot.current_source` | Available; use `TextTrimming="CharacterEllipsis"` and expose the full path by tooltip when shown |
| Current destination path | `UiSnapshot.current_destination` (plus the window's authoritative `active_destination_`) | Available; same trimming/tooltip rule when shown |
| Speed | `UiSnapshot.bytes_per_second` | Included |
| ETA | `UiSnapshot.eta_seconds` | Included |
| Current-file byte size | Not exposed by `UiSnapshot` | **Outside phase 1**; requires a separate presentation/core-contract decision |

The normal view must not infer a current-file ordinal from `completed_files + 1`; concurrent execution and non-success outcomes make that semantic stronger than the snapshot guarantees.

## Phase 1 state contract

The layout may change, but the existing state transitions and command enablement remain authoritative. "Visible" below describes the transfer surface; existing top-level decision/error surfaces remain as implemented until their dedicated phase.

| State | Progress | Pause/Resume | Cancel | Options | Queue disclosure | Existing status/decision surface |
| --- | --- | --- | --- | --- | --- | --- |
| Copying | normal native progress | Pause enabled | enabled | visible | visible | current item + speed/ETA |
| Paused / stopped | `ShowPaused=true`, `ShowError=false` | Resume enabled | enabled | visible | visible when existing queue state permits | speed/ETA are `—` while paused/stopped |
| Cancelling | no new stale snapshot repaint | no new capability is introduced | existing cancel transition remains authoritative | unchanged unless current code disables it | unchanged until terminal cleanup | terminal Cancelled status follows existing completion path |
| Error | `ShowError=true`, `ShowPaused=false` when the error notice is shown | idle/disabled after terminal failure | idle/disabled after terminal failure | visible | disabled after terminal failure | existing Error `InfoBar`; no new Retry action in phase 1 |
| Conflict | normal progress visual; `ShowPaused=false`, `ShowError=false` | disabled for destination conflict; existing recovery-decision variant may repurpose the button exactly as today | enabled | visible | enabled only when remaining/pending work exists | existing owned conflict/recovery decision surface |
| Completed | logical fraction `1.0` / `Value=100`; `ShowPaused=false`, `ShowError=false` | disabled | disabled | visible | disabled | existing completed/completed-with-issues status; no new completion toast in phase 1 |

This table is a preservation contract, not permission to normalize states that currently differ. If implementation and this table disagree during the phase-1 audit, preserve the current behavior and correct the table before changing behavior.

## Phase 2 expanded Information + Performance

Phase 2 adds an expanded **Details** surface to the same HWND. It is presentation-only: it does not alter copy planning, execution, conflict semantics or queue semantics.

- Details has its own disclosure/action and must not reuse the Queue disclosure or `OnQueueClick`.
- Details and Queue are mutually exclusive expanded surfaces. Opening Details collapses Queue; opening Queue collapses Details. This keeps one bounded expansion below the normal transfer surface and preserves Queue as a separate feature.
- The normal Phase-1 transfer surface remains visible and authoritative while Details is expanded.
- Details contains two sections: **Information** and **Performance**. It contains no queue list or queue editing controls.
- Information may repeat or expand only data already available to presentation: full current source, full current destination, transferred/total bytes, completed/total files, aggregate percentage, current speed and ETA. Current-file byte size and a current-file ordinal remain unavailable and must not be inferred.
- Long paths in Details wrap or trim predictably and expose the complete path through tooltip/accessibility text; they must not force horizontal window growth.
- Performance visualizes `UiSnapshot.bytes_per_second`. The core exposes a smoothed instantaneous speed, not a time series, so Phase 2 owns a bounded speed-sample history entirely in `MainWindow`.
- Presentation sampling is rate-limited independently of snapshot delivery. Keep at most 60 samples at approximately 500 ms spacing (about 30 seconds). Sampling continues while Details is collapsed so opening the panel reveals recent history instead of an empty graph.
- The sample source is the already-smoothed `UiSnapshot.bytes_per_second`. Phase 2 deliberately graphs that presentation signal directly; it does not compute a second moving average. The numeric current-speed value therefore matches the newest accepted graph sample subject to the 500 ms graph sampling cadence.
- Paused/stopped execution **freezes** graph sampling: do not append zeroes and do not synthesize gaps. The trace represents active transfer samples, not wall-clock duration. Existing samples remain visible while paused. Consequently the X axis represents up to about 30 seconds of **active transfer sampling**, not the last 30 seconds of wall-clock time. UI copy must not label it "last 30 seconds"; any label must be neutral or explicitly describe active-transfer time.
- Sampling stops immediately when execution enters **Cancelling**, before terminal Cancelled cleanup, so teardown does not appear as a throughput drop. Completed, terminal Error and Cancelled states likewise accept no further graph samples. Starting a new job/session in the same window clears the history and resets its sampling clock before accepting samples for that job.
- The graph is descriptive only. It must not feed progress, ETA, execution policy or any core decision back into the transfer engine.
- Phase 2 deliberately uses per-window auto-scaling against the maximum of the visible ~30-second sample history, with a non-zero floor. A single large peak may temporarily flatten subsequent smaller values; that tradeoff is accepted for this phase instead of introducing a hidden fixed throughput scale. A zero-speed history renders as a baseline rather than NaN/invalid geometry.
- Accessibility is carried by the localized numeric Performance fields. The graph itself is decorative/non-interactive: its XAML container sets `AutomationProperties.AccessibilityView="Raw"`, removing it from the UI Automation Control and Content views used for normal Narrator traversal. It has no focus target, exposes no per-bar/sample automation elements, and redraws must not cause Narrator announcements. Architecture tests must require this concrete accessibility setting.
- Details height is content-driven through the existing `ResizeWindowToContent()` client-size contract and participates in DPI/text-scale remeasurement.
- No continuous animation is introduced. The graph changes only when a rate-limited presentation sample is accepted.
- Queue remains separately available with its existing planning/edit/reorder semantics. Phase 2 does not redesign Queue.

## Drag/drop contract

Whole-window drag/drop is **append-only**.

- Accept StorageItems only while a transfer session with an authoritative destination is active.
- Append dropped items to that current destination and operation.
- If no transfer is active, reject the drop.
- Never invent a destination.
- Never open a destination picker, layout chooser, flyout, menu, prompt, overlay, or secondary surface because files were dragged onto VelocityCopy.
- Drag/drop must not expose Copy/Move choice UI; the active transfer operation is authoritative.

## Shell-driven transfer contract

Explorer/Shell integration delivers a resolved `CopyJob` to the UI process. The UI process queues or starts that job directly.

- No `ShellFlowFlyout`, destination browser, preserve/direct layout choice, or Start button is part of the runtime transfer path.
- No dormant destination/layout chooser should remain in `MainWindow.xaml` waiting for a future trigger.
- Shell integration and drag/drop converge on the direct transfer-routing path (`DeliverConvertedJob` / `StartTransfer` / `AppendTransfer`), not at a chooser flow.
- An accepted transfer must become visible immediately in the compact UI even while planning/enumeration is still running.

## Queue panel

The queue is collapsed by default and expands in the same HWND.

- The disclosure is always available, including while VelocityCopy is idle.
- Clicking the disclosure must make useful queue content visible whenever VelocityCopy has accepted work.
- During initial planning the list is read-only and represents accepted top-level sources; editing/reordering becomes available only after the live plan exists.
- Measure `QueuePanel` independently with unconstrained vertical space, use `DesiredSize`, then resize the HWND.
- Keep the expanded height bounded (roughly 176–340 epx).
- Queue rows use a compact two-line hierarchy: filename first, source location secondary.
- Virtualized list, drag/drop reordering, keyboard selection, move up/down/remove controls.
- Removing a queue entry never deletes the source file.
- No card background or second rounded shell around the queue.

## Transfer routing preference lifetime

For v1.0, the router dialog's **Remember my choice** preference is process-local only. `App` owns the remembered choice in memory for the current VelocityCopy execution and discards it when the process exits. No router preference is persisted under `%LOCALAPPDATA%\VelocityCopy` in v1.0. Persistent routing preferences, including UI to forget/reset them, are deferred as a possible v1.1 enhancement.

## Modal choices and menus

The compact copier surface must never be resized merely to make a modal decision UI fit.

- File-conflict decisions (`Replace`, `Skip`, `Cancel`) use a separate native top-level dialog owned by the VelocityCopy HWND.
- Lightweight command flyouts such as the options menu may use normal popup/flyout presentation.
- About is a themed, movable WinUI window launched from Options, with native close chrome and a fixed non-resizable presentation; it is not a legacy TaskDialog or MessageBox during the normal path.
- The About version resolver reads the running executable `VERSIONINFO` first and falls back to the compile-time `Version.h` identity. The UI must never display `Unknown` for a build whose compile-time version is known.
- Skip and Stop are menu commands, not hidden XAML buttons. Their enabled state is refreshed when Options opens and after transfer-state mutations.

## Window movement and caption chrome

The compact copier must remain movable with normal pointer dragging. `TitleBarDragRegion` remains the explicit drag surface and is registered with `SetTitleBar`.

VelocityCopy keeps the native Windows caption cluster visible: Minimize, Maximize and Close remain in the top-right. Maximize may remain disabled because the copier owns its compact/expanded size, but the native three-button chrome remains visually consistent with Windows 11.

Only the **top caption-content row** reserves `AppWindowTitleBar.RightInset`. The bottom telemetry/action row must not inherit that inset. The inset is converted from physical pixels to effective pixels for the current HWND DPI and is reapplied on `AppWindow.Changed`. User pointer resizing remains disabled; VelocityCopy may resize its own HWND programmatically between collapsed and expanded queue states.

## Fluent/system integration

- Use Mica as the base window material when available.
- Use native Windows 11 outer rounding/border and native caption buttons.
- Use Segoe Fluent Icons for compact actions.
- Use system theme/accent resources; do not hard-code decorative colors.
- Preserve accessible names, tooltips and focus behavior.
- Visual hierarchy comes from spacing, typography, opacity and native interaction states, not extra borders/cards.

## Performance

- Defer expensive queue realization where possible, but never make accepted work invisible during planning.
- Directory planning/enumeration runs off the UI thread and must observe the transfer stop token while walking the source tree so Cancel cannot leave a long planner running after the user has cancelled.
- Waiting for append planning must be bounded; a blocked filesystem enumeration must not keep the foreground transfer/session thread waiting forever.
- Avoid per-file progress controls and per-file animation.
- Do not update UI on every I/O completion.
- Do not assign the same telemetry/name text repeatedly; compare against the currently displayed value before mutating XAML properties.
- Keep core transfer state independent of concrete WinUI controls.
- Reuse queue visuals where possible and preserve selection/focus by stable file IDs during live refresh.


### Launch diagnostics

Unhandled WinUI exceptions remain fatal. `App` registers an `UnhandledException` observer before `App::InitializeComponent()` only to record the HRESULT and message; it must never set `Handled=true`. `OutputDebugStringW` is always available for an attached debugger. File logging is opt-in: when `VELOCITYCOPY_DIAGNOSTIC_LOG` names a writable path, the same HRESULT/message is appended there. CI x64 launch smoke sets this variable and publishes the file with `if: always()`; absence of the file is a warning because it is diagnostic evidence that failure may have occurred before the `App` observer could run.
