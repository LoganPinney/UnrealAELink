#pragma once

#include "UnrealAELink/GpuProtocol.h"
#include "UnrealAELink/Transport.h"
#include <vector>

struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12Fence;

namespace UnrealAELink
{
// Producer: render-thread use. The host records copy commands and signals Fence()
// AFTER that copy using its own graphics queue. This class never submits UE work.
class GpuProducer
{
public:
    GpuProducer();
    ~GpuProducer();
    GpuProducer(const GpuProducer&) = delete;
    GpuProducer& operator=(const GpuProducer&) = delete;
    Result Open(ID3D12Device* Device);
    void Close() noexcept;
    Result Reserve(std::uint32_t& Slot, std::uint64_t& FenceValue, bool& ReceiverConnected);
    Result Publish(std::uint32_t Slot, std::uint64_t FenceValue, const FrameMetadata& Camera, const FrameIdentity& Identity = {});
    // Even a cancelled reservation must signal its returned fence value. A
    // cancelled slot is reusable only after that signal completes.
    void Cancel(std::uint32_t Slot);
    void Heartbeat();
    bool OwnWritesComplete() const;
    ID3D12Resource* Texture(std::uint32_t Slot) const;
    ID3D12Fence* Fence() const;
    std::uint64_t Session() const;
    std::int32_t LastError() const;
private:
    struct Impl;
    std::unique_ptr<Impl> State;
};

struct BeautyFrame
{
    std::uint64_t Session = 0;
    std::uint64_t Sequence = 0;
    FrameMetadata Camera{};
    FrameIdentity Identity{};
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint64_t Checksum = 0;
    std::uint64_t ColoredPixels = 0;
    std::vector<std::uint8_t> Rgba; // packed rows; opaque Beauty; local readback only
};

// Single-thread use. Acquires a completed shared texture, GPU-copies into private
// resources, and reads packed RGBA locally. Shared pixels never go through disk.
class GpuConsumer
{
public:
    GpuConsumer();
    ~GpuConsumer();
    GpuConsumer(const GpuConsumer&) = delete;
    GpuConsumer& operator=(const GpuConsumer&) = delete;
    Result Open();
    void Close() noexcept;
    Result Read(BeautyFrame& Frame, std::uint32_t TimeoutMs = 100, std::uint64_t RequestId = 0);
    std::int32_t LastError() const;
private:
    struct Impl;
    std::unique_ptr<Impl> State;
};
}
