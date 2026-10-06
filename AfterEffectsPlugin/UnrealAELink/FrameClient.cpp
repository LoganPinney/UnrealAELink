#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "FrameClient.h"
#include <chrono>

namespace UnrealAELink
{
FrameClient::FrameClient() : Worker([this] { Run(); }) {}
FrameClient::~FrameClient()
{
    { std::lock_guard<std::mutex> Lock(Mutex); Stopping = true; }
    Wake.notify_one();
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
    Wake.notify_one();
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
void FrameClient::Run()
{
    GpuConsumer Consumer; bool Open = false;
    try
    {
        for (;;)
        {
            {
                std::unique_lock<std::mutex> Lock(Mutex);
                if (Stopping) break;
                if (!Active)
                {
                    Lock.unlock(); Consumer.Close(); Open = false; Lock.lock();
                    CurrentStatus = Result::NoProducer;
                    Wake.wait(Lock, [this] { return Stopping || Active; });
                    if (Stopping) break;
                }
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
            Wake.wait_for(Lock, std::chrono::milliseconds(Open ? 16 : 200), [this] { return Stopping || !Active; });
        }
    }
    catch (...)
    {
        std::lock_guard<std::mutex> Lock(Mutex); CurrentStatus = Result::Error; Latest.reset();
    }
    Consumer.Close();
}
}
