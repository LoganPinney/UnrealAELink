#include "UnrealAELink/GpuTransport.h"

#ifdef UNREAL_AE_LINK_WITH_UNREAL
#include "Windows/WindowsHWrapper.h"
#include "D3D12ThirdParty.h"
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#endif
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstring>
#include <cwchar>
#include <limits>
#include <new>

namespace UnrealAELink
{
namespace GpuDetail
{
using Microsoft::WRL::ComPtr;
class Handle
{
public:
    Handle() = default;
    ~Handle() { Reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE Get() const { return Value; }
    void Reset(HANDLE Next = nullptr) { if (Value) CloseHandle(Value); Value = Next; }
private:
    HANDLE Value = nullptr;
};
class Lock
{
public:
    Lock(HANDLE Mutex, DWORD Timeout) : Mutex(Mutex), Status(WaitForSingleObject(Mutex, Timeout)) {}
    ~Lock() { if (Acquired()) ReleaseMutex(Mutex); }
    bool Acquired() const { return Status == WAIT_OBJECT_0 || Status == WAIT_ABANDONED; }
    bool Abandoned() const { return Status == WAIT_ABANDONED; }
private:
    HANDLE Mutex;
    DWORD Status;
};
bool Fresh(std::uint64_t Timestamp) { const auto Now = GetTickCount64(); return Now >= Timestamp && Now - Timestamp <= LeaseTimeoutMs; }
bool Valid(const GpuBlock& B)
{
    return B.Magic == GpuProtocolMagic && B.Version == GpuProtocolVersion && B.BlockBytes == sizeof(GpuBlock) &&
           B.HeaderBytes == offsetof(GpuBlock, Slots) && B.Width == BeautyWidth && B.Height == BeautyHeight &&
           B.Format == BeautyFormat && B.SlotCount == GpuSlotCount;
}
struct Mapping
{
    Handle Guard, Memory;
    GpuBlock* Block = nullptr;
    std::int32_t Error = 0;
    ~Mapping() { Reset(); }
    void Reset()
    {
        if (Block) UnmapViewOfFile(Block);
        Block = nullptr; Memory.Reset(); Guard.Reset();
    }
    Result Open(bool Create)
    {
        Reset();
        Guard.Reset(Create ? CreateMutexW(nullptr, 0, GpuGuardName) : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, 0, GpuGuardName));
        if (!Guard.Get()) { Error = static_cast<std::int32_t>(GetLastError()); return Create ? Result::Error : Result::NoProducer; }
        Memory.Reset(Create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(GpuBlock), GpuMappingName)
                            : OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, 0, GpuMappingName));
        if (!Memory.Get()) { Error = static_cast<std::int32_t>(GetLastError()); Reset(); return Create ? Result::Error : Result::NoProducer; }
        Block = static_cast<GpuBlock*>(MapViewOfFile(Memory.Get(), FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(GpuBlock)));
        if (!Block) { Error = static_cast<std::int32_t>(GetLastError()); Reset(); return Result::Error; }
        Error = 0;
        return Result::Ok;
    }
};
D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE Type)
{
    D3D12_HEAP_PROPERTIES H{}; H.Type = Type; H.CreationNodeMask = 1; H.VisibleNodeMask = 1; return H;
}
D3D12_RESOURCE_DESC TextureDesc()
{
    D3D12_RESOURCE_DESC D{};
    D.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; D.Width = BeautyWidth; D.Height = BeautyHeight;
    D.DepthOrArraySize = 1; D.MipLevels = 1; D.Format = DXGI_FORMAT_R8G8B8A8_UNORM; D.SampleDesc.Count = 1;
    D.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; D.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    return D;
}
D3D12_RESOURCE_BARRIER Barrier(ID3D12Resource* Resource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After)
{
    D3D12_RESOURCE_BARRIER B{}; B.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    B.Transition.pResource = Resource; B.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    B.Transition.StateBefore = Before; B.Transition.StateAfter = After; return B;
}
}

using GpuDetail::ComPtr;
struct GpuProducer::Impl
{
    GpuDetail::Mapping Map;
    GpuDetail::Handle Owner, TextureHandles[GpuSlotCount], FenceHandle;
    bool OwnsMutex = false, Active = false;
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12Resource> Textures[GpuSlotCount];
    ComPtr<ID3D12Fence> Ready;
    std::uint64_t Session = 0, Next = 0;
};
GpuProducer::GpuProducer() : State(new (std::nothrow) Impl) {}
GpuProducer::~GpuProducer() { Close(); }
std::int32_t GpuProducer::LastError() const { return State ? State->Map.Error : E_OUTOFMEMORY; }
std::uint64_t GpuProducer::Session() const { return State ? State->Session : 0; }
ID3D12Resource* GpuProducer::Texture(std::uint32_t Slot) const { return State && Slot < GpuSlotCount ? State->Textures[Slot].Get() : nullptr; }
ID3D12Fence* GpuProducer::Fence() const { return State ? State->Ready.Get() : nullptr; }
bool GpuProducer::OwnWritesComplete() const { return State && State->Ready && State->Ready->GetCompletedValue() >= State->Next; }

Result GpuProducer::Open(ID3D12Device* Device)
{
    if (!State || !Device) return Result::Error;
    Close();
    State->Owner.Reset(CreateMutexW(nullptr, 0, GpuOwnerName));
    if (!State->Owner.Get()) { State->Map.Error = static_cast<std::int32_t>(GetLastError()); return Result::Error; }
    const DWORD Wait = WaitForSingleObject(State->Owner.Get(), 0);
    if (Wait != WAIT_OBJECT_0 && Wait != WAIT_ABANDONED) { State->Owner.Reset(); return Result::Busy; }
    State->OwnsMutex = true;
    const auto Mapped = State->Map.Open(true);
    if (Mapped != Result::Ok) { Close(); return Mapped; }
    const Result Initialized = [&]() -> Result
    {
        GpuDetail::Lock Guard(State->Map.Guard.Get(), 100);
        if (!Guard.Acquired()) return Result::Busy;
        auto& B = *State->Map.Block;
        if (GpuDetail::Valid(B) && B.ProducerActive && B.ProducerPid == GetCurrentProcessId()) return Result::Busy;
        State->Device = Device;
        LARGE_INTEGER Counter{}; QueryPerformanceCounter(&Counter);
        State->Session = static_cast<std::uint64_t>(Counter.QuadPart);
        State->Next = 0;
        std::memset(&B, 0, sizeof(B));
        B.Magic = GpuProtocolMagic; B.Version = GpuProtocolVersion; B.HeaderBytes = offsetof(GpuBlock, Slots);
        B.BlockBytes = sizeof(GpuBlock); B.Session = State->Session; B.ProducerPid = GetCurrentProcessId();
        const LUID Adapter = Device->GetAdapterLuid(); B.AdapterLuidLow = Adapter.LowPart; B.AdapterLuidHigh = Adapter.HighPart;
        B.Width = BeautyWidth; B.Height = BeautyHeight; B.Format = BeautyFormat; B.SlotCount = GpuSlotCount;
        const auto Heap = GpuDetail::Heap(D3D12_HEAP_TYPE_DEFAULT);
        const auto Desc = GpuDetail::TextureDesc();
        for (std::uint32_t I = 0; I < GpuSlotCount; ++I)
        {
            HRESULT Hr = Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_SHARED, &Desc, D3D12_RESOURCE_STATE_COMMON,
                                                         nullptr, IID_PPV_ARGS(&State->Textures[I]));
            if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
            swprintf_s(B.TextureNames[I], L"Local\\UnrealAELink.Beauty.%u.%llu.Texture%u", B.ProducerPid,
                       static_cast<unsigned long long>(B.Session), I);
            HANDLE Shared = nullptr;
            Hr = Device->CreateSharedHandle(State->Textures[I].Get(), nullptr, GENERIC_ALL, B.TextureNames[I], &Shared);
            if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
            State->TextureHandles[I].Reset(Shared);
        }
        HRESULT Hr = Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&State->Ready));
        if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
        swprintf_s(B.FenceName, L"Local\\UnrealAELink.Beauty.%u.%llu.Ready", B.ProducerPid, static_cast<unsigned long long>(B.Session));
        HANDLE Shared = nullptr;
        Hr = Device->CreateSharedHandle(State->Ready.Get(), nullptr, GENERIC_ALL, B.FenceName, &Shared);
        if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
        State->FenceHandle.Reset(Shared);
        B.ProducerActive = 1; B.ProducerHeartbeatMs = GetTickCount64(); State->Active = true;
        return Result::Ok;
    }();
    if (Initialized != Result::Ok) Close();
    return Initialized;
}

void GpuProducer::Close() noexcept
{
    if (!State) return;
    if (State->Active && State->Map.Block)
    {
        GpuDetail::Lock Guard(State->Map.Guard.Get(), 20);
        if (Guard.Acquired() && State->Map.Block->Session == State->Session) State->Map.Block->ProducerActive = 0;
    }
    State->Active = false; State->Map.Reset();
    for (std::uint32_t I = 0; I < GpuSlotCount; ++I) { State->TextureHandles[I].Reset(); State->Textures[I].Reset(); }
    State->FenceHandle.Reset(); State->Ready.Reset(); State->Device.Reset();
    if (State->OwnsMutex) ReleaseMutex(State->Owner.Get());
    State->OwnsMutex = false; State->Owner.Reset();
}
void GpuProducer::Heartbeat()
{
    if (!State || !State->Active) return;
    GpuDetail::Lock Guard(State->Map.Guard.Get(), 0);
    if (Guard.Acquired()) State->Map.Block->ProducerHeartbeatMs = GetTickCount64();
}
Result GpuProducer::Reserve(std::uint32_t& Slot, std::uint64_t& Value, bool& Connected)
{
    if (!State || !State->Active) return Result::NoProducer;
    GpuDetail::Lock Guard(State->Map.Guard.Get(), 0);
    if (!Guard.Acquired()) return Result::Busy;
    auto& B = *State->Map.Block;
    B.ProducerHeartbeatMs = GetTickCount64();
    if (Guard.Abandoned()) { B.ProducerActive = 0; return Result::Abandoned; }
    Connected = B.ReceiverPid && GpuDetail::Fresh(B.ReceiverHeartbeatMs);
    for (const auto& S : B.Slots)
        if (S.State == GpuSlotState::Reading && (!Connected || S.ReaderPid != B.ReceiverPid))
            return Result::Stale; // New resources, never reuse a possibly in-flight read.
    if (!Connected) return Result::NoFrame; // Avoid GPU copies when nobody is watching.
    const auto Complete = State->Ready->GetCompletedValue();
    std::uint64_t Oldest = std::numeric_limits<std::uint64_t>::max();
    Slot = GpuSlotCount;
    for (std::uint32_t I = 0; I < GpuSlotCount; ++I)
    {
        auto& S = B.Slots[I];
        if (S.State == GpuSlotState::Writing && S.FenceValue <= Complete) S.State = GpuSlotState::Free;
        if ((S.State == GpuSlotState::Free || S.State == GpuSlotState::Ready) && S.FenceValue <= Complete && S.Sequence < Oldest)
        { Oldest = S.Sequence; Slot = I; }
    }
    if (Slot == GpuSlotCount) return Result::Busy;
    B.Slots[Slot].State = GpuSlotState::Writing;
    Value = ++State->Next;
    B.Slots[Slot].FenceValue = Value;
    return Result::Ok;
}
Result GpuProducer::Publish(std::uint32_t Slot, std::uint64_t Value, const FrameMetadata& Camera)
{
    if (!State || !State->Active || Slot >= GpuSlotCount) return Result::Error;
    GpuDetail::Lock Guard(State->Map.Guard.Get(), 5);
    if (!Guard.Acquired()) return Result::Busy;
    auto& B = *State->Map.Block;
    auto& S = B.Slots[Slot];
    if (S.State != GpuSlotState::Writing) return Result::Error;
    S.Camera = Camera; S.Camera.StructBytes = sizeof(FrameMetadata); S.Camera.Version = ProtocolVersion;
    S.Camera.AspectRatio = static_cast<float>(BeautyWidth) / BeautyHeight; S.Camera.PublishedTickMs = GetTickCount64();
    S.FenceValue = Value; S.Sequence = Value; S.ReaderPid = 0; S.State = GpuSlotState::Ready;
    B.ProducerHeartbeatMs = GetTickCount64();
    return Result::Ok;
}
void GpuProducer::Cancel(std::uint32_t Slot)
{
    if (!State || !State->Active || Slot >= GpuSlotCount) return;
    GpuDetail::Lock Guard(State->Map.Guard.Get(), 5);
    if (Guard.Acquired() && State->Map.Block->Slots[Slot].State == GpuSlotState::Writing)
        State->Map.Block->Slots[Slot].State = GpuSlotState::Free;
}

struct GpuConsumer::Impl
{
    GpuDetail::Mapping Map;
    std::uint64_t Session = 0, LastSequence = 0, DoneValue = 0;
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12Resource> Shared[GpuSlotCount], PrivateTexture, Readback;
    ComPtr<ID3D12Fence> Ready, Done;
    ComPtr<ID3D12CommandQueue> Queue;
    ComPtr<ID3D12CommandAllocator> Allocator;
    ComPtr<ID3D12GraphicsCommandList> List;
    GpuDetail::Handle Event;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint{};
    std::uint64_t ReadbackSize = 0;
    bool Pending = false;
    std::uint32_t PendingSlot = GpuSlotCount;
    GpuSlot PendingMetadata{};

    void ReleaseSlot()
    {
        if (!Map.Block || PendingSlot >= GpuSlotCount) return;
        GpuDetail::Lock Guard(Map.Guard.Get(), 5);
        if (Guard.Acquired() && GpuDetail::Valid(*Map.Block) && Map.Block->Session == Session)
        {
            auto& S = Map.Block->Slots[PendingSlot];
            if (S.State == GpuSlotState::Reading && S.ReaderPid == GetCurrentProcessId())
            { S.State = GpuSlotState::Ready; S.ReaderPid = 0; }
        }
        if (Guard.Acquired()) PendingSlot = GpuSlotCount;
    }
};
GpuConsumer::GpuConsumer() : State(new (std::nothrow) Impl) {}
GpuConsumer::~GpuConsumer() { Close(); }
std::int32_t GpuConsumer::LastError() const { return State ? State->Map.Error : E_OUTOFMEMORY; }

Result GpuConsumer::Open()
{
    if (!State) return Result::Error;
    Close();
    if (!State) return Result::Error;
    auto ResultCode = State->Map.Open(false);
    if (ResultCode != Result::Ok) return ResultCode;
    GpuBlock Header{};
    {
        GpuDetail::Lock Guard(State->Map.Guard.Get(), 5);
        if (!Guard.Acquired()) return Result::Busy;
        if (Guard.Abandoned()) { State->Map.Block->ProducerActive = 0; return Result::Abandoned; }
        Header = *State->Map.Block;
        if (!GpuDetail::Valid(Header)) return Result::Incompatible;
        if (!Header.ProducerActive || !GpuDetail::Fresh(Header.ProducerHeartbeatMs)) return Result::Stale;
        if (Header.ReceiverPid && Header.ReceiverPid != GetCurrentProcessId() && GpuDetail::Fresh(Header.ReceiverHeartbeatMs)) return Result::Busy;
        State->Session = Header.Session;
        State->Map.Block->ReceiverPid = GetCurrentProcessId();
        State->Map.Block->ReceiverHeartbeatMs = GetTickCount64();
    }
    ComPtr<IDXGIFactory4> Factory;
    HRESULT Hr = CreateDXGIFactory1(IID_PPV_ARGS(&Factory));
    if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
    const LUID AdapterLuid{Header.AdapterLuidLow, Header.AdapterLuidHigh};
    ComPtr<IDXGIAdapter1> Adapter;
    Hr = Factory->EnumAdapterByLuid(AdapterLuid, IID_PPV_ARGS(&Adapter));
    if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
    Hr = D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&State->Device));
    if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
    for (std::uint32_t I = 0; I <= GpuSlotCount; ++I)
    {
        const wchar_t* Name = I == GpuSlotCount ? Header.FenceName : Header.TextureNames[I];
        if (Name[GpuNameLength - 1] != 0) return Result::Incompatible;
        HANDLE Shared = nullptr;
        Hr = State->Device->OpenSharedHandleByName(Name, GENERIC_ALL, &Shared);
        if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
        GpuDetail::Handle Named; Named.Reset(Shared);
        if (I == GpuSlotCount) Hr = State->Device->OpenSharedHandle(Shared, IID_PPV_ARGS(&State->Ready));
        else Hr = State->Device->OpenSharedHandle(Shared, IID_PPV_ARGS(&State->Shared[I]));
        if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
        if (I < GpuSlotCount)
        {
            const auto D = State->Shared[I]->GetDesc();
            if (D.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || D.Width != BeautyWidth || D.Height != BeautyHeight ||
                D.Format != DXGI_FORMAT_R8G8B8A8_UNORM || D.MipLevels != 1 || D.DepthOrArraySize != 1 || D.SampleDesc.Count != 1 ||
                !(D.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)) return Result::Incompatible;
        }
    }
    D3D12_COMMAND_QUEUE_DESC QueueDesc{}; QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Hr = State->Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&State->Queue));
    if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
    Hr = State->Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&State->Allocator));
    if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
    Hr = State->Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, State->Allocator.Get(), nullptr, IID_PPV_ARGS(&State->List));
    if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
    State->List->Close();
    Hr = State->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&State->Done));
    if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
    State->Event.Reset(CreateEventW(nullptr, 0, 0, nullptr));
    if (!State->Event.Get()) { State->Map.Error = static_cast<std::int32_t>(GetLastError()); return Result::Error; }
    const auto DefaultHeap = GpuDetail::Heap(D3D12_HEAP_TYPE_DEFAULT);
    const auto Desc = GpuDetail::TextureDesc();
    Hr = State->Device->CreateCommittedResource(&DefaultHeap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COMMON,
                                               nullptr, IID_PPV_ARGS(&State->PrivateTexture));
    if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
    UINT Rows = 0; UINT64 RowBytes = 0;
    State->Device->GetCopyableFootprints(&Desc, 0, 1, 0, &State->Footprint, &Rows, &RowBytes, &State->ReadbackSize);
    D3D12_RESOURCE_DESC Buffer{}; Buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; Buffer.Width = State->ReadbackSize;
    Buffer.Height = 1; Buffer.DepthOrArraySize = 1; Buffer.MipLevels = 1; Buffer.SampleDesc.Count = 1; Buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const auto ReadbackHeap = GpuDetail::Heap(D3D12_HEAP_TYPE_READBACK);
    Hr = State->Device->CreateCommittedResource(&ReadbackHeap, D3D12_HEAP_FLAG_NONE, &Buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr, IID_PPV_ARGS(&State->Readback));
    if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
    State->LastSequence = 0; State->DoneValue = 0; State->Pending = false;
    return Result::Ok;
}

void GpuConsumer::Close() noexcept
{
    if (!State) return;
    // No queue waits on a remote fence are ever submitted. Our own GPU copy can
    // still time out due to a device hang; keep those objects alive rather than
    // free memory while the GPU uses it. This exceptional retirement is bounded
    // to this abandoned session and lasts until process exit (documented).
    if (State->Pending && State->Done && State->Done->GetCompletedValue() < State->DoneValue)
    {
        State->Done->SetEventOnCompletion(State->DoneValue, State->Event.Get());
        if (WaitForSingleObject(State->Event.Get(), 100) != WAIT_OBJECT_0 && SUCCEEDED(State->Device->GetDeviceRemovedReason()))
        {
            if (State->Map.Block)
            {
                GpuDetail::Lock Guard(State->Map.Guard.Get(), 5);
                if (Guard.Acquired() && State->Map.Block->Session == State->Session)
                { State->Map.Block->ReceiverPid = 0; State->Map.Block->ReceiverHeartbeatMs = 0; }
            }
            State->Map.Reset();
            (void)State.release(); // GPU hang: deliberate lifetime extension, no dangling GPU objects.
            State.reset(new (std::nothrow) Impl);
            return;
        }
    }
    State->ReleaseSlot();
    if (State->Map.Block)
    {
        GpuDetail::Lock Guard(State->Map.Guard.Get(), 5);
        if (Guard.Acquired() && State->Map.Block->Session == State->Session && State->Map.Block->ReceiverPid == GetCurrentProcessId())
        { State->Map.Block->ReceiverPid = 0; State->Map.Block->ReceiverHeartbeatMs = 0; }
    }
    const auto Error = State->Map.Error;
    State.reset(new (std::nothrow) Impl);
    if (State) State->Map.Error = Error;
}

Result GpuConsumer::Read(BeautyFrame& Frame, std::uint32_t TimeoutMs)
{
    if (!State || !State->Map.Block || !State->Device || !State->Ready) return Result::NoProducer;
    {
        GpuDetail::Lock Guard(State->Map.Guard.Get(), 5);
        if (!Guard.Acquired()) return Result::Busy;
        auto& B = *State->Map.Block;
        if (Guard.Abandoned()) { B.ProducerActive = 0; return Result::Abandoned; }
        if (!GpuDetail::Valid(B)) return Result::Incompatible;
        if (B.Session != State->Session || !B.ProducerActive || !GpuDetail::Fresh(B.ProducerHeartbeatMs)) return Result::Stale;
        if (B.ReceiverPid != GetCurrentProcessId() && B.ReceiverPid && GpuDetail::Fresh(B.ReceiverHeartbeatMs)) return Result::Busy;
        B.ReceiverPid = GetCurrentProcessId(); B.ReceiverHeartbeatMs = GetTickCount64();
        if (!State->Pending)
        {
            // Retry an acknowledgement that previously lost the short mutex race.
            if (State->PendingSlot < GpuSlotCount)
            {
                auto& Old = B.Slots[State->PendingSlot];
                if (Old.State == GpuSlotState::Reading && Old.ReaderPid == GetCurrentProcessId())
                { Old.State = GpuSlotState::Ready; Old.ReaderPid = 0; }
                State->PendingSlot = GpuSlotCount;
            }
            std::uint32_t Latest = GpuSlotCount; auto Sequence = State->LastSequence;
            const auto Completed = State->Ready->GetCompletedValue();
            if (Completed == std::numeric_limits<std::uint64_t>::max()) { State->Map.Error = DXGI_ERROR_DEVICE_REMOVED; return Result::Error; }
            for (std::uint32_t I = 0; I < GpuSlotCount; ++I)
            {
                const auto& S = B.Slots[I];
                if (S.State == GpuSlotState::Ready && S.FenceValue <= Completed && S.Sequence > Sequence)
                { Latest = I; Sequence = S.Sequence; }
            }
            if (Latest == GpuSlotCount) return Result::NoFrame;
            B.Slots[Latest].State = GpuSlotState::Reading; B.Slots[Latest].ReaderPid = GetCurrentProcessId();
            State->PendingSlot = Latest; State->PendingMetadata = B.Slots[Latest];
        }
    }
    if (!State->Pending)
    {
        HRESULT Hr = State->Allocator->Reset();
        if (FAILED(Hr)) { State->Map.Error = Hr; State->ReleaseSlot(); return Result::Error; }
        Hr = State->List->Reset(State->Allocator.Get(), nullptr);
        if (FAILED(Hr)) { State->Map.Error = Hr; State->ReleaseSlot(); return Result::Error; }
        auto* Shared = State->Shared[State->PendingSlot].Get();
        const D3D12_RESOURCE_BARRIER Start[] = {
            GpuDetail::Barrier(Shared, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
            GpuDetail::Barrier(State->PrivateTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST)};
        State->List->ResourceBarrier(2, Start);
        State->List->CopyResource(State->PrivateTexture.Get(), Shared); // Actual GPU-to-GPU copy across processes.
        const D3D12_RESOURCE_BARRIER Copied[] = {
            GpuDetail::Barrier(Shared, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
            GpuDetail::Barrier(State->PrivateTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE)};
        State->List->ResourceBarrier(2, Copied);
        D3D12_TEXTURE_COPY_LOCATION Source{}; Source.pResource = State->PrivateTexture.Get(); Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION Destination{}; Destination.pResource = State->Readback.Get(); Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        Destination.PlacedFootprint = State->Footprint;
        State->List->CopyTextureRegion(&Destination, 0, 0, 0, &Source, nullptr);
        const auto End = GpuDetail::Barrier(State->PrivateTexture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        State->List->ResourceBarrier(1, &End);
        Hr = State->List->Close();
        if (FAILED(Hr)) { State->Map.Error = Hr; State->ReleaseSlot(); return Result::Error; }
        ID3D12CommandList* Lists[] = {State->List.Get()}; State->Queue->ExecuteCommandLists(1, Lists);
        State->Pending = true;
        Hr = State->Queue->Signal(State->Done.Get(), ++State->DoneValue);
        if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
    }
    if (State->Done->GetCompletedValue() < State->DoneValue)
    {
        const HRESULT Hr = State->Done->SetEventOnCompletion(State->DoneValue, State->Event.Get());
        if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
        if (WaitForSingleObject(State->Event.Get(), TimeoutMs) != WAIT_OBJECT_0) return Result::Busy;
    }
    if (FAILED(State->Device->GetDeviceRemovedReason())) { State->Map.Error = State->Device->GetDeviceRemovedReason(); return Result::Error; }
    void* Pixels = nullptr;
    const D3D12_RANGE ReadRange{0, static_cast<SIZE_T>(State->ReadbackSize)};
    const HRESULT Hr = State->Readback->Map(0, &ReadRange, &Pixels);
    if (FAILED(Hr)) { State->Map.Error = Hr; return Result::Error; }
    Frame.Width = BeautyWidth; Frame.Height = BeautyHeight; Frame.Session = State->Session;
    Frame.Sequence = State->PendingMetadata.Sequence; Frame.Camera = State->PendingMetadata.Camera;
    Frame.Rgba.resize(static_cast<std::size_t>(BeautyWidth) * BeautyHeight * 4);
    Frame.Checksum = 14695981039346656037ull; Frame.ColoredPixels = 0;
    for (std::uint32_t Y = 0; Y < BeautyHeight; ++Y)
    {
        auto* Row = Frame.Rgba.data() + static_cast<std::size_t>(Y) * BeautyWidth * 4;
        const auto* Source = static_cast<const std::uint8_t*>(Pixels) + State->Footprint.Offset + static_cast<std::size_t>(Y) * State->Footprint.Footprint.RowPitch;
        std::memcpy(Row, Source, BeautyWidth * 4);
        for (std::uint32_t X = 0; X < BeautyWidth; ++X)
        {
            auto* Pixel = Row + X * 4; Pixel[3] = 255; // Beauty only; no alpha semantics yet.
            Frame.ColoredPixels += Pixel[0] || Pixel[1] || Pixel[2];
            for (int C = 0; C < 3; ++C) { Frame.Checksum ^= Pixel[C]; Frame.Checksum *= 1099511628211ull; }
        }
    }
    const D3D12_RANGE NoWrites{0, 0}; State->Readback->Unmap(0, &NoWrites);
    State->LastSequence = Frame.Sequence; State->Pending = false; State->ReleaseSlot();
    return Result::Ok;
}
}
