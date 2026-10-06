# UnrealAELink

Windows-only prototype: **Unreal camera metadata and 1280x720 Beauty -> native
receiver**, verified on the locally detected Unreal Engine 5.8.3. A native Adobe
effect is also built against SDK 26.5; Adobe host testing awaits administrator
installation of its `.aex`. Camera metadata
uses named shared memory; rendered pixels use three named DX12 shared textures.

## Quick start

From PowerShell or Command Prompt, at this repository root:

```powershell
.\Tools\BuildNative.cmd
.\Tools\BuildUnreal.cmd
.\Tools\OpenTestProject.cmd
```

In another terminal:

```powershell
.\build\Tools\ReceiverTest\Release\ReceiverTest.exe
```

Move/rotate the perspective Unreal level viewport; watch position/rotation/FOV.
For rendered images, launch the editor using DX12 and run:

```powershell
.\build\Tools\ReceiverTest\Release\ReceiverTest.exe --gpu
```

Close other editors/receivers before the isolated acceptance test:

```powershell
.\Tools\TestBeautyTransfer.cmd
```

This creates transient illuminated geometry, moves the camera, checks received
pixels and writes a diagnostic image to `artifacts/beauty.bmp` after GPU receipt.
The plugin starts automatically. Output Log commands: `UnrealAELink.Start`,
`UnrealAELink.Stop`, `UnrealAELink.Status`. Read [build and testing](docs/build.md)
for prerequisites, detailed acceptance steps and the automated camera test.

## Files

- `UnrealPlugin/UnrealAELink/`: Unreal module, camera selection, service lifecycle,
  and editor camera automation test.
- `Shared/include/UnrealAELink/`, `Shared/src/`: protocol and Windows IPC, used by
  both sides, with no Unreal/Adobe dependency in the native build.
- `Tools/ReceiverTest/`: native Windows x64 C++ receiver.
- `Tools/*.cmd`, `Tools/*.ps1`: discovery, builds, isolated editor launch and
  acceptance run. The launchers select PowerShell 7 and set execution policy
  only for that invocation.
- `Tests/`: multi-process IPC tests and isolated Unreal test project.
- `AfterEffectsPlugin/UnrealAELink/`: native Adobe effect, asynchronous receiver,
  PiPL resource and `.aex`; see its [setup guide](AfterEffectsPlugin/UnrealAELink/README.md).
- `docs/`: [architecture](docs/architecture.md), [protocol](docs/protocol.md),
  [build](docs/build.md), and [actual validation](docs/validation.md).

## Limitations

One producer and one receiver process, in the same Windows logon session. Latest
sample only; no frame queue or delivery guarantee. Perspective editor camera or
first PIE/Game player view only. World time is not a timeline frame. View transform
uses unit scale. FOV metadata is not a full projection matrix. Two-second leases
can report a stalled editor as disconnected. Tested engine versions and actual
runtime results and remaining gaps are recorded in the validation documents.

See [Beauty transport](docs/beauty.md) for the GPU contract, validation and limits.
See [Adobe setup and validation](AfterEffectsPlugin/UnrealAELink/README.md) for the
built effect and the pending host test.
