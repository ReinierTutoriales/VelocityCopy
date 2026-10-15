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
