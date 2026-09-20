# VelocityCopy UI specification

VelocityCopy is a compact Windows 11 copy/move utility. The window itself is the copier surface; it must stay compact, direct, and free of destination/layout chooser UI during drag/drop or shell-driven transfers.

## Window geometry

- Default collapsed size: **460 × 72 epx**
- Expanded queue target: **~300 epx**, bounded by measured queue content
- Preferred width range: **440–520 epx**
- Minimum practical width: **420 epx**
- Outer content gutter: **12–14 epx**
- Related-control spacing: **6–8 epx**
- Tight inline spacing: **3–4 epx**

## Collapsed composition

The collapsed window is one transfer surface. Do not place a second copier/card/capsule inside the HWND.

Order:
1. VelocityCopy brand mark at the far left
2. current item
3. throughput + percentage + ETA
4. essential transport controls
5. queue disclosure triangle at the far right
6. progress expressed by the surface fill itself

The compact surface must read as one deliberate Windows 11 control, not as a row of oversized independent buttons. Keep icon targets compact, preserve native hover/focus behavior, and give the filename/telemetry region priority over decorative spacing.

## Integrated progress surface

- One global progress indication only: the copier surface fill.
- No separate ProgressBar.
- Progress fills left-to-right behind the content using the Windows accent brush.
- Keep sufficient contrast for text, icons and focus visuals.
- No continuous animation when no progress is occurring.
- Progress state is logical (`progress_fraction_`); resizing never derives state back from rendered pixel width.

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
- No dormant destination/layout chooser should remain in `MainWindow.xaml` waiting for a future trigger. If a future product requirement truly needs destination selection, it must be designed as a separate explicit entry point rather than being coupled to drag/drop or normal Explorer transfer execution.
- Shell integration and drag/drop therefore converge at `QueueOrStartCopy`, not at a chooser flow.
- An accepted transfer must become visible immediately in the compact UI even while planning/enumeration is still running. The queue disclosure remains usable during this phase and shows a read-only preview of the accepted top-level sources until the authoritative `LiveCopyPlan` replaces it.

## Queue panel

The queue is collapsed by default and expands in the same HWND.

- Clicking the disclosure must make useful queue content visible whenever VelocityCopy has accepted work, including the initial planning phase before a `LiveCopyPlan` exists. Changing the chevron alone is not a successful expansion.
- During initial planning the list is read-only and represents accepted top-level sources; editing/reordering becomes available only after the live plan exists.
- The current collapsed HWND height is a layout constraint, not a measurement source. Measure `QueuePanel` independently with unconstrained vertical space, use `DesiredSize`, then resize the HWND.
- Keep the expanded height bounded (roughly 176–340 epx) so short queues do not create empty space and long queues scroll instead of growing without limit.
- Queue rows use a compact two-line hierarchy: filename first, source location secondary. Avoid card-per-row decoration.
- Virtualized list, drag/drop reordering, keyboard selection, move up/down/remove controls.
- Removing a queue entry never deletes the source file.
- No card background or second rounded shell around the queue.

## Modal choices and menus

The compact copier surface must never be resized merely to make a modal decision UI fit.

- File-conflict decisions (`Replace`, `Skip`, `Cancel`) use a separate native top-level dialog owned by the VelocityCopy HWND. They must not be a XAML `ContentDialog` embedded inside the 72 epx copier surface.
- Modal choice windows may be centered/owned by VelocityCopy, but they remain separate windows so their content cannot be clipped by the copier's current size.
- Lightweight command flyouts such as the options menu may use normal popup/flyout presentation because they are not laid out inside the copier surface.

## Window movement

The compact copier must remain movable with normal pointer dragging. Keep a practical custom title-bar drag region across the top of the copier surface while leaving interactive controls usable.

## Fluent/system integration

- Use Mica as the base window material when available.
- Use native Windows 11 outer rounding only.
- Use Segoe Fluent Icons for compact actions.
- Use system theme/accent resources; do not hard-code decorative colors.
- Preserve accessible names, tooltips and focus behavior.
- Visual hierarchy comes from spacing, typography, opacity and native interaction states, not extra borders/cards.

## Performance

- Defer expensive queue realization where possible, but never make accepted work invisible during planning.
- Avoid per-file progress controls and per-file animation.
- Do not update UI on every I/O completion.
- Keep core transfer state independent of concrete WinUI controls.
- Reuse queue visuals where possible and preserve selection/focus by stable file IDs during live refresh.
