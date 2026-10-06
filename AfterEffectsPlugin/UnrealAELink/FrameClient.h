#pragma once
#include "UnrealAELink/GpuTransport.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace UnrealAELink
{
// One GPU consumer per Adobe process; instances share immutable completed frames.
// Worker never calls Adobe or Unreal. Host callbacks never wait for GPU work.
class FrameClient
{
public:
    FrameClient();
    ~FrameClient();
    std::uint64_t Register(std::uintptr_t Owner);
    void Remove(std::uint64_t Id);
    std::shared_ptr<const BeautyFrame> Snapshot(std::uint64_t Id, bool Connected, bool Live);
    Result Status() const;
private:
    struct Instance { std::uintptr_t Owner = 0; bool Connected = false, Live = true; std::shared_ptr<const BeautyFrame> Frozen; };
    void UpdateActive();
    void Run();
    mutable std::mutex Mutex;
    std::condition_variable Wake;
    std::unordered_map<std::uint64_t, Instance> Instances;
    std::shared_ptr<const BeautyFrame> Latest;
    bool Stopping = false, Active = false;
    std::uint64_t NextId = 0;
    Result CurrentStatus = Result::NoProducer;
    std::thread Worker; // Start only after every field used by Run is initialized.
};
}
