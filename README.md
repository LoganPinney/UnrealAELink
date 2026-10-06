# UnrealAELink

## v0.2: AE-driven deterministic Sequencer rendering

v0.1 is asynchronous **latest Beauty**. The new explicit `Mode: Deterministic`
uses AE's exact `current_time/time_scale` to request direct evaluation of one
selected Unreal Level Sequence, then waits for the matching completed GPU image.
`Mode: Live` keeps the existing asynchronous behavior, including freeze/resume.
Camera metadata v1 is unchanged; Beauty control metadata is versioned to v2.
Update both native producer and consumer together; old binaries remain isolated
on Beauty.v1 and cannot interpret the new slot layout.

In the Unreal Output Log, select the sequence:

```text
UnrealAELink.Sequence /Game/Test/MySequence
```

Enable `Connect`, set `Mode` to `Deterministic`, and render normally in AE. For
this prototype the effect layer must start at composition zero, use 100% stretch,
and have Time Remapping disabled. Sequence absolute zero corresponds to AE zero;
times outside its playback range fail. Requests are single-frame serialized;
parallel Multi-Frame Rendering is not advertised. Disable AE Multi-Frame
Rendering for deterministic exports; the test script disables it explicitly.
The timeout is 15 seconds;
an unsuccessful request fails the host render rather than using latest Beauty.

```powershell
.\Tools\BuildNative.cmd
.\Tools\BuildUnreal.cmd
.\Tools\BuildAfterEffects.cmd
# With Adobe closed, from an administrator terminal:
.\Tools\InstallAfterEffects.cmd
# Close other editors/Adobe/GPU receivers, then run:
.\Tools\TestDeterministic.cmd
```

The host acceptance renders known left/center/right states, an actual 31-frame
Render Queue job, then requests frames `20,3,17,0,29`. It validates timeline
identities, evaluated actor state and decoded AE pixel motion, and additionally
tests a half-frame and NTSC time. The actual Adobe 26.5 host test passed on
October 6, 2026: every decoded TIFF's full RGB hash matched the native image
painted for its exact request identity, and cube motion matched the evaluated
timeline. See [timeline details](docs/deterministic.md) and
[actual validation](docs/validation.md).

Windows-only prototype: **Unreal camera metadata and 1280x720 Beauty -> native
receiver**, verified on the locally detected Unreal Engine 5.8.3. A native Adobe
effect is built against SDK 26.5 and installed in After Effects 26.5. Its host
test renders received Beauty at 8/16/32 bpc, freeze/resume and disconnect. Camera metadata
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

One producer and one receiver process, in the same Windows logon session. Camera
metadata and Live Beauty use latest samples; deterministic Beauty uses the exact
request mailbox described above. Perspective editor camera or
first PIE/Game player view only. World time is not a timeline frame. View transform
uses unit scale. FOV metadata is not a full projection matrix. Two-second leases
can report a stalled editor as disconnected. Tested engine versions and actual
runtime results and remaining gaps are recorded in the validation documents.

See [Beauty transport](docs/beauty.md) for the GPU contract, validation and limits.
See [Adobe setup and validation](AfterEffectsPlugin/UnrealAELink/README.md) for the
built effect and the host test.
