#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealAELink/GpuTransport.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UWorld;
struct FBeautyRenderState;

class FBeautyCapture
{
public:
    FBeautyCapture();
    ~FBeautyCapture();
    bool Tick(UWorld* World, const UnrealAELink::FrameMetadata& Camera, const UnrealAELink::FrameIdentity& Identity = {});
    // 1 in-flight, 2 GPU complete, 3 failed, 4 retry reservation. Game thread only.
    int PollRequest(uint64& Session, uint64& Sequence);
    void Stop();
private:
    TStrongObjectPtr<USceneCaptureComponent2D> Capture;
    TStrongObjectPtr<UTextureRenderTarget2D> Target;
    TSharedPtr<FBeautyRenderState, ESPMode::ThreadSafe> RenderState;
    double LastCapture = 0;
    bool bWarned = false;
};
