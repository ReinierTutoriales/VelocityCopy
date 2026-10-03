# VelocityCopy release gates

These gates define the 1.1.0 stable release pipeline and must remain aligned with the engineering and implementation rules.

## Stable release gate

- `main` must pass Windows CI: x64 Release configure, build and `ctest`.
- Windows Package runs on pull requests, every main commit, `v*` tags and `workflow_dispatch`; commit CI stays free of packaging while package validation remains a separate workflow.
- x64 and ARM64 classic installers are built with payload verification, including PE-machine checks for both `VelocityCopy.WinUI.exe` and `VelocityCopy.Shell.dll`; smoke install/uninstall runs on x64.
- A stable tag is cut only from a main commit whose CI and Package runs are green.
- Stable `v*` tag packaging requires valid Authenticode signatures on `VelocityCopy.WinUI.exe`, `VelocityCopy.Shell.dll` and both installers. Tag jobs fail closed when signing secrets are absent or a signature is invalid.

## Packaging rules

- Distribution is classic self-contained NSIS, not MSIX.
- No x86 and no portable distribution.
- WinUI remains unpackaged and self-contained (`WindowsAppSDKSelfContained=true`, `AppxPackage=false`).
- Each x64/ARM64 payload includes `VelocityCopy.WinUI.exe` and `VelocityCopy.Shell.dll`.
- Registered installer payloads must be asserted before NSIS runs.
- The x64 package gate smoke-installs and uninstalls the classic setup.
- Gate 4 on real Windows must verify unpackaged `x:Uid` resource resolution with Procmon evidence for `.pri` access; CI resource validation does not close this runtime route.
- Do not add Chocolatey or AppX/MSIX deployment to the required path. Signing credentials are supplied only as GitHub secrets at stable-tag time and must never be committed or packaged.

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
