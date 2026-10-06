# Metadata protocol v1

The camera metadata below is unchanged. v0.2 deterministic rendering adds the
independent **Request.v1** channel and **Beauty.v2** slot identity described at
the end of this document; it does not reinterpret camera metadata.

Windows x64, little endian, IEEE-754 floats/doubles. There are no pointers,
Unreal structs, C++ bools, variable-length fields, or native-sized integers in
the wire layout. `Shared/include/UnrealAELink/Protocol.h` is the contract; compile
time assertions check size, alignment and significant offsets in both builds.
All values are local-machine data. This protocol is not a portable file format.

## Named Windows objects

- Mapping: `Local\UnrealAELink.Metadata.v1`, 192 bytes, pagefile backed.
- Data mutex: `Local\UnrealAELink.Metadata.Guard.v1`.
- Exclusive producer mutex: `Local\UnrealAELink.Metadata.Owner.v1`.

`Local\` means the current Windows logon session. The default Windows object
security descriptor applies. Run editor and receiver as the same ordinary user,
at the same elevation. This prototype trusts local clients and is not a security
boundary. No files are written to carry camera data.

## Block layout

| Offset | Bytes | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | Magic | `0x4B4C4155`, bytes `UALK` |
| 4 | 4 | Version | `1` |
| 8 | 4 | HeaderBytes | `64` |
| 12 | 4 | BlockBytes | `192` |
| 16 | 8 | SessionId | QPC tick at producer startup, changes on restart |
| 24 | 4 | ProducerPid | Windows process ID, diagnostic |
| 28 | 4 | ProducerActive | 1 running, 0 gracefully stopped/invalid |
| 32 | 8 | ProducerHeartbeatMs | `GetTickCount64()` at publication |
| 40 | 4 | ReceiverPid | One receiver process lease; 0 unclaimed |
| 44 | 4 | Reserved | Zero |
| 48 | 8 | ReceiverHeartbeatMs | `GetTickCount64()` at read/lease renewal |
| 56 | 8 | Sequence | Increments on every publication, including no-camera samples |
| 64 | 128 | Frame | Layout below |

## Frame layout (offsets relative to Frame)

| Offset | Bytes | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | StructBytes | `128` |
| 4 | 4 | Version | `1` |
| 8 | 4 | Source | 0 none; 1 editor perspective viewport; 2 first player camera |
| 12 | 4 | Flags | Bit 0: valid camera. All other bits reserved |
| 16 | 8 | FrameNumber | Unreal `GFrameCounter`; not a timeline/Sequencer frame |
| 24 | 8 | PublishedTickMs | System uptime clock, same timebase in both processes |
| 32 | 8 | TimeSeconds | World time; can stay still in an editor-only world |
| 40 | 24 | Position[3] | X/Y/Z doubles, Unreal centimeters |
| 64 | 32 | Quaternion[4] | X/Y/Z/W doubles, Unreal camera orientation |
| 96 | 12 | Rotation[3] | Pitch/Yaw/Roll floats, Unreal degrees |
| 108 | 4 | FieldOfView | Unreal viewport `ViewFOV` or player view `FOV`, degrees |
| 112 | 4 | AspectRatio | Constrained camera aspect, otherwise viewport width/height |
| 116 | 12 | Scale[3] | Unit camera-view scale `(1,1,1)` |

Unreal is left-handed: X forward, Y right, Z up. The transform describes the
resolved view, not an arbitrary camera actor's scaled world transform. Player
view FOV comes from `FMinimalViewInfo`; aspect-axis constraint/render projection
adjustments may alter the final effective projection. This metadata is not a
complete projection matrix. No AE coordinate conversion is performed.

## Synchronization and validity

Producer initialization, every sample copy, every receiver read and lease
update happen under the data mutex. The producer holds the separate owner mutex
for the bridge lifetime, enforcing one producer. A second Producer object on
the same owning thread is also rejected (Windows mutex acquisition is recursive).
Producer lifecycle operations must occur on that same thread; the UE module
starts, ticks and stops on the main thread. Each transport object is single-thread
use. Consumers must copy the frame out before rendering/printing/doing other work.

Producer tick uses a zero-ms lock attempt, skipping a sample when busy. Startup
wait is bounded at 100 ms; consumer reads wait up to 5 ms; orderly close waits
up to 20 ms. The receiver polls at 10 Hz. It reads the latest whole sample and
may skip earlier samples; there is no queue, delivery guarantee, or backpressure.

The reader validates magic, version, sizes, active state, producer heartbeat,
camera-valid bit and frame size/version. A heartbeat older than 2000 ms is stale.
`GetTickCount64` is a shared monotonic uptime timebase, not Unix time or world
time. Never derive liveness from world time or `GFrameCounter`.

Receiver leases also expire after 2000 ms. The producer logs connection changes
after observing a renewed/expired lease. Normal consumer destruction clears its
lease. Ctrl+C/forced process termination may bypass C++ destructors: expiry still
detects it. Logging and output use copied data outside the mutex.

On `WAIT_ABANDONED`, a reader discards the potentially incomplete frame and marks
the block inactive. A live producer repairs the frame and marks it active on its
next publication. A producer acquiring an abandoned data mutex clears the
receiver lease and replaces the frame. A producer crash is rejected by its
heartbeat/abandoned mutex; a restart reinitializes the entire block under the
mutex, even if a receiver keeps the mapping alive. Reader identity is the pair
`(SessionId, Sequence)`, not just a frame number. The receiver closes and retries
after stale, incompatible, or abandoned results; `Busy` and `NoFrame` are retried.

One receiver *process* is supported. Two objects in that same process are not
independent clients. Multiple editor worlds/instances and multiple consumers
need a future named-source/session design. A second running editor must stop its
bridge before another editor can start one. Protocol changes require a version
bump and different object names; do not reinterpret v1 memory as a new layout.

## Deterministic request/response v1

Contract: `Shared/include/UnrealAELink/RequestProtocol.h`. Named mapping:
`Local\UnrealAELink.Request.v1`; named guard/owner mutexes:
`Local\UnrealAELink.Request.Guard.v1` and `.Request.Owner.v1`. A 152-byte block
uses magic `0x52504155`, version 1 and explicit block size. The first 56 bytes
contain producer session/PID/active/heartbeat, client PID/heartbeat, mailbox
state and reserved fields. No pointers or process-local synchronization objects
are in shared memory. Struct size/offset assertions compile on both sides.

| Request offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | StructBytes = 40 |
| 4 | 4 | Version = 1 |
| 8 | 8 | RequestId, nonzero; monotonic per AE process, QPC-seeded |
| 16 | 8 | signed TimeValue, original AE current_time |
| 24 | 8 | signed TimeScale, original AE time_scale, positive |
| 32 | 4 | Width, currently 1280 |
| 36 | 4 | Height, currently 720 |

| Response offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | StructBytes = 56 |
| 4 | 4 | Version = 1 |
| 8 | 8 | matching RequestId |
| 16 | 8 | RequestedTimeValue, unchanged |
| 24 | 8 | RequestedTimeScale, unchanged |
| 32 | 8 | BeautySession, shared-resource generation |
| 40 | 8 | BeautySequence, completed GPU copy sequence/fence |
| 48 | 4 | signed Status |
| 52 | 4 | Reserved = 0 |

Status: 0 Pending (never acknowledged), 1 Success, 2 InvalidTime,
3 NoSequence, 4 CaptureFailed, 5 Timeout, 6 Cancelled. NoSequence/invalid requests
produce an explicit failed response. Local timeout/cancellation also fail the AE
callback when no response can arrive. Success requires a nonzero matching
resource session and Beauty sequence, checked against privately received pixels.

States under the mutex: Idle -> Requested -> Evaluating -> Complete -> Idle.
The client submits only to Idle. The server consumes Requested once. It cannot
complete another request id/time, or complete Pending work. Complete holds live
capture until the consumer privately copies the exact frame and closes/releases
its mailbox. Requests use raw rational identity; even equivalent fractions are
not silently normalized in the wire records. Dimensions unsupported by this
fixed-output prototype fail explicitly.

The producer owner mutex restricts one server; PID/2-second heartbeat restricts
one client process. Producer restart resets the block/session; an old client
fails closed on a changed session. Reads/writes wait at most 5ms for the guard.
Mailbox waiting happens outside the guard; no mutex spans Sequencer evaluation,
remote waiting or GPU work. Abandoned guard invalidates the block and requires
service restart. A dead/stalled client expires its mailbox lease. Local request
wait is bounded at 15,000ms; late GPU work is retired before Unreal evaluates
another request, even when the old mailbox was canceled.

## Beauty control protocol v2

The triple-buffer resources, shared fence, ownership and lease algorithm are
retained. The new control mapping/guard/owner use Beauty.v2,
Beauty.Guard.v2 and Beauty.Owner.v2 names and version 2. This prevents old v1
binaries from misreading extended slot metadata. Camera FrameMetadata remains
128 bytes and protocol v1. The GPU header is still 848 bytes; each slot grows
from 152 to 176 bytes, with a 24-byte FrameIdentity appended at slot offset 152.
The block is now 1376 bytes.

FrameIdentity contains uint64 RequestId, int64 TimeValue, int64 TimeScale.
Live frames use all zeros. Deterministic frames carry the original nonzero id
and rational AE time. Live consumers select only id zero. Exact consumers select
only the requested id and validate time/session/sequence against the response.
The existing Reading pin lasts through private GPU copy/readback; AE subsequently
owns an immutable CPU snapshot. Ready slots may appear in the control block
before their fence completes, but receivers cannot consume them until the
completion fence is observed. The deterministic **successful response** is
published only after that completion, not after slot metadata publication.
