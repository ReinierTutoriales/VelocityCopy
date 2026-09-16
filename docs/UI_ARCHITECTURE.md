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
