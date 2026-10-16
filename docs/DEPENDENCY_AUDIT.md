# Dependency audit — 2026-10-15

Stable versions were checked against NuGet's official v3 flat-container indexes
and the dependency groups of each selected `.nuspec`. Prerelease versions were
excluded. Package numbers belong to individual components, not to a single
Windows App SDK version that can be applied to every component.

| Package | Selected stable version | Result |
| --- | --- | --- |
| Microsoft.WindowsAppSDK.WinUI | 2.3.9 | Already latest stable |
| Microsoft.WindowsAppSDK.Foundation | 2.3.12 | Already latest stable |
| Microsoft.WindowsAppSDK.InteractiveExperiences | 2.1.9 | Already latest stable |
| Microsoft.WindowsAppSDK.DWrite | 2.1.0 | Already latest stable |
| Microsoft.WindowsAppSDK.Runtime | 2.5.1 | Already latest stable |
| Microsoft.Windows.CppWinRT | 3.0.260818.1 | Already latest stable |
| Microsoft.WindowsAppSDK.Base | 2.0.4 | Latest stable, transitively selected |
| Microsoft.Web.WebView2 | 1.0.4258.31 | Explicitly override WinUI's older 1.0.3719.77 dependency minimum |

Sources: [NuGet service index](https://api.nuget.org/v3/index.json),
[WinUI](https://www.nuget.org/packages/Microsoft.WindowsAppSDK.WinUI/2.3.9),
[Foundation](https://www.nuget.org/packages/Microsoft.WindowsAppSDK.Foundation/2.3.12),
[WebView2](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4258.31).
The WebView2 SDK update does not add a browser surface or bundle a fixed browser
runtime, and does not establish a transfer-performance improvement.

CI/Package action updates retain the existing inputs, triggers and gates:

| Action | Previous reference | Selected stable release and immutable commit |
| --- | --- | --- |
| actions/checkout | v6 | [v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1), `3d3c42e5aac5ba805825da76410c181273ba90b1` |
| actions/upload-artifact | v6 | [v7.0.2](https://github.com/actions/upload-artifact/releases/tag/v7.0.2), `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |
| actions/cache | v5 | [v6.1.0](https://github.com/actions/cache/releases/tag/v6.1.0), `55cc8345863c7cc4c66a329aec7e433d2d1c52a9` |
| microsoft/setup-msbuild | v3 | Already [latest stable](https://github.com/microsoft/setup-msbuild/releases/tag/v3) |

The upgraded actions run on Node 24, already required by the previous action
generation and supported by the hosted Windows runners. Checkout v7's stricter
fork checks do not require an unsafe opt-in for the ordinary `pull_request`
events used here. Artifact uploads keep ZIP archiving enabled so the release
workflow can validate and reuse their exact installer bytes.

[NSIS 3.13](https://nsis.sourceforge.io/Download) is already the latest stable
installer compiler; its existing SHA-256 pin remains. CMake/MSVC/Windows SDK
come from the hosted runner image; the project still targets Windows 11 build
22000 and does not download another toolchain. Exact build/runtime validation is
provided by CI and Package on the implementation commit, not by catalog checks.

## Payload use and removal policy

The x64 Package run [38836370053](https://github.com/ReinierTutoriales/VelocityCopy/actions/runs/38836370053)
reported 62.6 MiB uncompressed, 244 files. Its largest native files included
`Microsoft.ui.xaml.dll` (15,324,472 bytes), `Microsoft.UI.Xaml.Controls.dll`
(6,943,032), `WinUIEdit.dll` (3,419,448), `DWriteCore.dll` (3,252,576),
and `VelocityCopy.WinUI.exe` (1,971,200). This is installed payload size, not
installer size, process memory or startup cost.

Keep native C++/WinUI and self-contained deployment. The component package
references already avoid AI/ML, Search and Widgets; Package rejects those
unselected components and recursively removes build-only products. Keep WinUI,
Foundation, input/composition, text rendering, resource loading and activation
support. An absent direct source call is not evidence that a WinUI dependency
can be deleted. WebView2 is an SDK dependency override, not a bundled browser;
no application WebView surface was found in the source audit.

Package now writes `payload-dependencies-<arch>.json` beside the existing full
size inventory. It records every staged EXE/DLL's size, file version and combined
static/delay-load imports using MSVC DUMPBIN, without loading the image. Exact
totals separate the three application binaries from all other payload; the
latter includes application resources and must not be labeled entirely runtime.
Both x64 and ARM64 images are inspected on the runner. This is not ARM64 execution.

No further runtime file is excluded by this change. Import absence cannot prove
non-use by WinRT activation, `LoadLibrary`, resource lookup or optional UI paths.
A removal needs package dependency/activation evidence and startup, transfer,
clipboard/Explorer, dialog, localization, DPI and upgrade validation on the
reduced payload. The existing launch smoke alone cannot establish that safety.

Official references:
[self-contained deployment](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/self-contained-deploy/deploy-self-contained-apps),
[DUMPBIN /IMPORTS](https://learn.microsoft.com/en-us/cpp/build/reference/imports-dumpbin).

## Completed component disposition

The selected distribution remains native C++/WinUI, componentized and
self-contained. No additional runtime exclusion is approved by this audit.
This is a deliberate disposition of the current payload, not a claim that
every supplied SDK file is exercised by VelocityCopy or an absolute minimum.

| Group | Evidence and decision |
| --- | --- |
| WinUI/XAML, Controls, Internal, WinUIEdit, Phone, Designer | Retain the SDK component deployment. `MainWindow.xaml`, `DecisionSurface.cpp` and `MainWindow.About.cpp` use native controls and XAML activation; XAML imports Internal and FrameworkUdk. WinUIEdit/Phone/Designer have no inbound PE imports in this snapshot, which does not establish that optional framework paths cannot load them. Do not infer removal safety from their names. |
| Foundation/runtime/bootstrap and metadata | Retain SDK initialization, resource and activation support for the unpackaged process. Bootstrap imports WindowsAppRuntime. WinRT activation explains why an application can use a DLL without a PE import. Generated initialization is part of the SDK build, not just application source. |
| InteractiveExperiences/input/composition/windowing | Retain. `MainWindow.xaml.cpp` uses DispatcherQueue/windowing, and `UiTokens.h` applies DesktopAcrylicBackdrop. The report links CoreMessagingXP, marshal, FrameworkUdk, Windowing.Core and composition support through actual imports. |
| DWrite/text rendering | Retain the selected DWrite component. Text rendering is part of the native UI stack; the lack of an application DWriteCore import does not establish safe exclusion. |
| MRT/PRI, XBF and language satellites | Retain. `Localization.cpp` constructs ResourceManager, and Package validates both application languages. Keep the framework's other language satellites: native control localization may follow the user's language even when application strings fall back to English/Spanish. Do not remove languages solely from the application's two RESW folders. |
| WebView2 SDK/Core | Retain the pinned dependency required by the WinUI NuGet graph; no browser runtime or application WebView is added. SDK provenance and a missing source call do not justify hand-deleting its runtime projection. |
| RestartAgent / notification proxy | Retain the Foundation-supplied deployment; no application push notification API is used (tray messages use Shell APIs). Absence of application usage does not prove these SDK support files are independently removable. |
| CppWinRT | Build tooling/header projection, not another application framework. Track restored dependencies separately from files actually distributed. |
| AI/ML, Search, Widgets | Continue excluding by selecting only needed SDK components; recursively reject their known binaries if they reappear anywhere in the payload. |
| PDB/ILK/LIB/EXP/IOBJ/IPDB | Build products already removed recursively; keep that exclusion. |

### Size baseline and repeatability

At commit `eeaeeffa0f6713187e6e8866df6d25b622bc2165`, Package run
[38837636912](https://github.com/ReinierTutoriales/VelocityCopy/actions/runs/38837636912)
measured x64 **65,599,421 bytes** and ARM64 **70,622,949 bytes**, each 244 files
and 34 EXE/DLL modules. The three application binaries account for 2,133,504
and 2,192,896 bytes respectively; the remainder includes runtime AND resources.
The x64 inventory breaks down as follows:

| Type | Files | Bytes |
| --- | ---: | ---: |
| DLL | 31 | 52,543,016 |
| EXE (including SDK helper) | 3 | 2,109,240 |
| MUI | 172 | 3,530,328 |
| PRI | 4 | 4,419,912 |
| WinMD | 27 | 2,509,872 |
| XBF | 3 | 22,961 |
| ICO/PNG/HTML | 4 | 464,092 |

Report schema 2 now records the restored NuGet graph, reverse import edges,
file-type totals, and **byte-identical SHA-256 matches** from each staged file
to restored package files. Multiple exact owners remain visible; generated or
unmatched files are not assigned a guessed owner. Candidate names are indexed
once and source hashes cached. The audit reads files without loading any image
and is never included in the installer.

Future dependency updates must regenerate both architecture reports, inspect
new/unmatched files and package changes, and retain the existing exact-commit
CI/Package gates. Any proposed runtime exclusion is a separate behavioral
change requiring the reduced-payload checks above. This audit makes no runtime
removal, so no reduced-payload performance claim or unexecuted exclusion test
is represented as complete. Startup/RAM benchmarks are not substituted for
dependency safety; no startup/RAM improvement is claimed. Physical device/UI
and ARM64 runtime acceptance remain the release gates in RELEASE_GATES.md.
