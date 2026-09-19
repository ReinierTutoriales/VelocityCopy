# Explorer integration

## Current mechanism

The classic x64/ARM64 installer registers one native COM class under Directory, Drive and Folder / shellex / DragDropHandlers. The adapter implements IShellExtInit and IContextMenu. Initialize snapshots CF_HDROP paths from IDataObject and resolves the actual target PIDL with SHGetPathFromIDListEx. Virtual targets and missing or malformed file lists are rejected. Explicit Copy here / Move here commands carry an explicit operation in protocol version 2.

The former selection Copy, Paste, CopyTo and Open verb classes, their resources and staging-only IPC actions are retired. Upgrade and uninstall remove their machine-wide registrations. Tray, single-instance IPC, process activation, planning and the copy/move engine remain. Clipboard staging is removed because it has no consumer in this transfer-based design.

## SuperCopier-style default selection

The implementation is original code. The conceptual reference is [SuperCopier2 DDShellExt.cpp](https://github.com/gligli/SuperCopier2/blob/861e9dd/SC2C%2B%2B/DDShellExt.cpp): it adds two transfer commands and uses SetMenuDefaultItem to redirect the existing default. Its historical mapping is command 1 = Copy and command 2 = Move. VelocityCopy applies this mapping only when a recognized native default exists and otherwise preserves the existing default. Explicit invocation always wins over Preferred DropEffect. No global keyboard hooks, process injection, copy hooks or filesystem watchers are used.

**This is a compatibility technique, not a documented Windows 11 interception contract.** Microsoft documents DragDropHandlers as extensions of the right-button drop menu. The numeric default command IDs and whether Explorer routes ordinary Ctrl+V or left-button drops through this menu are implementation-dependent. A successful DLL test with a synthetic menu proves the adapter's logic, not automatic Ctrl+V interception.

Automatic Ctrl+C/Ctrl+X -> Ctrl+V is a required product gate. This change must not be described as meeting that gate until an installed Windows 11 build is exercised end to end. If Explorer bypasses this handler, do not silently substitute a manual menu and mark the gate green.

## Ownership and failure

The extension validates and snapshots data; it does not enumerate directories or copy files. InvokeCommand dispatches to the existing user/session-local pipe, with on-demand activation of VelocityCopy.WinUI.exe when needed. The current transport's connection timeout does not bound all synchronous pipe writes; a hung receiver remains a transport limitation.

The app owns layout selection, validation, queueing, conflicts, cancellation and source deletion after successful Move. Queue admission is not transfer completion: the extension never sends PASTESUCCEEDED or a performed MOVE back to the source. A failed handoff returns failure; automatic native retry is not assumed. Shell objects are released locally and never stored in IPC. An instance rejects duplicate invocation after a successful handoff.

Drag/drop onto the VelocityCopy window still appends only to an active transfer. It is distinct from Explorer's folder drop target.

## Evidence and required Windows checks

Automated coverage: COM lifetime, source snapshot, Unicode/ANSI CF_HDROP, malformed offsets, missing destination, bounded menu IDs, default-only queries, unknown native defaults, Copy/Move through real IPC, duplicate invocation and no premature source completion. Protocol tests reject v1, invalid operations/layouts and truncated messages.

On installed Windows 11 x64 and ARM64 verify:
- Ctrl+C -> Ctrl+V and Ctrl+X -> Ctrl+V into a normal folder and drive root;
- right-button and left-button drops, files, folders and multiselection;
- resident app and on-demand startup, unavailable app and cancelled layout;
- same-volume and cross-volume Move: no premature source deletion;
- UNC and long paths, unsupported virtual folders and other handlers;
- upgrade removes old verbs; uninstall removes the new registration;
- Explorer restart/sign-out after replacing a loaded DLL.

Record exact OS build, architecture, commit SHA and observed route. CI/package success alone is insufficient.

## References

- [Microsoft: creating shortcut menu and drag/drop handlers](https://learn.microsoft.com/en-us/windows/win32/shell/context-menu-handlers)
- [Microsoft: IShellExtInit::Initialize](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellextinit-initialize)
- [Microsoft: Shell clipboard formats](https://learn.microsoft.com/en-us/windows/win32/shell/clipboard)
