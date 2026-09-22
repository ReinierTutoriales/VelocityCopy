# VelocityCopy release gates

These gates define the 1.0 stable release pipeline and must remain aligned with the engineering and implementation rules.

## Stable release gate

- `main` must pass Windows CI: x64 Release configure, build and `ctest`.
- Windows Package runs on every main commit, on `v*` tags and on `workflow_dispatch`; commit CI stays free of packaging.
- x64 and ARM64 classic installers are built with payload verification; smoke install/uninstall runs on x64.
- A stable tag is cut only from a main commit whose CI and Package runs are green.

## Packaging rules

- Distribution is classic self-contained NSIS, not MSIX.
- No x86 and no portable distribution.
- WinUI remains unpackaged and self-contained (`WindowsAppSDKSelfContained=true`, `AppxPackage=false`).
- Each x64/ARM64 payload includes `VelocityCopy.WinUI.exe` and `VelocityCopy.Shell.dll`.
- Registered installer payloads must be asserted before NSIS runs.
- The x64 package gate smoke-installs and uninstalls the classic setup.
- Do not add Chocolatey, AppX/MSIX deployment, certificates or unrelated package managers to the required path.

## Workflow-change safety

Architecture tests validate durable workflow behavior.

Required commit-CI behavior: x64 configure/build/ctest; ARM64 core compile; ASan RelWithDebInfo compile; WinUI x64 build; no AppX installation; no packaging in commit CI.

Required package behavior: main-push, `v*` tag and manual triggers; self-contained WinUI payload; classic x64 and ARM64 installers; payload verification; smoke install/uninstall.

## Branch and commit hygiene

- `main` is the only intended long-lived branch.
- Do not create temporary branches unless isolation is required.
- Remove obsolete temporary branches when possible.
- One logical change should produce one coherent commit.
- Do not intentionally push known-broken intermediate states.

## Failure triage

If CI/package is red, read the actual failing job/log before editing and classify it as configure/build, production test, architecture contract, payload/package, installer smoke or runtime integration.
