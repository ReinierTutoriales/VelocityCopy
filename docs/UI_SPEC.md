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


## Phase 3 expanded-view contract

Phase 3 supersedes the Phase 2 presentation rule that Queue and Details are mutually exclusive. It does not change transfer planning, execution, conflict, recovery, or queue semantics.

- The normal surface remains 380 epx wide and keeps the Phase 1/2 transfer presentation.
- A single **Details / Hide details** disclosure owns expanded presentation state. Expanded presentation shows Queue, Performance, and Information together in the same HWND. Queue no longer has an independent disclosure state.
- `ExpandedPreferredWidth` is 880 epx. Effective expanded width is the lesser of 880 epx and the current monitor work-area width in effective pixels minus the window safety margin.
- `ExpandedThreeColumnThreshold` is 720 epx. At or above that effective width the expanded region is `Queue | Performance | Information`. Below it, the narrow composition is a single vertical stack in this order: Queue, Performance, Information. This avoids a hidden third breakpoint and remains deterministic down to the available work-area width. C++ selects the layout mode before measurement and resizing, then assigns the expanded region's `Grid.Row`, `Grid.Column`, `Grid.RowSpan`, `Grid.ColumnSpan`, and column widths directly. Phase 3 does not use `VisualStateManager`, `AdaptiveTrigger`, or an otherwise unnecessary `UserControl` for this deterministic composition.
- User pointer resizing remains disabled. Programmatic expansion/collapse uses a width-and-height resize contract and performs one final HWND placement after the layout mode and target dimensions are known.
- Expanded height is content-driven from the normal surface plus the expanded region. Performance and Information contribute their measured content height. Queue consumes the available expanded-row height through its ScrollViewer/virtualized list and does not dictate unbounded window height.
- Expanded height has a tokenized upper bound and is additionally clamped to the monitor work-area height.
- In Narrow composition, Queue keeps its tokenized minimum height and Details has its own tokenized minimum useful viewport height. When the monitor work-area budget cannot fit both minima plus expanded-region padding (including cases where an open InfoBar reduces the budget), neither section is collapsed to a token-sized sliver. The expanded region keeps both minima and a single outer vertical ScrollViewer receives an explicit maximum height equal to the calculated expanded-height budget, so its parent Auto row cannot remeasure it as unbounded content. This constrained-height fallback is Narrow-only; ThreeColumn keeps the outer scroll disabled and places Queue beside a two-column Performance | Information details viewport.
- Programmatic resizing is confined to the current monitor `rcWork`. Expansion preserves the left/top position when possible and shifts left and/or up only as far as necessary to keep the complete HWND inside the work area. Collapse keeps the resulting position; it does not restore pre-expansion coordinates.
- `SetExpanded(bool)` is the sole writer of expanded presentation visibility/state. Terminal, conflict, persistence, and queue paths request presentation changes through that state instead of independently collapsing Queue or Details controls.
- Expansion is available for a single transfer and remains available when Queue has no editable session. The single Details / Hide details disclosure is presentation-only and stays enabled; Queue's internal edit/reorder/remove commands carry session/conflict enablement. Queue presents the accepted/current session or an empty state as appropriate; expanded geometry must not depend on there being multiple jobs.
- Phase 3 initially displays only data with an authoritative existing source: source, destination, transferred/total bytes, aggregate percentage, completed/total files, current speed, and ETA.
- Current-file byte size and active-transfer count remain unavailable and must not be inferred.
- Destination filesystem type may be added later from a real `GetVolumeInformationW` query, in a separate change.
- Average speed is deferred until its active-time semantics are explicitly specified; Phase 3 must not synthesize it from wall-clock duration.
- The Phase 2 performance sampling contract remains authoritative. Phase 3 renders that same history as a declarative line/area graph without changing sample cadence, source, pause/cancel/terminal gating, or feeding graph data back into execution. The X axis has 60 fixed sample slots anchored at the right edge, so the newest sample is always at the right and the active-time history grows leftward until the 60-sample window is full. The Y axis starts at zero, uses a minimum 1 MiB/s ceiling, chooses a binary display unit compatible with transfer telemetry, rounds the visible peak upward to a 1/2/5 x 10^n ceiling within that unit, promotes a rounded MiB ceiling at or above 1024 MiB to the GiB axis, and shows zero, half, and maximum labels in the single unit selected for that axis with trailing decimal zeroes removed. The Y-axis label column keeps a tokenized 88 epx fallback minimum and, after load and on TextScaleFactorChanged, measures the real axis TextBlock with the widest expected label (`1000 MiB/s`) plus the horizontal margin of its label container to raise that minimum for the effective system text scale. Scale-label changes therefore do not horizontally shift the plot. Before two samples are available, the axis remains at its minimum 1 / 0.5 / 0 MiB/s scale while the line and area stay empty. The plot reserves half the line stroke thickness at both the top and zero baseline so peak and baseline strokes are not clipped. The area closes against that zero baseline. With fewer than two samples the line and area are empty. The graph container is Raw and each graph shape and changing Y-axis TextBlock is also explicitly AccessibilityView Raw; PerformanceCurrentSpeedText remains the accessible numeric speed.
- Phase 3 visual refinement targets the supplied Windows 11 mockup in **Light and Dark themes only** for this gate. The implementation keeps system/theme resources rather than hard-coded Light/Dark colors. The aggregate ProgressBar is a visible rounded track inside the normal content flow, between transfer totals and paths, rather than a hairline attached to the expanded-region divider. Queue, Performance, and Information receive subtle Fluent section surfaces using theme-aware card fill/stroke and control corner radius; these surfaces establish hierarchy without creating a second outer copier shell. Spacing/padding must keep the three sections visually distinct at the three-column width and remain valid in Narrow mode. In the bottom command row, Pause/Resume and Cancel remain grouped with the operational controls on the left side of the command area, while the single Details / Hide details disclosure is isolated at the far right; Options must not sit between Cancel and that disclosure.
- Per-job queue progress is a separate presentation change. The active job may use authoritative aggregate progress; waiting jobs are labelled as waiting. It must not invent per-file or per-job progress unavailable from the current execution model.

## Mockup coverage and phase ownership

The Windows 11 mockup is a product-direction reference, not a license to synthesize unavailable data or copy illustrative values literally. Each visible concept has an explicit owner and authoritative data requirement:

| Mockup concept | Phase / status | Authoritative implementation contract |
| --- | --- | --- |
| Minimal, normal and integrated expanded views | **Phase 3 — current** | Presentation of existing transfer state in the same HWND. Expanded view owns Queue + Performance + Information. |
| Aggregate progress track | **Phase 3 — current** | Native WinUI `ProgressBar` driven only by `UiSnapshot.fraction`. Visual thickness/spacing is tuned against real Light/Dark captures, not pixel-measured from the mockup image. |
| Queue job progress / “waiting 0%” | **Separate post-Phase-3 presentation block** | Only the authoritative active job may reuse aggregate progress. Waiting jobs may be labelled waiting; no fabricated per-file/per-job percentage. |
| Performance average speed | **Deferred** | Requires an explicit active-time accumulation contract. Never derive it from wall-clock duration. |
| Performance Y-axis labels | **Phase 3 — current contract wins** | Use the implemented zero / half / maximum 1-2-5 scale. Mockup labels are illustrative and must not replace the numeric scale contract. |
| Destination filesystem type | **Separate data-backed enhancement** | May be shown only from a real volume query such as `GetVolumeInformationW`. |
| Active-transfer count | **Deferred** | Not displayed until the application/core exposes an authoritative count. |
| Completed / conflict / recoverable-error presentation | **Phase 4 — terminal and decision surfaces** | Phase 4 owns the terminal/decision visual states described below; it must preserve existing execution semantics unless its contract explicitly supersedes presentation only. |
| Conflict “Keep both” | **Not currently supported** | Existing `DecisionSurface` exposes Replace, Skip, Cancel and optional Apply-to-all. Phase 4 must not add Keep-both without a real conflict policy in the core. |
| Conflict “Apply to all” | **Existing capability; Phase 4 may restyle it** | Maps to existing ReplaceAll / SkipAll policies. |
| Error “additional space required” | **Data-gated** | Requires a real free-space/required-space calculation for the affected destination. Never synthesize the number or drive letter. |
| Error “Retry” | **Capability-gated** | Existing recovery can expose `RecoveryAction::RetryTransfer` for parked incidents. Phase 4 may show Retry only when the actual incident/session is retryable; there is no generic retry promise for every terminal destination error. |
| Completion toast | **Deferred / excluded from Phase 4** | No completion notification is added by the terminal-surface phase. |
| Taskbar progress / quick status | **Later native taskbar phase** | Owned separately by the planned `ITaskbarList3` integration; not part of Phase 3 or Phase 4. |

## Phase 4 terminal and decision surfaces

Phase 4 begins only after Phase 3 is physically approved and merged. It supersedes the earlier presentation-only assumption that terminal/conflict UX must remain visually separate, while preserving the engine's existing decision policies and recovery semantics.

- **Completed:** a clean terminal transfer may remain visible in the main VelocityCopy window as a completion surface instead of immediately destroying that session window. The surface may expose only completion data already known authoritatively (operation result, completed file count, transferred/total bytes, elapsed time if a real session timer is available). “Open destination” requires the existing authoritative destination path. No toast is implied.
- **Conflict:** the main-window presentation may replace the owned top-level conflict dialog, but the available choices remain the policies that exist today: Replace, Skip, Cancel, plus Apply to all mapping to ReplaceAll / SkipAll. “Keep both” is outside Phase 4 until a real core policy exists.
- **Recoverable error:** Retry is shown only for a session/incident whose recovery metadata authorizes `RetryTransfer`; otherwise the surface exposes only actions that are genuinely available. Phase 4 must not relabel an unrecoverable terminal failure as retryable.
- **Destination-space messaging:** exact required/free-space values are optional and appear only after a real storage-space query/calculation is implemented. Generic failure text remains preferable to an invented byte deficit.
- Phase 4 changes presentation ownership, not the copy planner/executor's conflict or recovery policy. Architecture tests must continue to prevent UI-only invention of decisions.
- Phase 4 visual validation uses the same **Light/Dark** gate established for Phase 3.
