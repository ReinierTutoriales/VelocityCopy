# VelocityCopy UI specification

VelocityCopy is a compact Windows 11 utility, not a full-screen file manager. The window must remain useful while occupying as little screen space as practical.

## Window geometry

Use effective pixels (epx) and keep dimensions in multiples of 4 where practical.

- Default collapsed size: **460 × 156 epx**
- Expanded queue size: **460 × 380 epx**
- Preferred width range: **440–520 epx**
- Minimum practical width: **420 epx**
- Outer content gutter: **12 epx**
- Major vertical section spacing: **12 epx**
- Related-control spacing: **8 epx**
- Tight inline spacing: **4 epx**

These are defaults, not architectural limits. The layout must remain responsive and adjustable as the product evolves.

## Collapsed composition

The primary window contains only:

1. compact title bar / app identity
2. current item name
3. one global progress bar
4. throughput + ETA
5. essential transport controls
6. one disclosure affordance for the queue

Conceptual layout:

```
╭────────────────────────────────────────────╮
│  VelocityCopy                         — □ × │
│                                            │
│  Windows11_24H2.iso                        │
│  ███████████████████░░░░░░░   72%         │
│  684 MB/s                      1 min 42 s  │
│                                            │
│  ⏸ Pausar   ⏭ Saltar   ■ Detener     ⋯   │
│  ▾ 2,318 archivos pendientes               │
╰────────────────────────────────────────────╯
```

The current item name is always directly above the progress bar. Long names use end ellipsis and expose the full path through tooltip/details rather than increasing window height.

## Progress bar

- One global progress bar only.
- Preferred visual height: **6–8 epx**.
- Horizontal margin follows the 12 epx outer gutter.
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

Use WinUI compact density where appropriate for a desktop utility. Do not reduce hit targets to the point that keyboard/mouse interaction becomes difficult.

## Queue panel

The queue is collapsed by default. Expanding it must not create an entirely different application window.

- target expanded height: ~380 epx
- resizable later; 380 epx is the default, not a fixed limit
- virtualized item presentation
- drag/drop reordering
- keyboard selection
- controls: Subir, Bajar, Eliminar
- Delete removes selected pending entries from the copy plan, never source files
- current/completed entries are not reorderable

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
