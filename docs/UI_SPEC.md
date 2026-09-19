# VelocityCopy UI specification

VelocityCopy is a compact Windows 11 utility, not a full-screen file manager. The window must remain useful while occupying as little screen space as practical.

## Window geometry

Use effective pixels (epx) and keep dimensions in multiples of 4 where practical.

- Default collapsed size: **460 × 72 epx**
- Expanded queue size: **460 × 300 epx**
- Preferred width range: **440–520 epx**
- Minimum practical width: **420 epx**
- Outer content gutter: **12 epx**
- Major vertical section spacing: **12 epx**
- Related-control spacing: **8 epx**
- Tight inline spacing: **4 epx**

These are defaults, not architectural limits. The layout must remain responsive and adjustable as the product evolves.

## Collapsed composition

The collapsed window is a single compact transfer surface. The top-level window and the copier surface are the same visual body: do not inset a second rounded/card-like transfer container inside the HWND. The custom title-bar drag region belongs to this surface rather than consuming a separate decorative band.

The compact body occupies the full collapsed height; internal padding belongs to its content, not to an outer wrapper around the copier.

The collapsed composition is:

1. the VelocityCopy brand mark anchored at the far left
2. current item name
3. throughput + percentage + ETA as secondary metadata
4. essential icon-only transport controls
5. a queue disclosure triangle anchored at the far right
6. global progress expressed by the transfer surface itself

Conceptual layout:

```
╭────────────────────────────────────────────╮
│ ◉  Windows11_24H2.iso  72%  684 MB/s  1m42s  ⏸  ⚙  ▸ │
╰────────────────────────────────────────────╯
```

The logo remains visible at the far left in every compact state. The disclosure at the far right changes orientation when the queue is expanded. The current item uses end ellipsis rather than increasing the collapsed height.

## Integrated progress surface

- There is one global progress indication only: the copier surface fill itself.
- Do not keep a hidden or visible ProgressBar as a second progress model; runtime progress drives the surface fill and percentage text from the same fraction.
- Do not draw a separate progress strip in the collapsed window.
- Progress fills the compact transfer surface from left to right behind its content.
- Keep the fill visually subordinate so text, logo, icons and focus visuals retain contrast.
- The fill extends beneath the full transfer surface; it is not confined to the text region.
- Do not animate continuously when no progress is occurring.
- Update UI from periodic snapshots rather than per I/O completion.

## Typography

Use the Windows type system / Segoe UI Variable through WinUI theme resources rather than embedding a font.

- current item: Body / Body Strong depending on hierarchy
- throughput and ETA: Caption or Body
- percentage: compact Body Strong when displayed
- avoid oversized headings inside the copy window

## Fluent materials

- Mica: base window material
- Acrylic: transient flyouts, context menus and destination-choice surfaces only
- native rounded window corners
- subtle elevation only where it communicates hierarchy
- no decorative blur layers or permanent glow

## Density

Use WinUI compact density where appropriate for this desktop-first utility. Preserve keyboard focus visuals, accessible names/tooltips and practical pointer targets. Prefer progressive disclosure over adding permanent controls.

## Queue panel

The queue is collapsed by default. Expanding it must not create an entirely different application window.

- target expanded height: ~300 epx
- expansion must size the existing HWND from the measured XAML content so the queue is not clipped by a hard-coded shell height
- 300 epx remains a design target, not a clipping boundary; measured content wins when it needs more room
- virtualized item presentation
- drag/drop reordering
- keyboard selection
- controls: Subir, Bajar, Eliminar
- Delete removes selected pending entries from the copy plan, never source files
- current/completed entries are not reorderable
- the expanded queue is a continuation of the same window surface: no queue card background, inner rounded container, or second visual shell
- expansion measures the realized queue layout before resizing the existing HWND so header, list, and edit controls remain inside the visible client area

For very large queues, never instantiate a visual element for every file.

## Responsive implementation rules

Do not encode the design as absolute child coordinates. Use Grid, Auto sizing, Min/Max constraints, theme resources and reusable styles/tokens.

Keep visual constants in a small design-token/resource layer so margins, corner radii, density and dimensions can evolve without rewriting views.

Suggested tokens:

- `WindowCompactWidth`
- `WindowCollapsedHeight`
- `WindowExpandedHeight`
- `ContentGutter`
- `SectionSpacing`
- `InlineSpacing`
- `ProgressBarHeight`

The UI layer must remain replaceable and evolvable. Core copy, queue and shell behavior must not depend on concrete WinUI controls.

## Localization and text expansion

All visible strings must come from localization resources. No user-facing sentence should be hard-coded in XAML or C++ UI code.

Layouts must tolerate normal translation expansion without clipping. Do not size buttons based on one language string. Prefer Auto width with MinWidth where needed.

Support right-to-left layout at the resource/layout level from the beginning even if the first shipped languages are left-to-right.

Initial languages:

- `en-US` — fallback/default
- `es-ES` — first translated locale

Additional BCP-47 languages must require resources only, not changes to copy-engine logic.

## Performance rules

- defer queue visuals until expanded
- use deferred creation for non-visible surfaces
- use a virtualizing list/ItemsRepeater for large queues
- avoid nested non-virtualizing ScrollViewer/StackPanel combinations around the queue
- no per-file animations
- no per-file progress controls
- no UI updates on every I/O completion
- retain a single source of truth in the core model


### Iconography contract

Compact transfer actions use `FontIcon` with `Segoe Fluent Icons`; runtime state changes update the named icon glyph rather than replacing a button content with Unicode arrows or text symbols. Queue disclosure is always the named `QueueChevron`, so collapse/expand state cannot regress to improvised arrow characters.


### Shell destination/layout flow

Explorer `Copy here` / `Move here` always resolves one unambiguous destination, so the transfer applies the default layout (`PreserveSourceFolder`) and starts immediately; it never opens a prompt asking how to lay the transfer out. A destination/layout flyout (`ShellFlowFlyout`/`ShellFlowContent`) exists in the compact window and is driven by `DropFlowController`, but no current product path opens it — it previously fired unconditionally on every Explorer transfer, and on the compact HWND its content could exceed the visible surface, leaving the user unable to reach `StartCopyButton` to actually start the copy. This flow is separate from whole-window drag/drop: active-transfer drops must never open it either. Legacy `DropFlowFlyout`/`DropFlowContent` UI names are prohibited because they incorrectly couple shell-command choices to drag/drop semantics.

Before this flyout is wired to any future trigger (for example, a real multi-root-destination ambiguity), it must keep a bounded desktop width and a minimum scrollable destination-list viewport, with long destination paths remaining single-line ellipsized metadata rather than forcing flyout width growth, and transitions between destination and layout steps must remeasure the shell-flow content so controls are not clipped by dimensions inherited from the previous step. Do not reintroduce an unconditional flyout prompt for a single-destination Explorer transfer.


### Window movement

The compact copier must remain directly movable with normal pointer dragging. Its custom title-bar drag region spans a practical 32 epx band across the top of the copier surface, while interactive controls remain above it in hit testing. Do not reduce the drag target to a decorative sliver; an 8 epx region is not an acceptable desktop drag affordance.
