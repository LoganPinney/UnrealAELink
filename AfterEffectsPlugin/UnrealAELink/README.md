# Native After Effects prototype

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
host render callbacks never wait for the remote GPU or engine.

SmartFX pre-render selects one immutable frame and mixes session/sequence into
the GUID; smart-render uses that same snapshot. Legacy render also exists.
The renderer handles Adobe ARGB8/16/32 worlds, padded rows, tile origins,
downsampling, aspect-fit letterboxing, opaque black when disconnected and host
cancellation. Serializable sequence data contains only an instance token;
global/sequence teardown releases subscriptions.

## Build and installation

From the repository root, with the extracted official SDK:

```powershell
.\Tools\BuildAfterEffects.cmd -SDKRoot "C:\path\to\AfterEffectsSDK_26.5_win"
```

The SDK root must contain Examples/Headers/AE_Effect.h. AE_SDK_ROOT can supply
the path instead. The build also runs AdobeRenderTests against the SDK worlds.
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
**It is not installed yet:** Windows denied the copy and the administrator
elevation request was cancelled.

After copying, close editors, Adobe and GPU receivers, then in a normal terminal:

```powershell
.\Tools\TestAfterEffects.cmd
```

The prepared harness keeps an isolated illuminated Unreal scene alive, starts
a fresh Adobe instance, creates a disposable comp/solid and applies the native
effect. It requests 8/16/32-bpc renders, freeze/resume, half resolution and
disconnect, saving native diagnostics, PNGs and UnrealAELinkDemo.aep in artifacts/.
It changes no script/network permission setting. Its JSX is only a host test;
the effect and transport are native. **This harness has not run**, so its Adobe
startup/export assumptions remain unverified.

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
- Actual Adobe discovery/loading, sequence lifecycle, project save/reopen, GUI,
  SmartFX caching and host-rendered images remain unverified. These must pass
  before the After Effects milestone can be called complete.

## Limits

Fixed opaque 1280x720 RGBA8 LDR Beauty. 16/32-bpc conversion adds no HDR detail.
No working-space/OCIO conversion; start with an unmanaged test comp. Adobe pulls
frames on render requests; no forced idle redraw or deterministic export exists.
Multi-frame rendering is not advertised. One Adobe process owns the GPU lease;
aerender and interactive Adobe cannot both consume it simultaneously. Timeline
sync, camera control/conversion, alpha, depth and other passes are outside scope.
