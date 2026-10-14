# VelocityCopy release gates

These gates define the current release pipeline and must remain aligned with the
engineering and implementation rules.

## Release model

VelocityCopy currently publishes unsigned classic installers for x64 and ARM64.
The official publication path is `.github/workflows/release.yml`; there is no
second tag-driven packaging path.

A release candidate is a specific commit on `main` whose Windows CI and Windows
Package runs are green. The release workflow:

- runs only by explicit owner-triggered `workflow_dispatch` from `main`;
- requires a semantic version matching `Version.h` on the selected commit;
- requires the selected commit to be contained in `main`;
- finds a successful `Windows Package` push run for that exact commit;
- downloads the x64 and ARM64 installer artifacts from that run;
- verifies each artifact contains exactly the expected PE installer;
- renames the installers for the release, generates `SHA256SUMS.txt`, and
  publishes those exact bytes without rebuilding or modifying them;
- creates the release tag on the same commit and verifies the published assets
  and tag target.

`Windows Package` is a validation/build workflow. It runs on pull requests,
`main` pushes and explicit manual dispatch. It must not publish releases or
implement a separate tag policy.

If production Authenticode signing is introduced later, it must be added to the
single release path with explicit credential handling, signature verification
and timestamp verification before publication. Do not keep a second signing
pipeline in parallel with the official release workflow.

## Required automated gate

Before publication, the exact candidate commit must pass:

- Windows CI x64 Release configure/build/ctest;
- ARM64 core compilation;
- ASan RelWithDebInfo compilation;
- self-contained WinUI x64 build and launch smoke;
- Windows Package x64 and ARM64 payload/build validation;
- PE-machine checks for `VelocityCopy.WinUI.exe`, `VelocityCopy.Shell.dll`
  and `VelocityCopy.StartupHelper.exe`;
- PRI/resource and application-icon validation;
- deterministic Authenticode pipeline tests on disposable copies using an
  ephemeral self-signed certificate (test-only; never packaged);
- classic installer generation for x64 and ARM64;
- x64 upgrade, install, installed-launch and uninstall smoke;
- payload inventory and forbidden-component checks.

The upgrade fixture is the published unsigned x64 v1.1.0 installer, pinned to
SHA256 `21647db6ca0b98b8c523c7f68ec56fb14b0151d536d97d5eb3677d17a6da22a7`.
Historical v1.0.0 upgrade coverage requires its original external fixture and
remains a manual check.

## Required real-Windows sign-off

CI and Package are necessary but do not prove every shell/UI/device path.

Before declaring a release fully validated on hardware, test on x64 Windows 11:

- Explorer Ctrl+C/V, Ctrl+X/V and drag/drop with the shell extension loaded;
- Explorer restart while VelocityCopy is resident;
- upgrade from the oldest supported published fixture;
- resident uninstall;
- OneDrive/cloud-file hydration;
- junction/reparse-point rejection;
- Move with destination-full/unavailable conditions;
- destination removal/disconnect during transfer;
- DPI/text-size, keyboard/focus, notification/tray and taskbar behavior relevant
  to the release.

Source deletion for Move must never occur before the copied destination has
passed the engine completion rules.

ARM64 compilation and PE validation do not prove ARM64 runtime behavior. Record
physical ARM64 install/launch/shell-integration validation separately when a
real ARM64 device is available.

## Packaging rules

- Distribution is classic self-contained NSIS, not MSIX.
- Supported release architectures are x64 and ARM64 only.
- No x86 and no portable distribution.
- WinUI remains unpackaged and self-contained
  (`WindowsAppSDKSelfContained=true`, `AppxPackage=false`).
- Each payload includes `VelocityCopy.WinUI.exe`,
  `VelocityCopy.Shell.dll` and `VelocityCopy.StartupHelper.exe`.
- Registered installer payloads must be asserted before NSIS runs.
- The x64 package gate smoke-installs, launches and uninstalls the classic setup.
- Do not add Chocolatey, AppX or MSIX to the required release path unless product
  requirements explicitly change.

## Workflow-change safety

Changes to CI, Package or Release workflows are production changes.

Required commit-CI behavior: x64 configure/build/ctest; ARM64 core compile; ASan
RelWithDebInfo compile; WinUI x64 build/launch; no installer publication.

Required Package behavior: pull-request, `main`-push and manual triggers;
self-contained WinUI payload; classic x64/ARM64 installers; payload/resource
verification; x64 upgrade/install/launch/uninstall smoke; artifact upload.

Required Release behavior: owner-only explicit dispatch from `main`; exact
version/SHA validation; reuse of an already successful Package run on that exact
commit; checksum generation; no binary rebuild; verified tag and assets.

## Branch and commit hygiene

- `main` is the only intended long-lived branch.
- Do not create temporary branches unless isolation is required.
- Remove obsolete temporary branches when possible.
- One logical change should produce one coherent commit.
- Do not intentionally push known-broken intermediate states.

## Failure triage

If CI, Package or Release is red, inspect the exact failing job/log before
editing and classify the failure as configure/build, production test,
architecture contract, payload/package, installer smoke, publication or
runtime/integration.
