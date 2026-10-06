# Verified native Beauty transfer

UE 5.8.3 captures FinalColorLDR through a transient SceneCaptureComponent2D
following the selected perspective editor viewport or first PIE/Game player view.
The texture is 1280x720 PF_R8G8B8A8, at most 30 captures/second, opaque Beauty.
Capture pauses when no receiver holds a fresh lease. The separate camera channel
and its v1 layout remain compatible with milestone zero.

## Transport decision

The installed Texture Share SDK was found at Engine/Extras/VirtualProduction/
TextureShare/TextureShareSDK and its samples were examined. Its frame barriers
are less suited to an independent host requesting the latest image. This spike
uses the engine's public ID3D12DynamicRHI interface and Windows DX12 sharing
instead. No private Unreal RHI headers or third-party sharing library are used.

The producer creates three shared textures and a shared completion fence on the
Unreal adapter. Names include producer PID and a session ID. The GPU channel
`Local\\UnrealAELink.Beauty.v1` holds dimensions, format, adapter LUID, names,
leases and three slot records including the captured camera snapshot. Only this
small control block is CPU shared memory; rendered pixels cross processes as
GPU resources. The native receiver opens the same adapter and shared resources,
copies a completed shared image into its private GPU texture, then reads back
locally for pixel diagnostics and eventual Adobe CPU effect output. No disk,
TCP, WebSocket or JSON frame transport exists. Optional BMP export is diagnostic
output after receipt.

Slots move Free -> Writing -> Ready -> Reading -> Ready under a short named
mutex. The producer cannot reuse an unfinished write or a reader's pinned slot.
Ready fence values are GPU-signalled after the UE copy, via an executing RHI
command-list lambda. The receiver observes completed values before submitting
work; it never queues a wait on a remote process. It takes the newest completed
sequence and drops intermediate frames. Its local completion fence protects the
readback and slot acknowledgement. Partial/time-out copies remain pending rather
than reusing an in-flight allocator. Cancelled or mutex-raced publication is
retired after its fence, dropping that frame safely.

A two-second expired reader lease with a pinned slot requires new resources.
A newly connected PID also cannot inherit a departed reader's slot. Unreal
retires its own writes before recreating the resource generation; old resources
remain referenced by any old consumer. Shutdown submits and drains Unreal's GPU
work before closing its resources. A pathological native GPU hang has a bounded
100ms close wait, then retains that session's native GPU objects until process
exit to prevent use-after-free. Repeated device-hang reconnects are not intended
for normal use and need further hardening before production.

## Actual verification

- Native build: MSVC 19.44 / Windows SDK 10.0.22621, warnings treated as errors for
  shared native code and ReceiverTest.
- WindowsIPC and WindowsGPU CTest both pass when run by themselves. GPU testing
  checks every received pixel in a separate process, channel order, row pitch,
  opaque alpha, camera/frame pairing, exclusive ownership, live-reader slot
  protection, expired/new reader generation renewal and stopped producer.
- Unreal module compiled successfully against installed UE 5.8.3.
- `TestBeautyTransfer.cmd` passed by itself: 218 received frames before the camera
  motion assertion succeeded, with changing non-black pixels; Unreal automation
  `UnrealAELink.Beauty.FrameTransfer` reported Success. The received BMP was
  visually inspected and shows the illuminated cube.
- The initial runtime attempt hit a missing RHI execution context for the manual
  fence. Moving the signal into the executing RHI lambda fixed it. One later
  competing native/Unreal test run failed because both requested the single
  receiver lease; the final independent runs passed. Tests must run sequentially.

Logs: artifacts/beauty-build.log, native-gpu-build.log, beauty-acceptance.log,
editor-beauty.log, receiver-beauty.log. The first assertion log is preserved as
editor-beauty-first-failure.log. No untested driver-debug, GPU-hang, visual-window,
or long-duration stability claim is made.

## Limits

Windows x64, DX12, one adapter and one consumer process in the same Windows logon
session. Metadata receiver and GPU receiver may run together because they use
separate leases. Multiple GPU consumers cannot. GPU source is fixed Beauty;
alpha, depth and other passes are not implemented. SceneCapture matches camera
transform/FOV at fixed 16:9; custom viewport post-process/projection settings are
not cloned. Output is display-referred 8-bit LDR with opaque alpha. No timeline,
camera control, deterministic frame stepping or render-farm protocol exists.
