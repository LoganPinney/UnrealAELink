#include "UnrealAELink/Transport.h"

#ifdef UNREAL_AE_LINK_WITH_UNREAL
#include "Windows/WindowsHWrapper.h"
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <cstring>
#include <new>

namespace UnrealAELink
{
namespace
{
class Handle
{
public:
    ~Handle() { Reset(); }
    Handle() = default;
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
    Lock(HANDLE Mutex, DWORD Timeout) : Value(Mutex), Wait(WaitForSingleObject(Mutex, Timeout)) {}
    ~Lock() { if (Acquired()) ReleaseMutex(Value); }
    bool Acquired() const { return Wait == WAIT_OBJECT_0 || Wait == WAIT_ABANDONED; }
    bool Abandoned() const { return Wait == WAIT_ABANDONED; }
private:
    HANDLE Value;
    DWORD Wait;
};

struct Mapping
{
    Handle Guard;
    Handle Memory;
    SharedBlock* Block = nullptr;
    DWORD Error = 0;
    ~Mapping() { Reset(); }
    void Reset()
    {
        if (Block) UnmapViewOfFile(Block);
        Block = nullptr;
        Memory.Reset();
        Guard.Reset();
    }
    Result Map(bool Create)
    {
        Reset();
        Guard.Reset(Create ? CreateMutexW(nullptr, FALSE, GuardName)
                           : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, GuardName));
        if (!Guard.Get()) { Error = GetLastError(); return Create ? Result::Error : Result::NoProducer; }
        Memory.Reset(Create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                                0, sizeof(SharedBlock), MappingName)
                            : OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, MappingName));
        if (!Memory.Get()) { Error = GetLastError(); Reset(); return Create ? Result::Error : Result::NoProducer; }
        Block = static_cast<SharedBlock*>(MapViewOfFile(Memory.Get(), FILE_MAP_READ | FILE_MAP_WRITE,
                                                       0, 0, sizeof(SharedBlock)));
        if (!Block) { Error = GetLastError(); Reset(); return Result::Error; }
        Error = 0;
        return Result::Ok;
    }
};

bool Valid(const SharedBlock& B)
{
    return B.Magic == ProtocolMagic && B.Version == ProtocolVersion &&
           B.HeaderBytes == offsetof(SharedBlock, Frame) && B.BlockBytes == sizeof(SharedBlock);
}

bool Fresh(std::uint64_t Now, std::uint64_t Heartbeat)
{
    return Now >= Heartbeat && Now - Heartbeat <= LeaseTimeoutMs;
}
}

struct Producer::Impl
{
    Mapping Map;
    Handle Owner;
    bool OwnsMutex = false;
    bool Initialized = false;
};
struct Consumer::Impl
{
    Mapping Map;
    std::uint64_t Session = 0;
};

const char* ResultName(Result Value) noexcept
{
    switch (Value)
    {
    case Result::Ok: return "OK";
    case Result::NoProducer: return "No producer";
    case Result::Busy: return "Busy (mutex or single-instance lease)";
    case Result::NoFrame: return "No valid camera frame yet";
    case Result::Stale: return "Producer stopped or heartbeat expired";
    case Result::Incompatible: return "Incompatible protocol";
    case Result::Abandoned: return "Abandoned mutex; discarded possibly torn frame";
    default: return "Windows IPC error";
    }
}

Producer::Producer() : State(new (std::nothrow) Impl) {}
Producer::~Producer() { Close(); }
std::uint32_t Producer::LastError() const noexcept { return State ? State->Map.Error : ERROR_NOT_ENOUGH_MEMORY; }

Result Producer::Open()
{
    if (!State) return Result::Error;
    Close();
    State->Owner.Reset(CreateMutexW(nullptr, FALSE, OwnerName));
    if (!State->Owner.Get()) { State->Map.Error = GetLastError(); return Result::Error; }
    const DWORD Wait = WaitForSingleObject(State->Owner.Get(), 0);
    if (Wait != WAIT_OBJECT_0 && Wait != WAIT_ABANDONED) { State->Owner.Reset(); return Result::Busy; }
    State->OwnsMutex = true;
    const Result Mapped = State->Map.Map(true);
    if (Mapped != Result::Ok) { Close(); return Mapped; }
    // Bounded startup wait. Never stall Unreal indefinitely for a receiver.
    const Result Initialized = [&]() -> Result
    {
    Lock Guard(State->Map.Guard.Get(), 100);
    if (!Guard.Acquired())
    {
        return Result::Busy;
    }
    auto& B = *State->Map.Block;
    // Windows mutexes are recursive for the owning thread. Refuse a second
    // Producer object on that thread as well as a second process.
    if (Valid(B) && B.ProducerActive && B.ProducerPid == GetCurrentProcessId())
    {
        return Result::Busy;
    }
    std::memset(&B, 0, sizeof(B));
    B.Magic = ProtocolMagic;
    B.Version = ProtocolVersion;
    B.HeaderBytes = offsetof(SharedBlock, Frame);
    B.BlockBytes = sizeof(SharedBlock);
    LARGE_INTEGER Counter{};
    QueryPerformanceCounter(&Counter);
    B.SessionId = static_cast<std::uint64_t>(Counter.QuadPart);
    B.ProducerPid = GetCurrentProcessId();
    B.ProducerActive = 1;
    B.ProducerHeartbeatMs = GetTickCount64();
    State->Initialized = true;
    return Result::Ok;
    }();
    if (Initialized != Result::Ok) Close();
    return Initialized;
}

void Producer::Close() noexcept
{
    if (!State) return;
    if (State->Initialized && State->Map.Block)
    {
        Lock Guard(State->Map.Guard.Get(), 20);
        if (Guard.Acquired())
        {
            State->Map.Block->ProducerActive = 0;
            State->Map.Block->Frame.Flags = 0;
        }
    }
    State->Map.Reset();
    State->Initialized = false;
    if (State->OwnsMutex) ReleaseMutex(State->Owner.Get());
    State->OwnsMutex = false;
    State->Owner.Reset();
}

Result Producer::Publish(const FrameMetadata& Frame, bool& ReceiverConnected)
{
    if (!State || !State->Initialized || !State->Map.Block) return Result::NoProducer;
    Lock Guard(State->Map.Guard.Get(), 0); // Skip a sample rather than block the game thread.
    if (!Guard.Acquired()) return Result::Busy;
    auto& B = *State->Map.Block;
    const auto Now = GetTickCount64();
    // Recovery after a consumer dies while holding the mutex: the producer replaces the frame.
    if (Guard.Abandoned()) { B.ReceiverPid = 0; B.ReceiverHeartbeatMs = 0; }
    B.Frame = Frame;
    B.Frame.StructBytes = sizeof(FrameMetadata);
    B.Frame.Version = ProtocolVersion;
    B.Frame.PublishedTickMs = Now;
    B.ProducerActive = 1;
    B.ProducerHeartbeatMs = Now;
    ++B.Sequence;
    ReceiverConnected = B.ReceiverPid != 0 && Fresh(Now, B.ReceiverHeartbeatMs);
    return Result::Ok;
}

Consumer::Consumer() : State(new (std::nothrow) Impl) {}
Consumer::~Consumer() { Close(); }
std::uint32_t Consumer::LastError() const noexcept { return State ? State->Map.Error : ERROR_NOT_ENOUGH_MEMORY; }

Result Consumer::Open()
{
    if (!State) return Result::Error;
    Close();
    return State->Map.Map(false);
}

void Consumer::Close() noexcept
{
    if (!State) return;
    if (State->Map.Block)
    {
        Lock Guard(State->Map.Guard.Get(), 20);
        if (Guard.Acquired() && Valid(*State->Map.Block))
        {
            auto& B = *State->Map.Block;
            if (B.SessionId == State->Session && B.ReceiverPid == GetCurrentProcessId())
            { B.ReceiverPid = 0; B.ReceiverHeartbeatMs = 0; }
        }
    }
    State->Session = 0;
    State->Map.Reset();
}

Result Consumer::Read(FrameMetadata& Frame, std::uint64_t& Sequence, std::uint64_t& Session)
{
    if (!State || !State->Map.Block) return Result::NoProducer;
    Lock Guard(State->Map.Guard.Get(), 5);
    if (!Guard.Acquired()) return Result::Busy;
    auto& B = *State->Map.Block;
    // A process may have died midway through a write. Never trust that sample.
    if (Guard.Abandoned())
    { B.Frame.Flags = 0; B.ProducerActive = 0; return Result::Abandoned; }
    if (!Valid(B)) return Result::Incompatible;
    const auto Now = GetTickCount64();
    if (!B.ProducerActive || !Fresh(Now, B.ProducerHeartbeatMs)) return Result::Stale;
    const auto Pid = GetCurrentProcessId();
    if (B.ReceiverPid && B.ReceiverPid != Pid && Fresh(Now, B.ReceiverHeartbeatMs)) return Result::Busy;
    B.ReceiverPid = Pid;
    B.ReceiverHeartbeatMs = Now;
    State->Session = B.SessionId;
    if (!B.Sequence || !(B.Frame.Flags & CameraValid)) return Result::NoFrame;
    if (B.Frame.StructBytes != sizeof(FrameMetadata) || B.Frame.Version != ProtocolVersion) return Result::Incompatible;
    Frame = B.Frame;
    Sequence = B.Sequence;
    Session = B.SessionId;
    return Result::Ok;
}
}
