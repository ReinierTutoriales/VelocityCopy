# VelocityCopy UI specification

VelocityCopy is a compact Windows 11 copy/move utility. The window itself is the copier surface; it must stay compact, direct, and free of destination/layout chooser UI during drag/drop or shell-driven transfers.

## Window geometry

- Default collapsed size: **380 × 72 epx**
- Expanded queue target: **~300 epx**, bounded by measured queue content
- Compact width target: **380 epx**
- Minimum practical width: **360 epx**
- Outer content gutter: **8 epx**
- Related-control spacing: **8 epx**
- Tight inline spacing: **4 epx**

Spacing tiers are contractual, not advisory. The compact transfer surface uses the shared `DesignTokens.xaml` rhythm instead of ad-hoc values.

## Collapsed composition

The collapsed window is one transfer surface. Do not place a second copier/card/capsule inside the HWND.

Composition:
1. top row: VelocityCopy brand mark, current item, throughput + percentage + ETA
2. bottom row: centered primary action cluster with Pause/Resume, Cancel, Options and queue disclosure
3. progress expressed by the surface fill itself
4. native Windows caption buttons remain visible at the top-right

Skip and Stop live in Options rather than consuming permanent width in the primary row. The primary action cluster is centered independently of the system caption area so the controls do not become a long right-heavy strip.

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

## Window movement and caption chrome

The compact copier must remain movable with normal pointer dragging. `TitleBarDragRegion` remains the explicit drag surface and is registered with `SetTitleBar`.

VelocityCopy keeps the native Windows caption cluster visible: Minimize, Maximize and Close remain in the top-right. Maximize may remain disabled because the copier owns its compact/expanded size, but the native three-button chrome remains visually consistent with Windows 11.

Only the **top caption-content row** reserves `AppWindowTitleBar.RightInset`. The centered bottom action row must not inherit that inset; this avoids wasting the same caption width twice and keeps Pause/Cancel/Options/Queue centered. The inset is converted from physical pixels to effective pixels for the current HWND DPI and is reapplied on `AppWindow.Changed`. User pointer resizing remains disabled; VelocityCopy may resize its own HWND programmatically between collapsed and expanded queue states.

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
- Keep core transfer state independent of concrete WinUI controls.
- Reuse queue visuals where possible and preserve selection/focus by stable file IDs during live refresh.
