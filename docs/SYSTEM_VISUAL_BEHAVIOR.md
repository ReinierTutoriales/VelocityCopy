# System visual behavior

VelocityCopy should feel native to Windows 11 and avoid owning visual policy that the operating system already provides.

## Theme and accent

- Follow Windows light/dark theme automatically.
- Use WinUI theme resources instead of hard-coded foreground/background colors.
- Use the Windows accent resource for selected states, drag-over emphasis, primary actions and focus indicators.
- During an active transfer, the copier surface itself may use the Windows accent as the proportional left-to-right progress fill behind all content. The fill is state-driven, subordinate to readable foreground content, and absent when no transfer progress is being represented.

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

The main copier surface accepts file-system StorageItems only while a transfer session with an active destination exists.

- Drag enter/over: accept Copy only when an active transfer/destination exists and the data contains StorageItems.
- Do not show destination, layout, copy/move, modifier-key, or confirmation menus for drag/drop.
- Do not enumerate the file system or probe destinations on the UI thread during drag-over.
- Drop: capture the StorageItems once and append them to the active transfer using that session's existing destination and operation.
- If there is no active transfer/destination, reject the drop rather than inventing a destination.
- Drag/drop is not a destination-selection workflow.

## Performance rule

Visual effects are subordinate to responsiveness. No blur, shadow, animation, icon extraction or metadata query may delay copy control, Explorer IPC, drag/drop acknowledgement or destination navigation.
