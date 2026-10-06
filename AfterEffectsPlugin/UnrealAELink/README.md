# Native After Effects prototype

## v0.2 AE-authoritative deterministic rendering

The new `Mode` popup defaults to `Live` and adds `Deterministic`. Existing
Connect/Live/Source controls and their saved parameter identities remain in
place. The Live checkbox continues to freeze/resume asynchronous mode.
Deterministic mode uses the original AE `current_time/time_scale` in a separate
Request.v1 mailbox, waits up to 15 seconds for the matching completed Beauty.v2
image, and fails the host callback on timeout, abort, failed response or identity
mismatch. It never substitutes the worker's Latest image. PreRender owns the
immutable result; SmartRender paints that same image. Identical outstanding
requests share one native job. No Adobe suites run on the worker.

In Unreal, select one asset with `UnrealAELink.Sequence /Game/Test/MySequence`.
In AE, use a layer starting at composition zero, 100% stretch, no Time Remapping;
enable Connect and select Deterministic. AE zero maps to sequence absolute zero.
MFR is intentionally unsupported: `PF_OutFlag2_SUPPORTS_THREADED_RENDERING` is
absent in runtime/PiPL, and all deterministic requests are serialized globally.
Disable AE Multi-Frame Rendering for deterministic exports; the host test uses
`app.setMultiFrameRenderingConfig(false, 100)` explicitly.

After building and installing both updated components, run
`Tools/TestDeterministic.cmd`. The native fixture and actual AE Render Queue
test check known times, 31-frame progression, frames `20,3,17,0,29`, a subframe
and NTSC time. Logs, decoded previews and CSV identities go in
`artifacts/deterministic/`. See [timeline details](../../docs/deterministic.md).
Actual deterministic Adobe host acceptance passed on October 6, 2026, including
full RGB hash matching of each exported image to its exact native render.
The v0.1 host validation below remains historical evidence for Live.

`Binaries/Win64/UnrealAELink.aex` was compiled against the official Windows
After Effects 26.5 SDK supplied by the user. The installed application is also
26.5 at `C:/Program Files/Adobe/Adobe After Effects 2026`. SDK files are external
dependencies and are not redistributed here.

## Implemented components

Native EffectMain/PluginDataEntryFunction2 exports and SDK-generated PiPL;
Connect and Live checkboxes; fixed Beauty source; status refresh and current
frame labels. One background GPU consumer per Adobe process serves immutable
completed frames to effect instances. Frozen instances keep a private snapshot
without holding a shared GPU slot. The worker calls no Adobe or Unreal API;
Live host render callbacks never wait for the remote GPU or engine.

SmartFX pre-render selects one immutable frame and mixes session/sequence into
the GUID; smart-render uses that same snapshot. Legacy render also exists.
The renderer handles Adobe ARGB8/16/32 worlds, padded rows, tile origins,
downsampling, aspect-fit letterboxing, opaque black when disconnected and host
cancellation. Serializable sequence data contains only an instance token;
global/sequence teardown releases subscriptions. Each host sequence-data handle
owns its registration: setup/resetup of another handle sharing the same effect
reference cannot cancel the render copy's subscription. This lifecycle behavior
has an SDK callback regression test and actual Render Queue coverage.

## Build and installation

From the repository root:

```powershell
.\Tools\BuildAfterEffects.cmd
```

The launcher finds the previously configured SDK or the SDK extracted in this
workspace at `../../work/AdobeSDK26_5/AfterEffectsSDK_26.5_win`. It prints the
selected folder before building. For an SDK extracted elsewhere, pass
`-SDKRoot` followed by its actual folder in quotes, or set `AE_SDK_ROOT`.
An explicit path or environment setting takes precedence and must contain
`Examples/Headers/AE_Effect.h`; the downloaded ZIP is not the SDK root.
The build also runs AdobeRenderTests against the SDK worlds.
The .cmd launcher sets script policy only for its child PowerShell 7 invocation.

Close After Effects. In a terminal **running as Administrator**, from the
repository root:

```powershell
.\Tools\InstallAfterEffects.cmd
```

This installs only the built plugin at:

```text
C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\UnrealAELink\UnrealAELink.aex
```

The installer checks hashes and backs up an existing differing prototype before
replacing it. To uninstall, remove that one prototype .aex with Adobe closed.
The prototype is installed locally. Updating the `.aex` requires Windows
administrator approval.

After copying, close editors, Adobe and GPU receivers, then in a normal terminal:

```powershell
.\Tools\TestAfterEffects.cmd
```

The prepared harness keeps an isolated illuminated Unreal scene alive, starts
a fresh Adobe instance, creates a disposable comp/solid and applies the native
effect. It requests 8/16/32-bpc renders, freeze/resume, half resolution and
disconnect through Adobe's render queue and the stock TIFF Sequence with Alpha
template. It saves host-rendered TIFFs, decoded PNG previews, native diagnostics
and UnrealAELinkDemo.aep in artifacts/. The test verifies received-frame callbacks
at all three pixel formats, dimensions, visible image samples, opaque alpha
samples, frozen pixel equality and return to black after disconnect.
It changes no script/network permission setting. Its JSX is only a host test;
the effect and transport are native. Normal Adobe startup is used with a hidden
window: `-noui` crashes locally with 0xC0000409 even in a minimal script that does
not apply the effect. The original `saveFrameToPng` calls produced no images;
render queue completion and artifact validation now establish real host renders.
Adobe caches plugin discovery, so a registration callback is not required on
every test run. Fresh logs and exact test-owned image paths prevent stale passes.

For manual testing, launch the supplied Unreal project with DX12, create a
1280x720 solid in Adobe and apply Effect > UnrealAELink > UnrealAELink. Enable
Connect and Live, then advance/preview the Adobe composition or refresh the
effect to request a new image. Stop standalone GPU ReceiverTest so Adobe can
take its lease. The metadata-only receiver uses a separate lease.

## Actual validation

- The .aex builds with no final warnings. Windows binary inspection confirms
  both entry points and the resource section.
- WindowsIPC, WindowsGPU and WindowsAdobeFrameClient native CTest groups pass.
  The Adobe client test receives real shared textures in a separate process,
  checks every pixel/camera pairing and tests independent frozen/live
  subscribers, resume and disconnect.
- AdobeRenderTests passes against the actual renderer with official SDK pixel
  worlds: channel conversion at 8/16/32 bpc, alpha, row stride, tile origins,
  downsampling, letterbox, disconnected black, suite balancing and cancellation.
  This is a native test with test callbacks, **not an Adobe host run**.
- Unreal -> native receiver runtime acceptance is verified separately.
- Actual Adobe 26.5 loading, SmartFX rendering at 8/16/32 bpc, sequence
  setup/resetup/setdown, global teardown, freeze/resume, half-resolution output,
  disconnect and project saving have run locally. Two frozen host renders used
  the same sequence and identical decoded pixels. Resume used a later sequence.
  The cache dependency flag is declared in both Global Setup and PiPL so Adobe
  accepts the frame/session GUID mix-in.
- GUI interaction, project reopen, undo/redo and longer sessions remain
  unverified. v0.1 export reads the latest live frame; it is not deterministic
  timeline synchronization.

## Limits

Fixed opaque 1280x720 RGBA8 LDR Beauty. 16/32-bpc conversion adds no HDR detail.
No working-space/OCIO conversion; start with an unmanaged test comp. Adobe pulls
frames on render requests; no forced idle redraw is implemented. Deterministic
export passed the actual AE host acceptance above. The separate automatic live
refresh investigation is recorded in [timeline details](../../docs/deterministic.md).
Multi-frame rendering is not advertised. One Adobe process owns the GPU lease;
aerender and interactive Adobe cannot both consume it simultaneously. Camera
control/conversion, alpha, depth and other passes are outside scope.
