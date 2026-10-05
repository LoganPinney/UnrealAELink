#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace UnrealAELink
{
inline constexpr std::uint32_t ProtocolVersion = 1;
inline constexpr std::uint32_t ProtocolMagic = 0x4B4C4155; // "UALK", little endian
inline constexpr std::uint64_t LeaseTimeoutMs = 2000;
inline constexpr wchar_t MappingName[] = L"Local\\UnrealAELink.Metadata.v1";
inline constexpr wchar_t GuardName[] = L"Local\\UnrealAELink.Metadata.Guard.v1";
inline constexpr wchar_t OwnerName[] = L"Local\\UnrealAELink.Metadata.Owner.v1";

enum class CameraSource : std::uint32_t { None = 0, EditorViewport = 1, PlayerCamera = 2 };
inline constexpr std::uint32_t CameraValid = 1;

// This is a byte contract, never an Unreal type. Windows x64, little endian only.
struct alignas(8) FrameMetadata
{
    std::uint32_t StructBytes;
    std::uint32_t Version;
    CameraSource Source;
    std::uint32_t Flags;
    std::uint64_t FrameNumber;
    std::uint64_t PublishedTickMs;
    double TimeSeconds;
    double Position[3];
    double Quaternion[4]; // X, Y, Z, W
    float Rotation[3]; // Pitch, Yaw, Roll, degrees
    float FieldOfView; // Unreal view FOV, degrees (see protocol.md)
    float AspectRatio;
    float Scale[3];
};

struct alignas(8) SharedBlock
{
    std::uint32_t Magic;
    std::uint32_t Version;
    std::uint32_t HeaderBytes;
    std::uint32_t BlockBytes;
    std::uint64_t SessionId;
    std::uint32_t ProducerPid;
    std::uint32_t ProducerActive;
    std::uint64_t ProducerHeartbeatMs;
    std::uint32_t ReceiverPid;
    std::uint32_t Reserved;
    std::uint64_t ReceiverHeartbeatMs;
    std::uint64_t Sequence;
    FrameMetadata Frame;
};

static_assert(sizeof(void*) == 8, "Build UnrealAELink for Windows x64");
static_assert(sizeof(FrameMetadata) == 128 && alignof(FrameMetadata) == 8);
static_assert(sizeof(SharedBlock) == 192 && alignof(SharedBlock) == 8);
static_assert(offsetof(SharedBlock, Frame) == 64);
static_assert(offsetof(FrameMetadata, Position) == 40);
static_assert(offsetof(FrameMetadata, Quaternion) == 64);
static_assert(offsetof(FrameMetadata, FieldOfView) == 108);
static_assert(std::is_trivially_copyable_v<SharedBlock>);
}
