#pragma once
#include "UnrealAELink/GpuTransport.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <deque>
#include <functional>

namespace UnrealAELink
{
// One GPU consumer per Adobe process; instances share immutable completed frames.
// Worker never calls Adobe or Unreal. Only deterministic callbacks wait, bounded.
class FrameClient
{
public:
    FrameClient();
    ~FrameClient();
    std::uint64_t Register(std::uintptr_t Owner);
    void Remove(std::uint64_t Id);
    std::shared_ptr<const BeautyFrame> Snapshot(std::uint64_t Id, bool Connected, bool Live);
    Result Status() const;
    std::shared_ptr<const BeautyFrame> RequestFrame(std::uint64_t Id, std::int64_t TimeValue,
        std::int64_t TimeScale, FrameStatus& Status, const std::function<bool()>& Abort = {});
private:
    struct Job
    {
        FrameRequest Request;
        std::uint64_t Instance = 0, Deadline = 0;
        bool Done = false, Cancel = false;
        FrameStatus Status = FrameStatus::Pending;
        std::shared_ptr<const BeautyFrame> Image;
    };
    struct Instance { std::uintptr_t Owner = 0; bool Connected = false, Live = true; std::shared_ptr<const BeautyFrame> Frozen; };
    void UpdateActive();
    void Run();
    mutable std::mutex Mutex;
    std::condition_variable Wake;
    std::unordered_map<std::uint64_t, Instance> Instances;
    std::shared_ptr<const BeautyFrame> Latest;
    std::deque<std::shared_ptr<Job>> Jobs;
    bool Stopping = false, Active = false;
    std::uint64_t NextId = 0;
    std::uint64_t NextRequestId = 0;
    Result CurrentStatus = Result::NoProducer;
    std::thread Worker; // Start only after every field used by Run is initialized.
};
}
