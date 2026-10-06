# Milestone zero architecture

```text
UE main-thread module service
  -> PIE/Game first player camera, otherwise active editor perspective viewport
  -> fixed camera metadata snapshot
  -> shared C++ Windows IPC implementation
  -> named 192-byte mapping + data mutex + producer owner mutex
  -> ReceiverTest native x64 process
  -> console output (10 Hz)
```

The Unreal plugin loads at PostEngineInit and starts its service automatically.
Commands `UnrealAELink.Start`, `.Stop`, and `.Status` manage the service manually.
A core ticker captures view metadata each engine tick; module shutdown removes
the ticker, closes the transport, and unregisters commands. No UObject pointers
cross the process boundary. There is no persistent camera actor reference.

PIE/Game camera selection uses the first available player controller's resolved
camera cache (including blends). Editor selection searches level viewports only,
preferring the active perspective viewport and otherwise the first perspective
viewport. Orthographic and asset preview views are excluded. Losing all supported
views publishes a no-camera sample rather than repeating an old valid camera.

`Shared/` depends only on the Windows API and C++ standard library. CMake compiles
it into a static library for native tools. A tiny Unreal compilation unit includes
the same implementation, with Unreal's Windows header wrapper. `Build.cs` tracks
the external shared sources as dependencies. Unreal-only APIs remain inside
`UnrealPlugin/`. Adobe development is described in the current-state section below.

Windows handles and mapped views use RAII. Producer game-thread publication does
not wait for the receiver; a missing or dead peer only changes connection state.
Logs show startup, stop, lease changes and one frame/camera snapshot per second.
See protocol.md for timeouts and crash recovery.

The test host registers the plugin through `AdditionalPluginDirectories` and
does not copy files into an engine installation or another user's project.
Native tests launch separate C++ child processes. The editor acceptance test
moves an actual level editor camera for eight seconds, restores it, and requires
the separate receiver to report changing position/rotation/FOV. Both automated
and manual tests use Unreal's normal renderer, so the viewport has pixel dimensions.

The paragraphs above describe the accepted metadata milestone.

## Current extensions

The subsequently authorized GPU spike is complete; see [Beauty transport](beauty.md).
The engine follows the selected camera through a transient scene capture and
copies into a three-slot DX12 shared-resource ring. A separate CPU control block
contains named resources, fence values, ownership and per-frame camera metadata.

The native Adobe effect is built against SDK 26.5. One background client per
Adobe process provides immutable completed snapshots to render callbacks.
See [Adobe setup and validation](../AfterEffectsPlugin/UnrealAELink/README.md).
Host loading is pending administrator installation. No timeline sync or camera
conversion/control is implemented.
