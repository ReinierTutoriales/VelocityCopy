# VelocityCopy UI specification

VelocityCopy is a compact Windows 11 copy/move utility. The window itself is the copier surface; it must stay compact, direct, and free of destination/layout chooser UI during drag/drop or shell-driven transfers.

## Window geometry

- Default collapsed size: **460 × 72 epx**
- Expanded queue target: **460 × 300 epx**
- Preferred width range: **440–520 epx**
- Minimum practical width: **420 epx**
- Outer content gutter: **12 epx**
- Related-control spacing: **8 epx**
- Tight inline spacing: **4 epx**

## Collapsed composition

The collapsed window is one transfer surface. Do not place a second copier/card/capsule inside the HWND.

Order:
1. VelocityCopy brand mark at the far left
2. current item
3. throughput + percentage + ETA
4. essential transport controls
5. queue disclosure triangle at the far right
6. progress expressed by the surface fill itself

## Integrated progress surface

- One global progress indication only: the copier surface fill.
- No separate ProgressBar.
- Progress fills left-to-right behind the content using the Windows accent brush.
- Keep sufficient contrast for text, icons and focus visuals.
- No continuous animation when no progress is occurring.

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

## Queue panel

The queue is collapsed by default and expands in the same HWND.

- target expanded height: ~300 epx
- measure realized content before resizing
- virtualized list
- drag/drop reordering inside the queue
- keyboard selection
- controls: move up, move down, remove
- removing a queue entry never deletes the source file
- no card background or second rounded shell around the queue

## Window movement

The compact copier must remain movable with normal pointer dragging. Keep a practical custom title-bar drag region across the top of the copier surface while leaving interactive controls usable.

## Fluent/system integration

- Use Mica as the base window material when available.
- Use native Windows 11 outer rounding only.
- Use Segoe Fluent Icons for compact actions.
- Use system theme/accent resources; do not hard-code decorative colors.
- Preserve accessible names, tooltips and focus behavior.

## Performance

- Defer queue visuals until expanded.
- Avoid per-file progress controls and per-file animation.
- Do not update UI on every I/O completion.
- Keep core transfer state independent of concrete WinUI controls.
