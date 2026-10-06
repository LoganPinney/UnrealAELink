# UnrealAELink v0.2: AE-authoritative deterministic timeline

The actual Adobe 26.5 end-to-end acceptance passed on October 6, 2026. v0.1
receives asynchronous latest Beauty; v0.2 adds AE-driven direct Sequencer
evaluation and exact completed-image matching.

## Sequence selection and time mapping

`UnrealAELink.Sequence /Game/Test/MySequence` explicitly selects one asset.
Changing selection while a request is in flight is rejected. The game-thread
service resolves a `ULevelSequence`, creates a `ULevelSequencePlayer` and transient
`ALevelSequenceActor` using `CreateLevelSequencePlayer`, and retains them until
selection changes, the world changes, or the service stops. No automatic
multi-sequence discovery is performed. An active sequence camera cut supplies
the Beauty camera; otherwise the existing selected perspective view is used.

AE sends the original signed `current_time` and positive `time_scale`. Neither
`time_step` nor a presumed frame rate determines the request identity. Layer
start time must be composition zero; stretch must be 100%; remapping disabled.
Nested-comp, time-offset and remapping adaptation is outside this prototype.
Sequence absolute timeline zero maps to AE zero; requests outside the authored
playback range are rejected, including negative times.

The player's actual `GetFrameRate()` determines the frame representation.
`RationalToFrame` cancels integer factors, checks multiplication and frame-number
overflow, and divides into whole frame plus remainder. Only the final fractional
remainder becomes UE's float subframe, preserving noninteger-rate and subframe
evaluation. The original rational stays unchanged in request, response, GPU slot
and logging. UE's finite `FFrameTime` precision is a limitation; arbitrary
mathematical rational times cannot be represented with infinite precision.

The exact APIs are:

```cpp
FFrameTime Time(FFrameNumber(Whole), Fraction);
FMovieSceneSequencePlaybackParams Params(Time, EUpdatePositionMethod::Jump);
Params.bHasJumped = true;
Player->SetPlaybackPosition(Params);
```

The locally installed UE 5.8 implementation flushes the evaluation runner for
this direct non-async update. The service checks `GetCurrentTime()` before
capturing. `Jump` avoids intervening playback events; it never calls `Play()` or
waits for realtime playback. See Epic's [playback parameter API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/MovieScene/FMovieSceneSequencePlaybackParam-)
and [SetPlaybackPosition](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/MovieScene/UMovieSceneSequencePlayer/SetPlaybackPosition).

## Ordering and ownership

One process-wide FrameClient worker owns the GPU receiver and command client.
Deterministic callbacks queue jobs and wait on a condition variable. Identical
outstanding requests from the same effect instance share a job, request id and
immutable completed image. Other jobs are FIFO serialized. No Adobe suites are
called on this worker. Abort polling runs only on the requesting host callback.

The Unreal game thread takes one request, directly evaluates Sequencer and sends
dirty end-of-frame component state before `CaptureScene`. The existing capture
and triple-buffer copy perform the actual rendering and transport. Deterministic
capture resets temporal history and disables motion blur; simulated/history
dependent animation (physics, particles, temporal accumulation) is unsupported.

The GPU control slot carries request id and the original rational time. The
existing manual DX12 fence is signalled after the copy on the executing RHI
command list. A subsequent render-thread poll observes completion before the
game thread publishes a successful response. The response includes the resource
session and Beauty sequence. The producer suspends live copies while the mailbox
is Evaluating or Complete, so the exact slot cannot be overwritten before the
consumer copies it. The consumer selects the exact request id, reads back
privately, and checks id, rational time, resource session and sequence against
the response. It releases the mailbox after the immutable private image exists.

PreRender retains that image in `pre_render_data`; its deletion callback releases
the shared pointer. SmartRender paints only that pointer. It does not resample
Latest or the current mutable Sequencer. Session, sequence and request id mix
into the SmartFX GUID. The legacy callback also requests exact images in
Deterministic mode. Live mode keeps the existing latest-frame and freeze paths.

## Timeouts and shutdown

The entire queued request has a 15,000ms deadline, including producer startup,
submission, GPU completion, response and local receipt. A failed response,
identity mismatch, disconnection, cancellation or timeout returns no image and
fails the host render. There is no latest-frame substitution. Diagnostic logs
record the rational time, request id, status and Beauty sequence.

On timeout or cancellation the client closes its mailbox lease. A late response
cannot match another id. Unreal retains its active scene until its own GPU work
retires before taking the next request. A pathological remote GPU hang therefore
requires restarting the producer; client waits remain bounded. Existing pinned
slot protection and generation renewal remain in place. Shutdown retires Beauty
work before destroying sequence UObjects. Command data mutex abandonment fails
closed; restart the service to recover an invalidated command block.

`PF_OutFlag2_SUPPORTS_THREADED_RENDERING` is intentionally absent from both
runtime flags and PiPL. Parallel AE MFR and multiple simultaneous mutable
Sequencers are unsupported. One Windows logon-session producer and one GPU
consumer process are supported. Fixed 1280x720 opaque RGBA8 LDR output remains;
16/32-bpc AE conversion adds no HDR data. Extra render passes and camera control
are outside scope. Disable AE Multi-Frame Rendering for deterministic exports;
the acceptance script explicitly disables it for its run.

## Acceptance evidence

`Tools/TestDeterministic.cmd` builds no fake images: it launches an isolated
Unreal renderer and actual Adobe 26.5, whose native effect requests the frames.
The transient fixture has one real `ULevelSequence` with a 30/1 display rate,
24000/1 tick rate and a linear transform channel moving an illuminated cube from
Y=-240 at zero to Y=240 at two seconds. Its camera is fixed. The test owns no
persistent level/sequence assets and does not alter the user's existing asset.

The host script requests known times 0, 1 and 2 seconds, renders 0..30 as one
Render Queue job, purges AE caches, then requests 20,3,17,0,29. It also requests
1/60s and 17*1001/30000s. `ValidateDeterministic.ps1` correlates actual AE
current_time/time_scale logs with UE evaluation, actor transforms, capture and
publication; every decoded TIFF's full RGB FNV hash must equal the native
Beauty image logged by AE RENDER for the exact time and request id. TIFF pixel
centroids must match projected cube positions,
advance on every sequential frame and agree between random and sequential
access. Different known times must have different pixels. It writes evidence
CSV and PNG previews only after host rendering. AE's NTSC TIFF filename suffix
can round down; its requested rational time and full image hash establish
identity instead. The fresh test-owned NTSC output must contain exactly one TIFF.
Native protocol/DX12 tests are
separate and do not substitute for this Adobe acceptance.

`Tools/TestDeterministic.cmd -NativeOnly` independently checks the Unreal
request/evaluation/capture path through the same FrameClient, without claiming
an Adobe host test. See validation.md for results actually obtained.

## Secondary live refresh investigation

After deterministic acceptance passed, the supplied Adobe SDK 26.5 headers were
inspected. `AE_GeneralPlug.h` provides `AEGP_RegisterWithAEGP`,
`AEGP_RegisterIdleHook` and `AEGP_CauseIdleRoutinesToBeCalled`. The last function
is explicitly documented in that header as safe from a non-main thread, but
suite acquisition is not: cache its function pointer on the host main thread.
The existing FrameClient worker must continue to avoid all other Adobe APIs.

A follow-on integration can register one process-wide idle hook, atomically
signal newer Live Beauty sequences, and coalesce wakeups. The main-thread hook
should check the active composition and its connected Live effects, suppress
updates during deterministic rendering/export, and throttle invalidation to
15–30 Hz (33–67 ms). `PF_AdvItemSuite1::PF_TouchActiveItem()` is a candidate
without borrowed callback pointers. `PF_ForceRerender` requires a valid current
`PF_InData` and world; render callback pointers must not be retained for later
idle use. `PF_InvalidateRect` belongs to custom UI drawing, so it alone does not
establish effect-image cache invalidation.

No idle hook is enabled in v0.2. Targeted image-cache invalidation, active-comp
filtering, hook teardown, undo/project-close behavior, and actual interactive
preview throttling need a separate GUI acceptance test. The existing Refresh
button and host-requested Live rendering remain available. This investigation
does not claim automatic live redraw has been demonstrated.
