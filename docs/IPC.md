# IPC and single-instance architecture

VelocityCopy keeps Explorer integration out of process. Shell extensions only package a validated `ShellRequest` and hand it to the main application over a local named pipe.

## Rules

- One primary VelocityCopy instance per interactive Windows session.
- Named pipe and single-instance mutex use an explicit protected DACL granting access only to the current user and LocalSystem.
- The pipe rejects remote clients with `PIPE_REJECT_REMOTE_CLIENTS`.
- After connection, the server validates the client PID with `GetNamedPipeClientProcessId` and rejects clients whose Windows session differs from the server session. IPC is local-session only.
- IPC uses a named pipe scoped by Windows session ID.
- No TCP/UDP listener and no background broker process.
- Requests are versioned and length-prefixed.
- Maximum request size: 16 MiB.
- Maximum path length accepted by the protocol: 32,768 UTF-16 code units.
- Maximum sources per request: 65,535.
- Invalid, oversized, truncated or unknown-version messages are rejected before dispatch.
- Explorer-side callers must use a short timeout and must never wait for the copy operation itself.
- The pipe transfers intent only; scanning, planning, conflicts, UI and copy I/O remain in `VelocityCopy.WinUI.exe`.


## Wire version 2

The existing VCP1 magic identifies the protocol family; the version field is now 2. Header bytes are magic (u32 LE), version (u32 LE), action (u8), layout (u8), operation (u8), reserved zero (u8), then source count (u32 LE), length-prefixed UTF-16 sources and destination.

Actions: Transfer=0 (requires sources and destination), OpenVelocityCopy=1 (no paths). Operations: Copy=0, Move=1. Version 1 and the retired stage/paste/prompt actions are rejected. New DLL and app must be upgraded together; exit the old resident app and unload old Explorer DLLs before relying on the new contract. Paths must be absolute and contain no embedded NUL. Unknown operation/layout/reserved bytes are rejected.

Transport handoff is not a durable admission/completion acknowledgement. Neither pipe success nor process creation proves execution completed; the extension must not tell IDataObject to delete sources.

## Cancellable transport and bounded frames

The wire version, admission semantics, user/session DACL and remote-client rejection
are unchanged. Connect/read/write use documented overlapped named-pipe I/O.
A persistent stop event interrupts both idle connection waits and incomplete
headers/payloads. Cancellation targets the exact OVERLAPPED and drains its
completion before releasing its event or buffer. There is no idle polling timer.

An accepted connection has five seconds to finish one length-prefixed frame;
truncated/stalled input is discarded and the next client remains serviceable.
The normal pipe instance is disconnected/reused, with a 64 KiB buffer rather
than a buffer sized for the entire 16 MiB protocol ceiling. Requests up to the
existing ceiling still stream through it. Sender writes share the original
connection/write deadline. A bounded synchronous client flush keeps the handle
alive until the server consumes the buffered frame; its worker is joined and
cancellation is retried if it races the flush start.

Native regressions cover partial header/payload shutdown, stalled-frame recovery,
malformed data followed by valid activation, a 12 MB frame through the bounded
buffer, and an unresponsive receiver timing out without blocking Explorer.

References:
- https://learn.microsoft.com/en-us/windows/win32/ipc/synchronous-and-overlapped-input-and-output
- https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-server-using-overlapped-i-o
- https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex
