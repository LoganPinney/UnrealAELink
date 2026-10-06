#pragma once
#include "UnrealAELink/Transport.h"
#include <limits>
#include <numeric>

namespace UnrealAELink
{
inline constexpr std::uint32_t RequestVersion = 1;
inline constexpr std::uint32_t RequestTimeoutMs = 15000;
inline constexpr wchar_t RequestMappingName[] = L"Local\\UnrealAELink.Request.v1";
inline constexpr wchar_t RequestGuardName[] = L"Local\\UnrealAELink.Request.Guard.v1";
inline constexpr wchar_t RequestOwnerName[] = L"Local\\UnrealAELink.Request.Owner.v1";
enum class FrameStatus : std::int32_t { Pending, Success, InvalidTime, NoSequence, CaptureFailed, Timeout, Cancelled };
enum class RequestState : std::uint32_t { Idle, Requested, Evaluating, Complete };
struct alignas(8) FrameRequest
{
    std::uint32_t StructBytes = sizeof(FrameRequest), Version = RequestVersion;
    std::uint64_t RequestId = 0;
    std::int64_t TimeValue = 0, TimeScale = 1;
    std::uint32_t Width = 1280, Height = 720;
};
struct alignas(8) FrameResponse
{
    std::uint32_t StructBytes = sizeof(FrameResponse), Version = RequestVersion;
    std::uint64_t RequestId = 0;
    std::int64_t RequestedTimeValue = 0, RequestedTimeScale = 1;
    std::uint64_t BeautySession = 0, BeautySequence = 0;
    FrameStatus Status = FrameStatus::Pending;
    std::uint32_t Reserved = 0;
};
struct alignas(8) FrameIdentity
{
    std::uint64_t RequestId = 0; // zero denotes asynchronous Live Beauty
    std::int64_t TimeValue = 0, TimeScale = 0;
};
struct alignas(8) RequestBlock
{
    std::uint32_t Magic = 0, Version = 0, BlockBytes = 0, ProducerActive = 0;
    std::uint64_t Session = 0, ProducerHeartbeatMs = 0;
    std::uint32_t ProducerPid = 0, ClientPid = 0;
    std::uint64_t ClientHeartbeatMs = 0;
    RequestState State = RequestState::Idle;
    std::uint32_t Reserved = 0;
    FrameRequest Request;
    FrameResponse Response;
};
static_assert(sizeof(FrameRequest) == 40 && offsetof(FrameRequest, TimeValue) == 16);
static_assert(sizeof(FrameResponse) == 56 && offsetof(FrameResponse, BeautySession) == 32);
static_assert(sizeof(FrameIdentity) == 24 && sizeof(RequestBlock) == 152);
static_assert(std::is_trivially_copyable_v<RequestBlock>);

// Integer cancellation precedes multiplication. Only the final fractional frame
// is rounded to UE's float subframe; the wire time and identity stay rational.
inline bool RationalToFrame(std::int64_t Value, std::int64_t Scale,
    std::int32_t RateNumerator, std::int32_t RateDenominator, std::int32_t& Frame, float& Subframe)
{
    if (Value < 0 || Scale <= 0 || RateNumerator <= 0 || RateDenominator <= 0) return false;
    auto N = std::int64_t(RateNumerator), D = std::int64_t(RateDenominator);
    auto G = std::gcd(Value, Scale); Value /= G; Scale /= G;
    G = std::gcd(N, Scale); N /= G; Scale /= G;
    G = std::gcd(Value, D); Value /= G; D /= G;
    if (Value > std::numeric_limits<std::int64_t>::max() / N || Scale > std::numeric_limits<std::int64_t>::max() / D) return false;
    const auto Num = Value * N, Den = Scale * D, Whole = Num / Den;
    if (Whole > std::numeric_limits<std::int32_t>::max()) return false;
    Frame = static_cast<std::int32_t>(Whole);
    Subframe = static_cast<float>(static_cast<double>(Num % Den) / static_cast<double>(Den));
    // FFrameTime requires a fractional part strictly below one.
    if (Subframe >= 1.0f) Subframe = 0.99999994f;
    return true;
}
class RequestChannel
{
public:
    RequestChannel();
    ~RequestChannel();
    RequestChannel(const RequestChannel&) = delete;
    RequestChannel& operator=(const RequestChannel&) = delete;
    Result Open(bool Server);
    void Close();
    Result Poll(FrameRequest& Request, bool& Deterministic);
    Result Submit(const FrameRequest& Request);
    Result Receive(std::uint64_t Id, FrameResponse& Response);
    Result Complete(const FrameResponse& Response);
    Result Heartbeat();
    Result Release(std::uint64_t Id);
private:
    struct Impl;
    std::unique_ptr<Impl> Data;
};
}
