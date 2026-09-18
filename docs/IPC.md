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
- The pipe transfers intent only; scanning, planning, conflicts, UI and copy I/O remain in `VelocityCopy.exe`.

`VelocityCopyIpcHost` exists as a minimal development host for exercising the transport. It is not the final UI process and does not replace the main application.
