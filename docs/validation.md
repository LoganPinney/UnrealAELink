# Actual validation — October 6, 2026

## v0.2 deterministic Adobe host acceptance: passed

`Tools/TestDeterministic.cmd` completed successfully against actual Adobe 26.5
and Unreal 5.8.3 (`artifacts/v02-deterministic-host-final.log`). It launched a
real transient Level Sequence with a 30/1 display rate, 24000/1 tick rate and a
linear cube transform, applied the native effect in AE, and rendered 41 TIFFs.
`artifacts/deterministic/UnrealAELinkDeterministicDemo.aep` is the saved project.

Acceptance requires more than file count. The validator correlates every native
AE render with a successful response, matching raw rational time, Unreal direct
evaluation and actor transform, capture, completed GPU publication, resource
session and Beauty sequence. Every exported TIFF's full RGB FNV hash matches
an AE-painted native frame at its requested time. Independent pixel centroids
match the cube's projected location. All 31 sequential frames advance, and each
random-access frame agrees with the corresponding sequential frame within one
pixel. Logs, TIFFs, decoded PNGs, `evidence.csv` and `request-identities.csv` are
in `artifacts/deterministic/`.

Observed actual AE callback times and evaluated states:

| Probe | AE current_time/time_scale | UE frame at 30/1 | Cube Y (cm) | Pixel X |
|---|---|---|---:|---:|
| Known left | 0/30720 | 0 | -240 | 268.455 |
| Known center | 30720/30720 | 30 | 0 | 639.479 |
| Known right | 61440/30720 | 60 | 240 | 1010.527 |
| Random 20 | 20480/30720 | 20 | -80 | 510.773 |
| Random 3 | 3072/30720 | 3 | -216 | 305.135 |
| Random 17 | 17408/30720 | 17 | -104 | 474.336 |
| Random 0 | 0/30720 | 0 | -240 | 268.455 |
| Random 29 | 29696/30720 | 29 | -8 | 626.490 |
| Half frame | 512/30720 | 0+0.5 | -236 | 274.394 |
| NTSC probe | 13600/23976 | 17+0.017017016 | -103.863864 | 474.330 |

AE quantizes the NTSC composition to its own rational timebase. The transmitted
pair is the actual callback time, not a fabricated 17017/30000 value. A TIFF
suffix may round down to 00016 for this probe; the callback rational and full
RGB hash prove the rendered time. At time zero the native/exported RGB hash is
11933886883401695913 across repeated sequential and random requests.

The user-reported failure exposed independent host sequence-data copies sharing
one `effect_ref`. Registering by that reference canceled the render copy when
AE reset another copy. Registrations now belong to each host sequence-data
handle, and an SDK callback regression test verifies setup/resetup/teardown of
one copy cannot invalidate the other. The host script also now resolves Mode by
name (button parameters create scripting-index gaps), avoids unsupported time
remap setters on solids, disables MFR explicitly, and uses `comp.frameDuration`
for one-frame durations. Historical failure logs are retained in artifacts.

The final v0.2 Unreal and Adobe builds succeeded (`artifacts/v02-unreal-build-final.log`, `artifacts/v02-adobe-build-final.log`). The installed .aex hash matches the final build (`artifacts/v02-install-verified.log`); deterministic acceptance was repeated with that installed binary.

Preservation and complementary checks:

- Original native baseline passed before edits (`artifacts/v01-baseline.log`).
- All five native groups passed (`artifacts/v02-native-tests-final.log`):
  WindowsIPC, WindowsGPU, WindowsAdobeFrameClient, WindowsRequests and
  WindowsDeterministicGPU. Actual cross-process IPC/DX12 tests cover rational
  conversion, overflow, wrong identity rejection, one outstanding request,
  completed-image ordering, 15-second timeout, abort, duplicate coalescing,
  immutable images, random access and rejection of unrelated/live frames.
- Official SDK-world tests pass for ARGB8/16/32 conversion, layout, downsampling,
  disconnected black, suite balancing, cancellation and sequence-copy lifetime.
- Actual Unreal/native acceptance separately passed known, sequential, random,
  half-frame and noninteger-rate requests (`artifacts/sequencer-native-final.log`).
- The existing actual Adobe Live regression passed after the lifecycle fix
  (`artifacts/v02-live-host-final.log`): received Beauty at 8/16/32 bpc,
  freeze/resume, half resolution, disconnect and project saving.
- Existing user edits to the test configuration, project/plugin descriptors and
  Content asset were preserved. Only the plugin version fields advance to 0.2.0.

The main-thread idle-hook APIs were investigated after acceptance; the findings
and proposed 15–30 Hz throttle are in [deterministic.md](deterministic.md).
Automatic live redraw remains unimplemented and unverified. Camera/control
synchronization and additional passes were not added.

## Native build and runtime: passed

Built Windows x64 Release with MSVC 19.44.35229 and Windows SDK 10.0.22621.0.
The shared library and ReceiverTest compile with `/W4 /WX /permissive-`.
`Tools/BuildNative.cmd` completed successfully. CTest's WindowsIPC suite passed
in 8.75 seconds after the Unreal Windows-header compatibility fix.

The suite uses actual Windows mappings/mutexes and separate native child
processes, not mocked IPC. It checked:

- Duplicate producer rejection in both the same thread and a second process.
- No uninitialized camera frame, valid decoding and stable repeated sample ID.
- Unknown protocol version and incorrect block size rejection.
- Receiver heartbeat and second receiver process rejection.
- Stale producer rejection and resumed heartbeat.
- Graceful receiver disconnect.
- 100 changing, coherent samples received by a separate process.
- Forced receiver termination and lease expiry without producer failure.
- Graceful producer stop while receiver retains the mapping.
- Producer restart and new session identity.
- Forced producer termination, stale rejection and ownership reclamation.
- Consumer following a replacement producer.
- Forced termination while owning the data mutex: abandoned sample rejection
  and repaired publication by the live producer.

## Unreal build and editor acceptance: passed

The follow-up inspection confirmed .NET Framework 4.8 SDK is now installed.
`Tools/BuildUnreal.cmd` successfully built the isolated Development Win64 editor
target and plugin with Unreal 5.8.3, MSVC 14.44.35229, and Windows SDK 22621.
The target uses `BuildSettingsVersion.V7` and UE 5.8 include order. Compilation
found and fixed Unreal's hidden `FALSE` macro and missing braces around a logging
macro. No compiler warnings remain in the successful incremental build.

Actual plugin output:
`Tests/UnrealAELinkTest/Binaries/Win64/UnrealEditor-UnrealAELink.dll`.
UE 5.8 places this external plugin in its host project's binary directory.
The successful build diagnostics were copied to `artifacts/unreal-build.log`.

`Tools/TestEditorCamera.cmd -TimeoutSeconds 300` completed with exit 0 from both
the editor and the external receiver. The editor used its normal D3D renderer
with offscreen output, rather than NullRHI. It changed a real perspective level
viewport camera for eight seconds, then restored its original view. The receiver
reported **92 distinct samples and camera changed=1**. Unreal automation reported:

```text
Test Completed. Result={Success} Name={EditorCamera} Path={UnrealAELink.Metadata.EditorCamera}
```

The external receiver's changed sample included:

```text
Frame: 1049  Sequence: 1050
Time: 10.1696
Camera: Editor viewport
Position: X=128.4560 Y=-39.1544 Z=185.0000
Rotation: P=-10.0000 Y=42.4228 R=0.0000
FOV: 50.0846
Aspect: 1.6273
PASS: received 92 distinct samples; camera changed=1
```

The earlier view was X=-348.2961, Y=78.8395, Z=228.8923, yaw=2, FOV=90.
Editor logs also show plugin startup, bridge startup, receiver connection and
disconnection, and subsequent camera movement. Evidence is saved in
`artifacts/editor-camera.log` and `artifacts/receiver-camera.log`.

The initial NullRHI run failed: it connected but did not publish valid camera
frames because the viewport did not have usable dimensions. Failure logs were
retained with `nullrhi-failed` filenames. The corrected acceptance script uses
the normal renderer, fails promptly when Unreal exits without enough samples,
and preserves output on failure. No GPU frame sharing is involved.

The automated editor camera path is verified. The user subsequently confirmed
the manual test works and supplied a screenshot of UnrealAELinkTest alongside
ReceiverTest. It shows the editor viewport source, advancing frame/sequence
numbers, and camera position, rotation, FOV, and aspect data in the native console
(including frame 4280 / sequence 4281). Milestone zero is accepted in manual use.

PIE/Game camera selection and packaged game targets have not been separately
tested. Those acceptance results were limited to milestone-zero metadata.

## Environment fixes

The build launch environment contained both `PATH` and `Path`. MSBuild initially
failed with `Item has already been added` while identifying the compiler.
`Tools/InvokeNative.ps1` removes case-insensitive duplicates only in child-process
environments. A fresh CMake configuration then correctly identified the compiler.
No machine/user environment variables were changed.

The user also encountered Windows PowerShell's restricted script policy.
New `.cmd` launchers locate PowerShell 7 and use `-ExecutionPolicy Bypass` for
that child invocation only. The BuildNative launcher was successfully executed
from a Windows PowerShell 5.1 process explicitly set to `Restricted`, and its
build/tests passed. The Unreal build and camera acceptance also ran through the
launchers. No persistent execution policy setting was changed.

No existing Unreal project or Adobe installation file was modified during
milestone zero. Initial scoped discovery did not find Adobe.

## Authorized follow-on work

The user authorized GPU frame transfer followed by Adobe components. The GPU
spike compiled and passed actual Unreal/native receiver acceptance; see
[Beauty validation](beauty.md). Its milestone commit is c27975b.

The user supplied the installed After Effects path (file version 26.5) and official
SDK 26.5 archive. A native .aex built successfully; three native CTest groups and
the Adobe SDK pixel-world renderer tests passed. Details are in the
[Adobe setup guide](../AfterEffectsPlugin/UnrealAELink/README.md).

Initial installation into Program Files was denied and administrator elevation
was cancelled. The user subsequently installed the plugin; diagnostic/fixed
builds were updated with Windows administrator approval on October 6.

Adobe 26.5 `-noui` startup reproduced exit 0xC0000409 with a minimal script that
never applied the effect. Normal startup succeeded. The acceptance harness now
uses normal startup with its window hidden, and synchronous render-queue TIFF
output instead of the original ineffective `saveFrameToPng` calls. Host testing
also exposed a missing `PF_OutFlag2_I_MIX_GUID_DEPENDENCIES` declaration; matching
runtime/PiPL flags now allow GUID mixing and SmartFX rendering.

Host-rendered Beauty images at ARGB32, ARGB64 and ARGB128, identical frozen-frame
pixels, later resumed frames, half resolution (640x360), black disconnect output
and a saved demo project have been verified. Native startup/sequence/teardown
callbacks are recorded in `artifacts/ae-native.log`; TIFFs and PNG previews are
under `artifacts/`. GUI, project reopen and longer sessions remain unverified.
The user's existing DefaultEngine.ini changes were preserved and excluded from
the follow-on commits.
