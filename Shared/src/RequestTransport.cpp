#include "UnrealAELink/RequestProtocol.h"
#ifdef UNREAL_AE_LINK_WITH_UNREAL
#include "Windows/WindowsHWrapper.h"
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace UnrealAELink
{
namespace RequestDetail
{
constexpr std::uint32_t Magic = 0x52504155;
struct Lock
{
    HANDLE Handle; DWORD Code;
    explicit Lock(HANDLE H) : Handle(H), Code(WaitForSingleObject(H, 5)) {}
    ~Lock() { if (Acquired()) ReleaseMutex(Handle); }
    bool Acquired() const { return Code == WAIT_OBJECT_0 || Code == WAIT_ABANDONED; }
};
bool Valid(const RequestBlock& B) { return B.Magic == Magic && B.Version == RequestVersion && B.BlockBytes == sizeof(B); }
bool Fresh(std::uint64_t Time) { auto Now = GetTickCount64(); return Now >= Time && Now - Time <= LeaseTimeoutMs; }
}
struct RequestChannel::Impl
{
    HANDLE Mapping = nullptr, Guard = nullptr, Owner = nullptr;
    RequestBlock* Block = nullptr;
    std::uint64_t Session = 0;
    bool Server = false, Owns = false;
    Result Check(const RequestDetail::Lock& Lock)
    {
        if (!Lock.Acquired()) return Result::Busy;
        if (Lock.Code == WAIT_ABANDONED) { Block->ProducerActive = 0; return Result::Abandoned; }
        if (!RequestDetail::Valid(*Block)) return Result::Incompatible;
        if (Block->Session != Session || !Block->ProducerActive) return Result::Stale;
        if (Server) Block->ProducerHeartbeatMs = GetTickCount64();
        else
        {
            if (!RequestDetail::Fresh(Block->ProducerHeartbeatMs)) return Result::Stale;
            if (Block->ClientPid && Block->ClientPid != GetCurrentProcessId() && RequestDetail::Fresh(Block->ClientHeartbeatMs)) return Result::Busy;
            Block->ClientPid = GetCurrentProcessId(); Block->ClientHeartbeatMs = GetTickCount64();
        }
        return Result::Ok;
    }
};
RequestChannel::RequestChannel() : Data(new Impl) {}
RequestChannel::~RequestChannel() { Close(); }
Result RequestChannel::Open(bool Server)
{
    Close(); Data->Server = Server;
    if (Server)
    {
        Data->Owner = CreateMutexW(nullptr, 0, RequestOwnerName);
        if (!Data->Owner) return Result::Error;
        const auto Wait = WaitForSingleObject(Data->Owner, 0);
        if (Wait != WAIT_OBJECT_0 && Wait != WAIT_ABANDONED) { Close(); return Result::Busy; }
        Data->Owns = true;
    }
    Data->Guard = Server ? CreateMutexW(nullptr, 0, RequestGuardName) : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, 0, RequestGuardName);
    Data->Mapping = Server ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(RequestBlock), RequestMappingName)
        : OpenFileMappingW(FILE_MAP_ALL_ACCESS, 0, RequestMappingName);
    if (!Data->Guard || !Data->Mapping) { Close(); return Result::NoProducer; }
    Data->Block = static_cast<RequestBlock*>(MapViewOfFile(Data->Mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RequestBlock)));
    if (!Data->Block) { Close(); return Result::Error; }
    RequestDetail::Lock Lock(Data->Guard);
    if (!Lock.Acquired()) return Result::Busy;
    auto& B = *Data->Block;
    if (Server)
    {
        if (RequestDetail::Valid(B) && B.ProducerActive && B.ProducerPid == GetCurrentProcessId()) return Result::Busy;
        LARGE_INTEGER Counter{}; QueryPerformanceCounter(&Counter);
        B = RequestBlock{}; B.Magic = RequestDetail::Magic; B.Version = RequestVersion; B.BlockBytes = sizeof(B);
        B.Session = static_cast<std::uint64_t>(Counter.QuadPart); B.ProducerPid = GetCurrentProcessId();
        B.ProducerActive = 1; B.ProducerHeartbeatMs = GetTickCount64();
    }
    Data->Session = B.Session;
    return Data->Check(Lock);
}
void RequestChannel::Close()
{
    if (Data->Block)
    {
        RequestDetail::Lock Lock(Data->Guard);
        if (Lock.Acquired() && Data->Block->Session == Data->Session)
        {
            if (Data->Server && Data->Owns) Data->Block->ProducerActive = 0;
            else if (!Data->Server && Data->Block->ClientPid == GetCurrentProcessId())
            { Data->Block->ClientPid = 0; Data->Block->State = RequestState::Idle; }
        }
    }
    if (Data->Block) UnmapViewOfFile(Data->Block);
    if (Data->Mapping) CloseHandle(Data->Mapping);
    if (Data->Guard) CloseHandle(Data->Guard);
    if (Data->Owns) ReleaseMutex(Data->Owner);
    if (Data->Owner) CloseHandle(Data->Owner);
    *Data = Impl{};
}
Result RequestChannel::Poll(FrameRequest& Request, bool& Deterministic)
{
    Deterministic = false;
    if (!Data->Block || !Data->Server) return Result::NoProducer;
    RequestDetail::Lock Lock(Data->Guard); const auto Code = Data->Check(Lock); if (Code != Result::Ok) return Code;
    auto& B = *Data->Block;
    if (!RequestDetail::Fresh(B.ClientHeartbeatMs)) { B.State = RequestState::Idle; B.ClientPid = 0; }
    Deterministic = B.ClientPid && B.State != RequestState::Idle;
    if (B.State != RequestState::Requested) return Result::NoFrame;
    Request = B.Request; B.State = RequestState::Evaluating;
    return Result::Ok;
}
Result RequestChannel::Submit(const FrameRequest& Request)
{
    if (!Data->Block || Data->Server) return Result::NoProducer;
    if (!Request.RequestId || Request.StructBytes != sizeof(Request) || Request.Version != RequestVersion || Request.TimeScale <= 0) return Result::Incompatible;
    RequestDetail::Lock Lock(Data->Guard); const auto Code = Data->Check(Lock); if (Code != Result::Ok) return Code;
    auto& B = *Data->Block;
    if (B.State != RequestState::Idle) return Result::Busy;
    B.Request = Request; B.Response = FrameResponse{}; B.State = RequestState::Requested;
    return Result::Ok;
}
Result RequestChannel::Receive(std::uint64_t Id, FrameResponse& Response)
{
    if (!Data->Block || Data->Server) return Result::NoProducer;
    RequestDetail::Lock Lock(Data->Guard); const auto Code = Data->Check(Lock); if (Code != Result::Ok) return Code;
    const auto& B = *Data->Block;
    if (B.Request.RequestId != Id || B.State != RequestState::Complete) return Result::NoFrame;
    Response = B.Response;
    return Response.StructBytes == sizeof(Response) && Response.Version == RequestVersion && Response.RequestId == Id ? Result::Ok : Result::Incompatible;
}
Result RequestChannel::Complete(const FrameResponse& Response)
{
    if (!Data->Block || !Data->Server) return Result::NoProducer;
    RequestDetail::Lock Lock(Data->Guard); const auto Code = Data->Check(Lock); if (Code != Result::Ok) return Code;
    if (Response.Status == FrameStatus::Pending) return Result::NoFrame; // heartbeat only; never acknowledge unfinished GPU work
    auto& B = *Data->Block;
    if (B.State != RequestState::Evaluating || B.Request.RequestId != Response.RequestId) return Result::NoFrame;
    if (B.Request.TimeValue != Response.RequestedTimeValue || B.Request.TimeScale != Response.RequestedTimeScale) return Result::Incompatible;
    B.Response = Response; B.State = RequestState::Complete; return Result::Ok;
}
Result RequestChannel::Release(std::uint64_t Id)
{
    if (!Data->Block || Data->Server) return Result::NoProducer;
    RequestDetail::Lock Lock(Data->Guard); const auto Code = Data->Check(Lock); if (Code != Result::Ok) return Code;
    if (Data->Block->Request.RequestId != Id) return Result::NoFrame;
    Data->Block->State = RequestState::Idle; return Result::Ok;
}
Result RequestChannel::Heartbeat()
{
    if (!Data->Block) return Result::NoProducer;
    RequestDetail::Lock Lock(Data->Guard); return Data->Check(Lock);
}
}
