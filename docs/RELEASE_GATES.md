# VelocityCopy release gates

These gates define the 1.1.0 stable release pipeline and must remain aligned with the engineering and implementation rules.

## Stable release gate

- `main` must pass Windows CI: x64 Release configure, build and `ctest`.
- Windows Package runs on pull requests, every main commit, `v*` tags and `workflow_dispatch`; commit CI stays free of packaging while package validation remains a separate workflow.
- x64 and ARM64 classic installers are built with payload verification, including PE-machine checks for both `VelocityCopy.WinUI.exe` and `VelocityCopy.Shell.dll`; smoke install/uninstall runs on x64.
- A stable tag is cut only from a main commit whose CI and Package runs are green.
- Package CI exercises the Authenticode implementation on disposable copies with an ephemeral trusted code-signing certificate; the test certificate is removed afterward and is never packaged.
- Stable `v*` tag packaging requires valid Authenticode signatures and RFC3161 timestamps on `VelocityCopy.WinUI.exe`, `VelocityCopy.Shell.dll`, `VelocityCopy.StartupHelper.exe` and both installers. Tag jobs fail closed when signing secrets are absent or verification fails.

## Required real-Windows sign-off

CI and Package are necessary but do not authorize a stable tag by themselves.

- Before `v1.1.0`, an x64 Windows 11 machine must pass Explorer end-to-end checks with the shell extension loaded: Ctrl+C/V, Ctrl+X/V, drag/drop, Explorer restart, upgrade from 1.0.0, resident uninstall, OneDrive/cloud-file hydration and junction rejection.
- The x64 sign-off must include destructive/fault cases for Move: destination full or unavailable and removal/disconnect of a USB destination during transfer. The source must never be deleted before the copied destination has passed the engine's completion validation.
- ARM64 may be published as a 1.1.0 release asset only after a real Windows ARM64 install/launch/shell-integration smoke. Cross-compilation and PE validation alone do not satisfy this gate.
- Record the exact `main` SHA used for the real-Windows sign-off. The stable tag must point to that exact SHA after its CI and Package runs are green.
- Do not infer Explorer runtime safety from the write-locked DLL fixture: that fixture proves file-replacement mechanics, not that an already-loaded Explorer process switched to the new COM image.

## Packaging rules

- Distribution is classic self-contained NSIS, not MSIX.
- No x86 and no portable distribution.
- WinUI remains unpackaged and self-contained (`WindowsAppSDKSelfContained=true`, `AppxPackage=false`).
- Each x64/ARM64 payload includes `VelocityCopy.WinUI.exe` and `VelocityCopy.Shell.dll`.
- Registered installer payloads must be asserted before NSIS runs.
- The x64 package gate smoke-installs and uninstalls the classic setup.
- Gate 4 on real Windows must verify unpackaged `x:Uid` resource resolution with Procmon evidence for `.pri` access; CI resource validation does not close this runtime route.
- Do not add Chocolatey or AppX/MSIX deployment to the required path. Production signing credentials are supplied only as GitHub secrets at stable-tag time and must never be committed or packaged.

## Workflow-change safety

Architecture tests validate durable workflow behavior.

Required commit-CI behavior: x64 configure/build/ctest; ARM64 core compile; ASan RelWithDebInfo compile; WinUI x64 build; no AppX installation; no packaging in commit CI.

Required package behavior: main-push, `v*` tag and manual triggers; self-contained WinUI payload; classic x64 and ARM64 VelocityCopy installers; payload verification; smoke install/uninstall.

## Branch and commit hygiene

- `main` is the only intended long-lived branch.
- Do not create temporary branches unless isolation is required.
- Remove obsolete temporary branches when possible.
- One logical change should produce one coherent commit.
- Do not intentionally push known-broken intermediate states.

## Failure triage

If CI/package is red, read the actual failing job/log before editing and classify it as configure/build, production test, architecture contract, payload/package, installer smoke or runtime integration.
