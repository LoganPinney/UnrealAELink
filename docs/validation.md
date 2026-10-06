# Actual validation — October 5, 2026

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
