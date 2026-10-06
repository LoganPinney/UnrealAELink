# UnrealAELink

Windows-only milestone zero: **Unreal camera -> named shared memory -> native
ReceiverTest console**. Target: the locally detected Unreal Engine 5.8.3.

No After Effects integration or graphics transfer is implemented.

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
- `AfterEffectsPlugin/UnrealAELink/`: reservation and missing SDK checklist.
- `docs/`: [architecture](docs/architecture.md), [protocol](docs/protocol.md),
  [build](docs/build.md), and [actual validation](docs/validation.md).

## Limitations

One producer and one receiver process, in the same Windows logon session. Latest
sample only; no frame queue or delivery guarantee. Perspective editor camera or
first PIE/Game player view only. World time is not a timeline frame. View transform
uses unit scale. FOV metadata is not a full projection matrix. Two-second leases
can report a stalled editor as disconnected. Tested engine versions and actual
runtime results are recorded in validation.md, without implying AE/GPU support.

The next proposed milestone is rendered Beauty frame transfer to a native
receiver, after accepting this milestone and explicitly authorizing that work.
