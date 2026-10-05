#pragma once

#include "UnrealAELink/Protocol.h"
#include <memory>

namespace UnrealAELink
{
enum class Result { Ok, NoProducer, Busy, NoFrame, Stale, Incompatible, Abandoned, Error };
const char* ResultName(Result Value) noexcept;

// Each object is used from ONE thread. No callbacks, exceptions, or host dependencies.
class Producer
{
public:
    Producer();
    ~Producer();
    Producer(const Producer&) = delete;
    Producer& operator=(const Producer&) = delete;
    Result Open();
    void Close() noexcept;
    Result Publish(const FrameMetadata& Frame, bool& ReceiverConnected);
    std::uint32_t LastError() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> State;
};

class Consumer
{
public:
    Consumer();
    ~Consumer();
    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;
    Result Open();
    void Close() noexcept;
    Result Read(FrameMetadata& Frame, std::uint64_t& Sequence, std::uint64_t& Session);
    std::uint32_t LastError() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> State;
};
}
