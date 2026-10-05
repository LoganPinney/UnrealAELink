# Build and test on Windows

Run commands in **PowerShell 7**, from the repository root. Scripts use the
PowerShell 7/.NET process argument API and normalize only child environments.

## Discovered toolchain

Inspected on October 5, 2026:

- Unreal Engine 5.8.3, changelist 58210709, discovered through Epic's install
  manifest and verified against `Engine/Build/Build.version`.
- Engine root: `C:\Program Files\Epic Games\UE_5.8`.
- C++ Build Tools were absent. Installed Microsoft Visual Studio Build Tools
  2022 17.14.41, MSVC compiler 19.44.35229 (toolset directory 14.44.35207),
  Windows SDK 10.0.22621.0 and CMake 3.31.6.
- Unreal's SwarmInterface also requires .NET Framework SDK 4.6+; install the
  .NET Framework 4.8 SDK and targeting pack. Both automated attempts to add
  this component were cancelled at the Windows administrator prompt; it is
  still missing, and currently blocks the Unreal build.
- No AE application/SDK was found in the scoped locations checked. See
  `AfterEffectsPlugin/UnrealAELink/README.md` for the missing development inputs.

Only Microsoft prerequisite installation changes system software. Plugin and
test code stay in this repository. Builds/editor runs also create their normal
per-user Unreal caches and logs. No existing Unreal project or Adobe install
file is edited.

## Prerequisites on another machine

Use Microsoft's signed Build Tools installer with these components:

```text
Microsoft.VisualStudio.Workload.VCTools
Microsoft.VisualStudio.Component.VC.Tools.x86.x64
Microsoft.VisualStudio.Component.Windows11SDK.22621
Microsoft.VisualStudio.Component.VC.CMake.Project
Microsoft.Net.Component.4.8.SDK
Microsoft.Net.Component.4.8.TargetingPack
```

Component IDs and installer options are documented by
[Microsoft](https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=visualstudio).
The installed engine's `Engine/Config/Windows/Windows_SDK.json` contains its
toolchain constraints; the numeric compiler version matters more than the
toolset directory name. Do not force a compiler version banned by the engine.

To clear this machine's current blocker: open **Visual Studio Installer**, find
**Build Tools 2022**, choose **Modify -> Individual components**, select
**.NET Framework 4.8 SDK** and **.NET Framework 4.8 targeting pack**, apply the
change and approve Windows' administrator prompt. Then rerun BuildUnreal.ps1.

## Build both projects

Close Unreal editors before the native IPC tests; they use the same v1 endpoint.

```powershell
.\Tools\BuildNative.ps1
.\Tools\BuildUnreal.ps1
```

The first command builds ReceiverTest and runs the Windows IPC suite. The second
builds `UnrealAELinkTestEditor` and the plugin using the installed engine's bundled
.NET/UnrealBuildTool, Development Win64. It disables UBA for a local compiler
build. No full engine rebuild is required. Scripts fail visibly on nonzero exit.
Use `BuildNative.ps1 -Fresh` if an earlier failed CMake compiler probe polluted
the cache. Optional `BuildUnreal.ps1 -EngineRoot 'D:\CustomUE'` selects an explicit
UE 5.8 root. Automatic selection prefers a registered 5.8 engine.

Outputs:

```text
build/Tools/ReceiverTest/Release/ReceiverTest.exe
UnrealPlugin/UnrealAELink/Binaries/Win64/UnrealEditor-UnrealAELink.dll
Tests/UnrealAELinkTest/Binaries/Win64/UnrealEditor-UnrealAELinkTest.dll
```

## First acceptance test: move the viewport

1. Run `.\Tools\OpenTestProject.ps1`. This opens the isolated test project with
   UnrealAELink already enabled. For a newly enabled plugin in another project,
   restart Unreal after enabling it.
2. Select the perspective level viewport. Open **Tools -> Debug -> Output Log**.
   Look for `LogUnrealAELink: Plugin startup` and `Bridge startup`.
3. In a second PowerShell terminal at the repository root, run:

   ```powershell
   .\build\Tools\ReceiverTest\Release\ReceiverTest.exe
   ```

4. The receiver prints `Connected to UnrealAELink` and camera data. Hold the
   right mouse button in the level viewport and move with W/A/S/D, or rotate with
   the mouse. Position/rotation should update. Change the perspective viewport
   field of view to check FOV. The viewport size/aspect changes with its dimensions.
5. Run `UnrealAELink.Stop` in Output Log's command field. The receiver should
   disconnect; `UnrealAELink.Start` reconnects it. `UnrealAELink.Status` prints
   service state. Close the receiver; Unreal reports its lease expiry within
   about two seconds. Close Unreal; the receiver waits safely for another bridge.
6. Ctrl+C stops the receiver. During editing, its world time may be static;
   engine frame number and publication sequence still advance.

## Automated acceptance

Close other Unreal editors and receivers, then run:

```powershell
.\Tools\TestEditorCamera.ps1
```

This starts a hidden, unattended Unreal editor with NullRHI (metadata only),
moves the actual editor camera for eight seconds, and starts a separate native
receiver. Passing requires successful Unreal automation plus at least 20 distinct
received samples and changing camera values. It restores the camera before exit.
First startup can require extra time; `-TimeoutSeconds 300` extends the bound.
Logs are saved in `artifacts/`. Review `docs/validation.md` for actual results.

## Register the plugin in your own project later

Keep `Shared/` and `UnrealPlugin/` together in this repository. With that editor
closed, add the absolute `UnrealPlugin` directory to its `.uproject` file's
`AdditionalPluginDirectories`, and enable the `UnrealAELink` plugin. For example:

```json
"AdditionalPluginDirectories": ["C:/path/to/UnrealAELink/UnrealPlugin"],
"Plugins": [{ "Name": "UnrealAELink", "Enabled": true }]
```

Merge these entries with existing ones. Do not replace other plugin settings.
The plugin must be rebuilt with that project's exact engine; simply copying only
the plugin folder loses its shared source dependency. The supplied test project
is the recommended first test and requires no edits to your existing work.
