#pragma once

#include "UnrealAELink/Protocol.h"
#include "UnrealAELink/RequestProtocol.h"

namespace UnrealAELink
{
inline constexpr std::uint32_t GpuProtocolVersion = 2;
inline constexpr std::uint32_t GpuProtocolMagic = 0x47504155; // UAPG
inline constexpr std::uint32_t BeautyWidth = 1280;
inline constexpr std::uint32_t BeautyHeight = 720;
inline constexpr std::uint32_t BeautyFormat = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
inline constexpr std::uint32_t GpuSlotCount = 3;
inline constexpr std::uint32_t GpuNameLength = 96;
inline constexpr wchar_t GpuMappingName[] = L"Local\\UnrealAELink.Beauty.v2";
inline constexpr wchar_t GpuGuardName[] = L"Local\\UnrealAELink.Beauty.Guard.v2";
inline constexpr wchar_t GpuOwnerName[] = L"Local\\UnrealAELink.Beauty.Owner.v2";

enum class GpuSlotState : std::uint32_t { Free, Writing, Ready, Reading };
struct alignas(8) GpuSlot
{
    GpuSlotState State;
    std::uint32_t ReaderPid;
    std::uint64_t FenceValue;
    std::uint64_t Sequence;
    FrameMetadata Camera;
    FrameIdentity Identity;
};

struct alignas(8) GpuBlock
{
    std::uint32_t Magic;
    std::uint32_t Version;
    std::uint32_t HeaderBytes;
    std::uint32_t BlockBytes;
    std::uint64_t Session;
    std::uint32_t ProducerPid;
    std::uint32_t ProducerActive;
    std::uint64_t ProducerHeartbeatMs;
    std::uint32_t AdapterLuidLow;
    std::int32_t AdapterLuidHigh;
    std::uint32_t Width;
    std::uint32_t Height;
    std::uint32_t Format;
    std::uint32_t SlotCount;
    std::uint32_t ReceiverPid;
    std::uint32_t Reserved;
    std::uint64_t ReceiverHeartbeatMs;
    wchar_t TextureNames[GpuSlotCount][GpuNameLength];
    wchar_t FenceName[GpuNameLength];
    GpuSlot Slots[GpuSlotCount];
};

static_assert(sizeof(wchar_t) == 2, "Windows UTF-16 names required");
static_assert(sizeof(GpuSlot) == 176);
static_assert(offsetof(GpuBlock, Slots) == 848);
static_assert(sizeof(GpuBlock) == 1376);
static_assert(std::is_trivially_copyable_v<GpuBlock>);
}
