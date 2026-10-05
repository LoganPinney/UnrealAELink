# Actual validation — October 5, 2026

## Native build and runtime: passed

Built Windows x64 Release with MSVC 19.44.35229 and Windows SDK 10.0.22621.0.
The shared library and ReceiverTest compile with `/W4 /WX /permissive-`.
`Tools/BuildNative.ps1` completed successfully. CTest's WindowsIPC suite passed
in 8.67 seconds after adding version/size rejection tests.

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

## Unreal build and editor acceptance: blocked / not verified

Unreal Engine 5.8.3 was detected and its build tooling was invoked. The first
attempt revealed target-settings mismatch; the isolated test target now uses the
engine-required `BuildSettingsVersion.V7` and UE 5.8 include order.

The current build fails during module rule evaluation, before C++ compilation:

```text
Unable to instantiate module 'SwarmInterface': Could not find NetFxSDK install dir;
Install a version of .NET Framework SDK at 4.6.0 or higher.
```

This dependency belongs to Unreal's editor, not the bridge. .NET Framework 4.8
SDK/targeting-pack installation was attempted. The installer returned 1602;
its log reports that the user may have declined the Windows administrator
prompt. A follow-up installation prompt was opened and approval requested.

**The Unreal DLL has not yet compiled successfully. The editor camera acceptance
test has not run. Milestone zero is therefore not yet accepted.**

Once the Microsoft SDK is installed, run:

```powershell
.\Tools\BuildUnreal.ps1
.\Tools\TestEditorCamera.ps1
```

Fix any compiler/runtime diagnostics then update this record with actual log
evidence. The included acceptance test uses a real editor perspective viewport
with NullRHI, moves/restores the camera, and requires a separate ReceiverTest
process to receive changing metadata. It has not been claimed as passing.

## Environment fixes

The build launch environment contained both `PATH` and `Path`. MSBuild initially
failed with `Item has already been added` while identifying the compiler.
`Tools/InvokeNative.ps1` removes case-insensitive duplicates only in child-process
environments. A fresh CMake configuration then correctly identified the compiler.
No machine/user environment variables were changed.

No existing Unreal project or Adobe installation file was modified. After Effects
and its SDK were not found in the scoped inspected locations. No work beyond
metadata milestone zero was implemented.
