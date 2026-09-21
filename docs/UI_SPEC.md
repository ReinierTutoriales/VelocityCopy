# VelocityCopy UI specification

VelocityCopy is a compact Windows 11 copy/move utility. The window itself is the copier surface; it must stay compact, direct, and free of destination/layout chooser UI during drag/drop or shell-driven transfers.

## Window geometry

- Default collapsed size: **360 × 72 epx**
- Expanded queue target: **~300 epx**, bounded by measured queue content
- Fixed compact width: **360 epx**
- Minimum practical width: **360 epx**
- Outer content gutter: **12 epx**
- Related-control spacing: **8 epx**
- Tight inline spacing: **4 epx**

Spacing tiers are contractual, not advisory. The compact transfer surface uses the shared `DesignTokens.xaml` rhythm instead of ad-hoc per-control numbers: 12 epx content gutter, 8 epx group separation, and 4 epx inline control spacing. Queue-row vertical spacing follows the same 4 epx inline tier.

## Collapsed composition

The collapsed window is one transfer surface. Do not place a second copier/card/capsule inside the HWND.

Order:
1. VelocityCopy brand mark at the far left
2. current item
3. throughput + percentage + ETA
4. Pause/Resume
5. Cancel
6. Options (`…`)
7. queue disclosure triangle at the far right
8. progress expressed by the surface fill itself

The compact surface must prioritize the filename/telemetry region. Skip and Stop live in Options rather than consuming permanent width. Options also owns queue persistence, About, and Hide to tray. The system caption buttons are removed; VelocityCopy keeps the native border/rounding but does not spend compact content width on Minimize/Maximize/Close affordances that duplicate tray behavior.

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
- The About version resolver reads the running executable `VERSIONINFO` first and falls back to the compile-time `Version.h` identity.

## Window movement and chrome

The compact copier must remain movable with normal pointer dragging. `TitleBarDragRegion` remains the explicit drag surface and is registered with `SetTitleBar`.

VelocityCopy uses an `OverlappedPresenter` with native border retained and the system title bar removed via `SetBorderAndTitleBar(true, false)`. Minimize, Maximize and pointer resizing are disabled. Because there is no system caption-button cluster, the UI must not contain `RightInset` compensation, `ApplyTitleBarInset`, or AppWindow title-bar-change bookkeeping. Programmatic resizing between collapsed and expanded queue states remains allowed.

If a future Windows App SDK version breaks dragging with the custom title bar while the system title bar is hidden, fix dragging with the supported non-client caption-region API; do not restore permanent caption buttons merely to regain drag behavior.

## Fluent/system integration

- Use Mica as the base window material when available.
- Use native Windows 11 outer rounding/border.
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
