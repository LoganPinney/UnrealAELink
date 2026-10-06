#include "BeautyCapture.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "ID3D12DynamicRHI.h"
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogUnrealAELinkBeauty, Log, All);

struct FBeautyRenderState
{
    UnrealAELink::GpuProducer Producer;
    FTextureRHIRef Shared[UnrealAELink::GpuSlotCount];
    bool bOpen = false, bConnected = false;
    std::atomic<bool> bWatching{false};
    std::atomic<int> RequestResult{0};
    uint64 PendingFence = 0, PendingSession = 0;
    bool Open()
    {
        const auto Result = Producer.Open(GetID3D12DynamicRHI()->RHIGetDevice_NoMGPU());
        if (Result != UnrealAELink::Result::Ok)
        {
            UE_LOG(LogUnrealAELinkBeauty, Error, TEXT("Beauty startup: %s HRESULT=%08x"),
                UTF8_TO_TCHAR(UnrealAELink::ResultName(Result)), Producer.LastError());
            return false;
        }
        for (uint32 I = 0; I < UnrealAELink::GpuSlotCount; ++I)
            Shared[I] = GetID3D12DynamicRHI()->RHICreateTexture2DFromResource(PF_R8G8B8A8, TexCreate_Shared | TexCreate_ShaderResource,
                FClearValueBinding::None, Producer.Texture(I));
        bOpen = true;
        UE_LOG(LogUnrealAELinkBeauty, Display, TEXT("Beauty ready: DX12 shared textures 1280x720 RGBA8 session=%llu"), Producer.Session());
        return true;
    }
    void Close()
    {
        for (auto& Texture : Shared) Texture.SafeRelease();
        Producer.Close(); bOpen = false; bConnected = false; bWatching = false;
    }
};

FBeautyCapture::FBeautyCapture() : RenderState(MakeShared<FBeautyRenderState, ESPMode::ThreadSafe>()) {}
FBeautyCapture::~FBeautyCapture() { Stop(); }

bool FBeautyCapture::Tick(UWorld* World, const UnrealAELink::FrameMetadata& Camera, const UnrealAELink::FrameIdentity& Identity)
{
    if (!World || !World->Scene || !IsRHID3D12())
    {
        if (!bWarned) { UE_LOG(LogUnrealAELinkBeauty, Warning, TEXT("Beauty requires a rendering world and DX12 RHI")); bWarned = true; }
        return false;
    }
    const double Now = FPlatformTime::Seconds();
    if (!Identity.RequestId && Now - LastCapture < 1.0 / 30.0) return false;
    LastCapture = Now;
    if (Capture.IsValid() && Capture->GetWorld() != World) Stop();
    const bool bCapture = Identity.RequestId || RenderState->bWatching.load();
    if (bCapture && !Capture.IsValid())
    {
        Target.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient));
        Target->ClearColor = FLinearColor::Black;
        Target->InitCustomFormat(UnrealAELink::BeautyWidth, UnrealAELink::BeautyHeight, PF_R8G8B8A8, false);
        Target->UpdateResourceImmediate(true);
        Capture.Reset(NewObject<USceneCaptureComponent2D>(GetTransientPackage(), NAME_None, RF_Transient));
        Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false;
        Capture->bAlwaysPersistRenderingState = true;
        Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
        Capture->TextureTarget = Target.Get();
        Capture->RegisterComponentWithWorld(World);
    }
    FTextureRenderTargetResource* Resource = nullptr;
    if (bCapture)
    {
        // Sequencer dirties primitive render state on the game thread. Send it
        // before enqueuing CaptureScene so the render thread sees this evaluation.
        if (Identity.RequestId) World->SendAllEndOfFrameUpdates();
        Capture->bAlwaysPersistRenderingState = !Identity.RequestId;
        Capture->bCameraCutThisFrame = Identity.RequestId != 0;
        Capture->ShowFlags.SetMotionBlur(!Identity.RequestId);
        Capture->SetWorldLocationAndRotation(FVector(Camera.Position[0], Camera.Position[1], Camera.Position[2]),
            FRotator(Camera.Rotation[0], Camera.Rotation[1], Camera.Rotation[2]));
        Capture->FOVAngle = Camera.FieldOfView;
        Capture->CaptureScene();
        Resource = Target->GameThread_GetRenderTargetResource();
    }
    auto State = RenderState;
    if (Identity.RequestId) State->RequestResult = 1;
    ENQUEUE_RENDER_COMMAND(UnrealAELinkBeautyCopy)([State, Resource, Camera, Identity](FRHICommandListImmediate& Cmd)
    {
        FRHICommandListScopedPipeline Pipeline(Cmd, ERHIPipeline::Graphics);
        if (Identity.RequestId) State->PendingFence = 0;
        if (!State->bOpen && !State->Open()) { if (Identity.RequestId) State->RequestResult = 3; return; }
        uint32 Slot = 0; uint64 Value = 0; bool Connected = false;
        const auto Reserved = State->Producer.Reserve(Slot, Value, Connected);
        if (Reserved != UnrealAELink::Result::Busy) State->bWatching = Connected;
        if (Reserved == UnrealAELink::Result::Stale || Reserved == UnrealAELink::Result::Abandoned)
        {
            // A departed reader may have queued GPU work. Use a new generation;
            // never overwrite its old resources. Retire only our own completed writes.
            if (State->Producer.OwnWritesComplete()) { State->Close(); State->Open(); }
            if (Identity.RequestId) State->RequestResult = 4;
            return;
        }
        if (Connected != State->bConnected)
        {
            UE_LOG(LogUnrealAELinkBeauty, Display, TEXT("Beauty receiver %s"), Connected ? TEXT("connected") : TEXT("disconnected"));
            State->bConnected = Connected;
        }
        if (Reserved != UnrealAELink::Result::Ok) { if (Identity.RequestId) State->RequestResult = 4; return; }
        const FTextureRHIRef Source = Resource ? Resource->GetRenderTargetTexture() : FTextureRHIRef();
        if (!Source.IsValid())
        {
            Cmd.EnqueueLambda([State, Value](FRHICommandList& Executing)
            { GetID3D12DynamicRHI()->RHISignalManualFence(Executing, State->Producer.Fence(), Value); });
            State->Producer.Cancel(Slot); if (Identity.RequestId) State->RequestResult = 3; return;
        }
        Cmd.Transition(FRHITransitionInfo(Source, ERHIAccess::Unknown, ERHIAccess::CopySrc));
        Cmd.Transition(FRHITransitionInfo(State->Shared[Slot], ERHIAccess::Unknown, ERHIAccess::CopyDest));
        Cmd.CopyTexture(Source, State->Shared[Slot], FRHICopyTextureInfo());
        Cmd.Transition(FRHITransitionInfo(State->Shared[Slot], ERHIAccess::CopyDest, ERHIAccess::SRVMask));
        Cmd.Transition(FRHITransitionInfo(Source, ERHIAccess::CopySrc, ERHIAccess::SRVMask));
        Cmd.EnqueueLambda([State, Value](FRHICommandList& Executing)
        { GetID3D12DynamicRHI()->RHISignalManualFence(Executing, State->Producer.Fence(), Value); });
        // If a short mutex race loses publication, Reserve retires this slot after
        // the fence completes and simply drops the frame.
        const auto Published = State->Producer.Publish(Slot, Value, Camera, Identity);
        if (Identity.RequestId)
        {
            if (Published == UnrealAELink::Result::Ok)
            { State->PendingFence = Value; State->PendingSession = State->Producer.Session(); }
            else State->RequestResult = 4;
        }
    });
    return bCapture;
}

int FBeautyCapture::PollRequest(uint64& Session, uint64& Sequence)
{
    auto State = RenderState;
    const int Result = State->RequestResult.load();
    if (Result == 2) { Session = State->PendingSession; Sequence = State->PendingFence; }
    if (Result == 1)
    {
        ENQUEUE_RENDER_COMMAND(UnrealAELinkBeautyCompletion)([State](FRHICommandListImmediate&)
        {
            State->Producer.Heartbeat();
            if (State->RequestResult.load() == 1 && State->PendingFence && State->Producer.Fence())
            {
                const auto Completed = State->Producer.Fence()->GetCompletedValue();
                if (Completed == MAX_uint64) State->RequestResult = 3;
                else if (Completed >= State->PendingFence) State->RequestResult = 2;
            }
        });
    }
    return Result;
}

void FBeautyCapture::Stop()
{
    if (Capture.IsValid()) { Capture->TextureTarget = nullptr; Capture->UnregisterComponent(); }
    auto State = RenderState;
    ENQUEUE_RENDER_COMMAND(UnrealAELinkBeautyStop)([State](FRHICommandListImmediate& Cmd)
    {
        FRHICommandListScopedPipeline Pipeline(Cmd, ERHIPipeline::Graphics);
        if (State->bOpen) { Cmd.SubmitAndBlockUntilGPUIdle(); State->Close(); }
    });
    FlushRenderingCommands();
    Capture.Reset(); Target.Reset();
}

