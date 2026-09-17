# Explorer Integration Policy

VelocityCopy integrates with File Explorer as a guest, not as an execution host.

## Goals

- Appear in the Windows 11 modern context menu using `IExplorerCommand`.
- Support files, folders, multi-selection and folder background commands.
- Provide actions equivalent to **Copy with VelocityCopy**, **Paste with VelocityCopy**, **Copy to...**, and **Open VelocityCopy**.
- Reuse the same destination/layout semantics as drag and drop.
- Start VelocityCopy on demand when no app instance is running.

## Stability rules

1. The shell extension must remain tiny and synchronous work must be bounded.
2. No file enumeration, hashing, benchmarking, network access or copy execution inside Explorer.
3. No global keyboard hooks and no interception of Explorer's own Ctrl+C/Ctrl+V implementation.
4. `GetTitle`, `GetIcon`, `GetState` and related menu-building calls must return quickly.
5. `Invoke` only collects the current shell selection/context and forwards a versioned `ShellRequest` to VelocityCopy.
6. The main process owns planning, queueing, collision policy, resume, copy execution and UI.
7. Explorer integration must fail closed: if IPC or app launch fails, Explorer continues normally.
8. The extension must not keep background threads, timers or polling loops alive inside Explorer.

## Process boundary

```text
Explorer.exe
   |
   |  IExplorerCommand
   v
VelocityCopy.Shell.dll
   |
   |  small versioned IPC request
   v
VelocityCopy.exe
   |
   +-- JobPlanner
   +-- editable queue
   +-- CopyEngine / strategy selector
```

## Context menu surface

Keep the top-level surface minimal. Prefer one app-attributed VelocityCopy entry with subcommands when supported:

- Copy with VelocityCopy
- Paste with VelocityCopy
- Copy to...
- Open VelocityCopy

Commands only appear when their context is meaningful. Paste is shown for a folder/background destination; copy actions require one or more selected filesystem items.

## Copy/Paste behavior

VelocityCopy will not replace Windows clipboard semantics globally. A user may invoke VelocityCopy commands from Explorer, and the shell adapter passes only the selected paths and destination context. The core then applies the same layout rules already used by drag and drop:

- direct: place item/content directly in destination;
- preserve: retain only the selected folder or the immediate containing folder for a loose file, never the full ancestral tree.

## Startup and availability

Installed builds register a packaged startup task for the main VelocityCopy app. After the app has been launched once, Windows can start the primary instance silently at user sign-in so Explorer handoff is immediate. The resident app remains idle on a blocking named pipe and performs no scanning, polling, hashing or copy work until a request arrives.

The shell extension still treats the app as optional: it first connects to an existing primary instance through IPC; if none is available, it launches the app on demand. No permanently running Explorer helper, Windows service, global hook or worker thread inside Explorer is required. The user can disable VelocityCopy startup through Windows Settings or Task Manager.
