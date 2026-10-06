# Package audit — 2026-10-06

## Findings and changes

The project referenced the Windows App SDK 2.4.0 metapackage. Its actual NuGet
nuspec references WinUI, Foundation, InteractiveExperiences, DWrite, Runtime,
AI, ML, Widgets and Search. VelocityCopy uses XAML, windowing, dispatching,
AppLifecycle, MRT resources and storage pickers; no AI/ML/Widgets/Search API
is used by the current sources.

Reference the required component packages directly, preserving the exact versions
from the 2.4.0 nuspec: WinUI 2.3.6, Foundation 2.3.9,
InteractiveExperiences 2.1.6, DWrite 2.1.0 and Runtime 2.4.0.
Base remains transitive. Preserve self-contained deployment, C++/WinRT,
all WinUI localization resources and application EN/ES (73 matching keys).
Do not manually trim runtime DLLs or WinUI locale folders.

Disable Deployment Manager auto-initialization: the source uses neither push
nor app notifications and has no Main/Singleton package requirement. Preserve
AppLifecycle and self-contained runtime initialization.

Normal installer builds no longer enable the geometry probe. Both its helper
functions and recording body are compiled only with VELOCITYCOPY_GEOMETRY_PROBE.
The workflow_dispatch geometry_probe input allows explicit diagnostic builds.
Unhandled-exception diagnostics remain available.

Filter build-only files recursively during staging and publish a JSON byte/file
inventory per architecture. Fail staging if unused SDK entry DLLs remain.

Upgrades retire 52 exact root filenames from the removed component packages,
including their metadata and ML runtime. Each deletion is guarded at installer
compile time: a filename present in the new payload is never retired. There
are no wildcard or directory deletions in this cleanup. Locked files may be
scheduled for deletion at reboot. Existing shell DLL retirement is unchanged.
The Windows upgrade smoke seeds an obsolete AI DLL and an unrelated user file,
checks retirement/preservation, then validates every retired name against the
new payload. Fresh/upgrade launch, PRI EN/ES, shortcut/icon, two-profile startup,
two locked shell generations, signing fixtures and uninstall checks remain.

## Official references

- https://www.nuget.org/packages/Microsoft.WindowsAppSDK/2.4.0
- https://www.nuget.org/packages/Microsoft.WindowsAppSDK.WinUI/2.3.6
- https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/self-contained-deploy/deploy-self-contained-apps
- https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/project-properties
- Microsoft NuGet archives: AI 2.4.4, ML 2.1.74, MachineLearning 2.1.74,
  Widgets 2.0.5 and Search 2.4.4 (retired root filenames).

## Validation scope

Local checks: project XML, EN/ES key parity, selected native source architecture
contracts, NSIS compilation with a disposable payload, git diff --check.
Windows CI and Windows Package must validate the exact PR head before integration.
Measure actual staged bytes in the inventory; do not infer installer reduction
from NuGet download sizes. Interactive hover, visual DPI/accessibility and ARM64
hardware execution still require a Windows desktop/device check.

No transfer engine or UI layout changes and no release/tag publication.
