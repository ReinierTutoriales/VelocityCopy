# VelocityCopy UI specification

VelocityCopy is a compact Windows 11 copy/move utility. The window itself is the copier surface; it must stay compact, direct, and free of destination/layout chooser UI during drag/drop or shell-driven transfers.

## Window geometry

- Default collapsed size: **380 × 72 epx**
- Expanded queue target: **~300 epx**, bounded by measured queue content
- Compact width target: **380 epx**
- Outer content gutter: **8 epx**
- Related-control spacing: **8 epx**
- Tight inline spacing: **4 epx**

Only resources consumed by the live XAML belong in `DesignTokens.xaml`; do not mirror runtime constants there merely for documentation.

## Collapsed composition

The collapsed window is one transfer surface. Do not place a second copier/card/capsule inside the HWND.

Composition:
1. top row: VelocityCopy brand mark and current item name
2. bottom row: telemetry on the left and actions on the right
3. primary actions: Pause/Resume, Cancel, Options and queue disclosure
4. progress expressed by the surface fill itself
5. native Windows caption buttons remain visible at the top-right

Skip and Stop live in Options rather than consuming permanent width. Telemetry and actions occupy separate grid columns so long speed/ETA strings cannot overlap Pause/Cancel. The action cluster is right-aligned, not artificially centered across the same row as telemetry.

Telemetry formatting is compact and stable:
- use KiB/s below 1 MiB/s, MiB/s through the normal range, and GiB/s at or above 1 GiB/s;
- represent multi-hour ETA as `H h MM m` instead of hundreds of minutes;
- reserve minimum width for speed and percentage fields to reduce visual movement while values change;
- avoid rewriting XAML text properties when the displayed value has not changed.

## Integrated progress surface

- One global progress indication only: the copier surface fill.
- No separate ProgressBar.
- Progress fills left-to-right behind the content using the Windows accent brush.
- Keep sufficient contrast for text, icons and focus visuals.
- No continuous animation when no progress is occurring.
- Progress state is logical (`progress_fraction_`); resizing never derives state back from rendered pixel width.
- Real progress below one percent must not be rounded back to a misleading `0%`. Show sub-percent progress with enough precision to make forward movement visible (`<0.1%`, then one decimal below 10%).

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
- Shell integration and drag/drop converge at `QueueOrStartCopy`, not at a chooser flow.
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

## Modal choices and menus

The compact copier surface must never be resized merely to make a modal decision UI fit.

- File-conflict decisions (`Replace`, `Skip`, `Cancel`) use a separate native top-level dialog owned by the VelocityCopy HWND.
- Lightweight command flyouts such as the options menu may use normal popup/flyout presentation.
- About is a themed WinUI flyout launched from Options, not a legacy TaskDialog or MessageBox during the normal path.
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
