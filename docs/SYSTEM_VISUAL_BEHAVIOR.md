# System visual behavior

VelocityCopy should feel native to Windows 11 and avoid owning visual policy that the operating system already provides.

## Theme and accent

- Follow Windows light/dark theme automatically.
- Use WinUI theme resources instead of hard-coded foreground/background colors.
- Use the Windows accent resource for selected states, drag-over emphasis, primary actions and focus indicators.
- Never use accent color as a large permanent fill when a neutral Fluent surface is clearer.

## Materials

- Mica is the default backdrop for the main window when supported.
- Acrylic is reserved for transient flyouts/menus and only when supported by the system.
- Fall back to standard theme brushes when transparency/material effects are unavailable or disabled.
- Do not layer multiple blur surfaces.

## Motion

- Respect system animation preferences.
- All motion must be short and functional: state transition, expansion/collapse, drag-over feedback.
- No continuous decorative animation.
- When animations are disabled or reduced, state changes happen immediately.

## Accessibility

- High Contrast overrides decorative styling.
- Do not encode meaning only through color; pair selected/error states with icon/text/shape.
- Keyboard focus remains visible with native focus visuals.
- Touch/mouse hit targets remain usable even under compact density.

## Drag and drop feedback

The entire main window is a valid drop surface.

- Drag enter: subtle accent border/surface treatment and localized "Drop to copy" prompt.
- Drag over: no file-system enumeration or destination probing on the UI thread.
- Drag leave: restore normal surface immediately.
- Drop: capture the selected StorageItems/paths once and transition to the destination step.
- Multiple files and folders are first-class; preserve incoming order and present only a bounded preview.

## Performance rule

Visual effects are subordinate to responsiveness. No blur, shadow, animation, icon extraction or metadata query may delay copy control, Explorer IPC, drag/drop acknowledgement or destination navigation.
