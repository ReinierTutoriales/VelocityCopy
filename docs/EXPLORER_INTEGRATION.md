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

The same COM class is registered under
`Directory\Background\shellex\ContextMenuHandlers` for an explicit **Paste with
VelocityCopy** command. It reads `OleGetClipboard` only when invoked, snapshots
`CF_HDROP`, and uses `CFSTR_PREFERREDDROPEFFECT` for Copy versus Move (Copy
when absent). Link-only data is rejected. This command does not intercept Ctrl+V.

The former selection Copy/Paste/CopyTo/Open verb classes and clipboard staging
remain retired. There is one production transfer path:
`ShellRequest -> ShellSession -> JobExecutor/LiveCopyPlan`.

## Resident Explorer Ctrl+V integration

While VelocityCopy is resident, Ctrl+C/Ctrl+X remain native Explorer operations.
Ctrl+V in the focused filesystem folder view is routed to VelocityCopy. The
adapter uses the documented WH_KEYBOARD_LL API on a dedicated message thread.
Its callback only recognizes the shortcut, captures window/focus/clipboard
identity, and posts work. It does not perform COM, clipboard payload decoding,
filesystem access, or IPC. Both threads sleep on message queues when idle.

A separate STA worker resolves the active visible Shell view using IShellWindows,
IShellBrowser and IFolderView, snapshots the clipboard through the same decoder
as explicit Paste, and sends an identified v3 request to the existing queue.
Matching the focused view avoids choosing an inactive Explorer tab. Focus and
clipboard sequence are checked again before admission; changed contexts cancel.

Unsupported views and text fields retain native Paste via a marked SendInput
replay. Replay only occurs in the original window/focus/clipboard context and
after known non-delivery. An ambiguous IPC result is never replayed, preventing
duplicate Copy/Move. The engine alone owns source deletion.

This is a keyboard adapter, not a general replacement of every Shell operation.
It requires the resident app, an interactive desktop, and normal Explorer folder
windows. Desktop, virtual folders, mouse-menu native Paste, elevated Explorer,
and third-party file managers are outside this keyboard contract. Explicit
VelocityCopy commands remain available independently of the keyboard adapter.
Windows may remove a low-level hook after a callback timeout; the dedicated
thread and deferred work minimize that risk but do not eliminate OS limits.

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

The resident keyboard adapter is separate from these Shell extension points.
VelocityCopy does not inject into Explorer, patch it, poll the clipboard, watch
the filesystem, or replace undocumented numeric/default menu commands.

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
- exactly two bounded drag/drop menu IDs, or one background Paste ID;
- Explorer's existing native default remains unchanged;
- explicit Copy/Move dispatch through the real IPC protocol;
- duplicate invocation rejection;
- shared clipboard decoder Copy/Move/default-Copy and Link rejection, without a test DLL export;
- no premature source completion/deletion.

For an opt-in Windows OLE clipboard -> COM Paste -> IPC check, close the resident
app and run `VelocityCopyShellDllTest.exe <path-to-VelocityCopy.Shell.dll> --clipboard`.
This uses synthetic Copy/Move selections in a private window station, leaving the desktop clipboard untouched.
The normal CTest run keeps deterministic IDataObject coverage without desktop access.

Installed Windows checks remain required for the behaviors automation cannot
prove:

- right-button file/folder/multiselection drag to normal folders and drive roots;
- both explicit Copy here and Move here commands;
- resident process and bounded on-demand activation;
- same-volume and cross-volume Move with no premature source deletion;
- UNC/long paths and unsupported virtual targets;
- Explorer restart/sign-out across shell-DLL upgrade and uninstall;
- x64 and, when hardware is available, ARM64 shell integration.

Run `VelocityCopyExplorerPasteTest.exe --interactive` with the resident app
closed to exercise actual Explorer selection, Ctrl+C/Ctrl+X, Ctrl+V, IPC and
JobExecutor using disposable nested folders. This test opens Explorer windows
and replaces/clears the test clipboard. Normal CTest additionally covers adapter
start/stop/restart. Installed WinUI admission, Explorer restart, text paste,
tabs and fallback remain required interactive release checks.

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
