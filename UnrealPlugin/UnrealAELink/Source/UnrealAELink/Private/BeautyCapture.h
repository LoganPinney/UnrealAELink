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
    void Tick(UWorld* World, const UnrealAELink::FrameMetadata& Camera);
    void Stop();
private:
    TStrongObjectPtr<USceneCaptureComponent2D> Capture;
    TStrongObjectPtr<UTextureRenderTarget2D> Target;
    TSharedPtr<FBeautyRenderState, ESPMode::ThreadSafe> RenderState;
    double LastCapture = 0;
    bool bWarned = false;
};
