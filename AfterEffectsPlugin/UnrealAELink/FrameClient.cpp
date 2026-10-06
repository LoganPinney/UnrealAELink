#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "FrameClient.h"
#include <chrono>
#include <fstream>
#include <filesystem>

namespace UnrealAELink
{
namespace
{
void LogRequest(const char* Event, const FrameRequest& R, std::uint64_t Beauty = 0, FrameStatus Status = FrameStatus::Pending)
{
    wchar_t Directory[1024]{};
    const auto Count = GetEnvironmentVariableW(L"UNREAL_AE_LINK_DIAGNOSTIC_DIR", Directory, 1024);
    if (!Count || Count >= 1024) return;
    std::ofstream Log(std::filesystem::path(Directory) / "ae-request.log", std::ios::app);
    Log << "AE " << Event << " id=" << R.RequestId << " time=" << R.TimeValue << '/' << R.TimeScale
        << " beauty=" << Beauty << " status=" << int(Status) << '\n';
}
}
FrameClient::FrameClient() : Worker([this] { Run(); }) {}
FrameClient::~FrameClient()
{
    { std::lock_guard<std::mutex> Lock(Mutex); Stopping = true; }
    Wake.notify_all();
    if (Worker.joinable()) Worker.join();
}
std::uint64_t FrameClient::Register(std::uintptr_t Owner)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    // Resetup of the same host instance replaces only its previous registration.
    for (auto It = Instances.begin(); It != Instances.end(); )
        if (It->second.Owner == Owner) It = Instances.erase(It); else ++It;
    const auto Id = ++NextId; Instances.emplace(Id, Instance{Owner}); UpdateActive(); return Id;
}
void FrameClient::UpdateActive()
{
    Active = false;
    for (const auto& Pair : Instances) Active |= Pair.second.Connected && Pair.second.Live;
    Wake.notify_all();
}
void FrameClient::Remove(std::uint64_t Id)
{
    std::lock_guard<std::mutex> Lock(Mutex); Instances.erase(Id); UpdateActive();
}
std::shared_ptr<const BeautyFrame> FrameClient::Snapshot(std::uint64_t Id, bool Connected, bool Live)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    const auto Found = Instances.find(Id); if (Found == Instances.end()) return {};
    auto& I = Found->second;
    if (!Connected) I.Frozen.reset();
    if (Connected && !Live && (I.Live || !I.Frozen)) I.Frozen = Latest;
    I.Connected = Connected; I.Live = Live; UpdateActive();
    if (!Connected) return {};
    if (!Live) return I.Frozen;
    const auto Now = GetTickCount64();
    if (Latest && Now >= Latest->Camera.PublishedTickMs && Now - Latest->Camera.PublishedTickMs <= LeaseTimeoutMs)
    { I.Frozen = Latest; return Latest; }
    return {};
}
Result FrameClient::Status() const { std::lock_guard<std::mutex> Lock(Mutex); return CurrentStatus; }
std::shared_ptr<const BeautyFrame> FrameClient::RequestFrame(std::uint64_t Id, std::int64_t TimeValue,
    std::int64_t TimeScale, FrameStatus& Status, const std::function<bool()>& Abort)
{
    std::unique_lock<std::mutex> Lock(Mutex);
    Status = FrameStatus::Cancelled;
    const auto Found = Instances.find(Id);
    if (Stopping || Found == Instances.end()) return {};
    if (TimeValue < 0 || TimeScale <= 0) { Status = FrameStatus::InvalidTime; return {}; }
    Found->second.Connected = true; Found->second.Live = false; UpdateActive();
    std::shared_ptr<Job> Pending;
    for (const auto& J : Jobs)
        if (!J->Cancel && J->Instance == Id && J->Request.TimeValue == TimeValue && J->Request.TimeScale == TimeScale) { Pending = J; break; }
    if (!Pending)
    {
        Pending = std::make_shared<Job>(); Pending->Instance = Id;
        if (!NextRequestId) { LARGE_INTEGER Counter{}; QueryPerformanceCounter(&Counter); NextRequestId = static_cast<std::uint64_t>(Counter.QuadPart); }
        Pending->Request.RequestId = ++NextRequestId; Pending->Request.TimeValue = TimeValue; Pending->Request.TimeScale = TimeScale;
        Pending->Deadline = GetTickCount64() + RequestTimeoutMs;
        Jobs.push_back(Pending); Wake.notify_all();
    }
    while (!Stopping && !Pending->Done)
    {
        Wake.wait_for(Lock, std::chrono::milliseconds(20));
        Lock.unlock(); const bool Cancelled = Abort && Abort(); Lock.lock();
        if (Cancelled || GetTickCount64() >= Pending->Deadline)
        {
            Pending->Cancel = true; Status = Cancelled ? FrameStatus::Cancelled : FrameStatus::Timeout;
            LogRequest(Cancelled ? "CANCEL" : "TIMEOUT", Pending->Request, 0, Status); Wake.notify_all(); return {};
        }
    }
    Status = Stopping ? FrameStatus::Cancelled : Pending->Status;
    return Status == FrameStatus::Success ? Pending->Image : nullptr;
}
void FrameClient::Run()
{
    GpuConsumer Consumer; RequestChannel Requests; bool Open = false;
    try
    {
        for (;;)
        {
            {
                std::unique_lock<std::mutex> Lock(Mutex);
                if (Stopping) break;
                if (!Active && Jobs.empty())
                {
                    Lock.unlock(); Consumer.Close(); Open = false; Lock.lock();
                    CurrentStatus = Result::NoProducer;
                    Wake.wait(Lock, [this] { return Stopping || Active || !Jobs.empty(); });
                    if (Stopping) break;
                }
            }
            std::shared_ptr<Job> Pending;
            { std::lock_guard<std::mutex> Lock(Mutex); if (!Jobs.empty()) Pending = Jobs.front(); }
            if (Pending)
            {
                FrameResponse Response{}; bool Submitted = false, Responded = false, ChannelOpen = false;
                FrameStatus Status = FrameStatus::Timeout;
                std::shared_ptr<const BeautyFrame> Image;
                LogRequest("REQUEST", Pending->Request);
                while (GetTickCount64() < Pending->Deadline)
                {
                    { std::lock_guard<std::mutex> Lock(Mutex); if (Stopping || Pending->Cancel) { Status = FrameStatus::Cancelled; break; } }
                    if (!Open) Open = Consumer.Open() == Result::Ok;
                    if (!ChannelOpen) ChannelOpen = Requests.Open(false) == Result::Ok;
                    if (Open && ChannelOpen && !Submitted)
                    {
                        const auto Code = Requests.Submit(Pending->Request);
                        Submitted = Code == Result::Ok;
                        if (Code != Result::Ok && Code != Result::Busy) { Status = FrameStatus::CaptureFailed; break; }
                    }
                    if (Submitted)
                    {
                        const auto Code = Requests.Receive(Pending->Request.RequestId, Response); // renew lease throughout wait
                        if (Code != Result::Ok && Code != Result::NoFrame && Code != Result::Busy) { Status = FrameStatus::CaptureFailed; break; }
                        if (Code == Result::Ok)
                        {
                            Responded = true;
                            if (Response.RequestedTimeValue != Pending->Request.TimeValue || Response.RequestedTimeScale != Pending->Request.TimeScale)
                            { Status = FrameStatus::CaptureFailed; break; }
                            if (Response.Status != FrameStatus::Success) { Status = Response.Status; break; }
                        }
                        auto Frame = std::make_shared<BeautyFrame>();
                        const auto GpuCode = Consumer.Read(*Frame, 10, Pending->Request.RequestId);
                        if (GpuCode == Result::Ok && Frame->Identity.RequestId == Pending->Request.RequestId)
                        {
                            if (Frame->Identity.TimeValue != Pending->Request.TimeValue || Frame->Identity.TimeScale != Pending->Request.TimeScale)
                            { Status = FrameStatus::CaptureFailed; break; }
                            Image = Frame;
                        }
                        else if (GpuCode != Result::Ok && GpuCode != Result::Busy && GpuCode != Result::NoFrame)
                        { Status = FrameStatus::CaptureFailed; Consumer.Close(); Open = false; break; }
                        if (Responded && Image)
                        {
                            Status = Image->Session == Response.BeautySession && Image->Sequence == Response.BeautySequence ? FrameStatus::Success : FrameStatus::CaptureFailed;
                            break;
                        }
                    }
                    std::unique_lock<std::mutex> Lock(Mutex); Wake.wait_for(Lock, std::chrono::milliseconds(5));
                }
                // Close also releases a timed-out/cancelled mailbox; late results
                // cannot satisfy another id. GPU in-flight retirement stays intact.
                Requests.Close();
                LogRequest(Status == FrameStatus::Timeout ? "TIMEOUT" : "RESPONSE", Pending->Request, Image ? Image->Sequence : 0, Status);
                {
                    std::lock_guard<std::mutex> Lock(Mutex);
                    Pending->Image = Status == FrameStatus::Success ? Image : nullptr;
                    Pending->Status = Status; Pending->Done = true; Jobs.pop_front();
                    CurrentStatus = Status == FrameStatus::Success ? Result::Ok : Result::Error;
                }
                Wake.notify_all(); continue;
            }
            auto Code = Result::Ok;
            if (!Open) { Code = Consumer.Open(); Open = Code == Result::Ok; }
            auto Frame = std::make_shared<BeautyFrame>();
            if (Open) Code = Consumer.Read(*Frame, 10);
            {
                std::lock_guard<std::mutex> Lock(Mutex);
                CurrentStatus = Code;
                if (Code == Result::Ok) Latest = std::move(Frame);
                else if (Code != Result::Busy && Code != Result::NoFrame) Latest.reset();
            }
            if (Code != Result::Ok && Code != Result::Busy && Code != Result::NoFrame)
            { Consumer.Close(); Open = false; }
            std::unique_lock<std::mutex> Lock(Mutex);
            Wake.wait_for(Lock, std::chrono::milliseconds(Open ? 16 : 200), [this] { return Stopping || !Active || !Jobs.empty(); });
        }
    }
    catch (...)
    {
        std::lock_guard<std::mutex> Lock(Mutex); CurrentStatus = Result::Error; Latest.reset();
        for (auto& J : Jobs) { J->Status = FrameStatus::CaptureFailed; J->Done = true; }
        Wake.notify_all();
    }
    Consumer.Close();
}
}
