# Explorer integration

## Supported mechanism

The classic x64/ARM64 installer registers one native COM class under
`Directory`, `Drive` and `Folder\shellex\DragDropHandlers`. The adapter
implements `IShellExtInit` and `IContextMenu`.

Microsoft documents `DragDropHandlers` as handlers for the shortcut menu shown
when Shell objects are dragged with the **right mouse button**. VelocityCopy
therefore uses this surface only for two explicit commands: **Copy here** and
**Move here**. It does not replace Explorer's native default command and does not
depend on undocumented numeric command IDs.

`Initialize` snapshots `CF_HDROP` paths from the supplied `IDataObject` and
resolves the destination PIDL with `SHGetPathFromIDListEx`. Virtual targets,
relative paths and malformed/bounds-violating file lists are rejected. The
explicit command selected by the person determines Copy versus Move.

The former selection Copy/Paste/CopyTo/Open verb classes and clipboard staging
remain retired. There is one production transfer path:
`ShellRequest -> ShellSession -> JobExecutor/LiveCopyPlan`.

## Platform limit: ordinary Explorer Paste is not interceptable by a supported hook

The project does **not** claim to replace ordinary Explorer `Ctrl+V`.

Microsoft's documented Shell extension points do not provide a supported
mechanism for a third-party classic application to take ownership of Explorer's
normal Copy/Paste operation:

- `DragDropHandlers` extend the right-button drag/drop menu.
- `IContextMenu` adds/invokes explicit shortcut-menu commands.
- `ICopyHook` can only approve or veto folder/printer operations; it does not
  perform or redirect the operation and is not notified of completion.
- `IFileOperation` lets an application execute its own file operations; it is
  not an interception surface for File Explorer's operation.
- Microsoft documents Copy, Paste and Delete as Shell actions Windows handles,
  rather than extensible Ribbon actions.

Accordingly, VelocityCopy will not use a global low-level keyboard hook, process
or DLL injection, Explorer patching, clipboard polling, filesystem watchers, or
an undocumented command-ID/default-menu trick to emulate transparent paste.

If Microsoft introduces a stable documented interception/redirect API in a
future Windows release, evaluate it against `docs/ENGINEERING_RULES.md` before
changing this contract.

## Ownership and failure

The shell DLL validates and snapshots data only. It never enumerates directory
trees or performs file I/O.

`InvokeCommand` sends a bounded `ShellRequest` to the existing user/session
local IPC endpoint and can start VelocityCopy on demand if the primary process
is absent. A stalled or unavailable receiver must fail the handoff within the
transport deadline instead of hanging Explorer.

The app owns planning, queueing, conflicts, cancellation, retries and source
deletion after a successful Move. Queue admission is not transfer completion:
the extension never reports `PASTESUCCEEDED` or a performed Move to the source.
Shell objects are released locally and are never retained across IPC.

Drag/drop onto the VelocityCopy WinUI window is separate: it appends
`StorageItems` only to the currently active transfer and never invents a
destination.

## Evidence and required Windows checks

Automated coverage must prove:

- COM lifetime and unload behavior;
- Unicode/ANSI `CF_HDROP` snapshots and malformed-offset rejection;
- missing destination rejection;
- exactly two bounded menu IDs;
- Explorer's existing native default remains unchanged;
- explicit Copy/Move dispatch through the real IPC protocol;
- duplicate invocation rejection;
- no premature source completion/deletion.

Installed Windows checks remain required for the behaviors automation cannot
prove:

- right-button file/folder/multiselection drag to normal folders and drive roots;
- both explicit Copy here and Move here commands;
- resident process and bounded on-demand activation;
- same-volume and cross-volume Move with no premature source deletion;
- UNC/long paths and unsupported virtual targets;
- Explorer restart/sign-out across shell-DLL upgrade and uninstall;
- x64 and, when hardware is available, ARM64 shell integration.

Ordinary Explorer `Ctrl+C/Ctrl+X -> Ctrl+V` remains Windows-owned behavior and
is not a VelocityCopy release gate.

## Microsoft references

- Shell extension handlers:
  https://learn.microsoft.com/windows/win32/shell/handlers
- Registering Shell extension handlers:
  https://learn.microsoft.com/windows/win32/shell/reg-shell-exts
- `IContextMenu`:
  https://learn.microsoft.com/windows/win32/api/shobjidl_core/nn-shobjidl_core-icontextmenu
- Copy hook handlers:
  https://learn.microsoft.com/windows/win32/shell/how-to-create-copy-hook-handlers
- `IFileOperation`:
  https://learn.microsoft.com/windows/win32/api/shobjidl_core/nn-shobjidl_core-ifileoperation
- Shell drag/drop and Clipboard transfers:
  https://learn.microsoft.com/windows/win32/shell/dragdrop
- Explorer Ribbon extensibility:
  https://learn.microsoft.com/windows/win32/shell/extending-the-ribbon
