#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "UnrealClient.h"
#include "UnrealAELink/Transport.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogUnrealAELink, Log, All);

namespace
{
bool FindCamera(UnrealAELink::FrameMetadata& Frame)
{
    if (!GEngine) return false;
    FTransform Transform;
    FRotator Rotation;
    UWorld* World = nullptr;
    // A player view in PIE/Game has priority over the level editor viewport.
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        UWorld* Candidate = Context.World();
        if (!Candidate || (Context.WorldType != EWorldType::PIE && Context.WorldType != EWorldType::Game)) continue;
        APlayerController* Player = Candidate->GetFirstPlayerController();
        if (!Player || !Player->PlayerCameraManager) continue;
        const FMinimalViewInfo& View = Player->PlayerCameraManager->GetCameraCacheView();
        Rotation = View.Rotation;
        Transform = FTransform(Rotation, View.Location);
        Frame.FieldOfView = View.FOV;
        Frame.AspectRatio = View.AspectRatio;
        if (!View.bConstrainAspectRatio && Context.GameViewport && Context.GameViewport->Viewport)
        {
            const FIntPoint Size = Context.GameViewport->Viewport->GetSizeXY();
            if (Size.Y > 0) Frame.AspectRatio = static_cast<float>(Size.X) / Size.Y;
        }
        Frame.Source = UnrealAELink::CameraSource::PlayerCamera;
        World = Candidate;
        break;
    }
#if WITH_EDITOR
    if (!World && GEditor)
    {
        FLevelEditorViewportClient* Selected = nullptr;
        FViewport* Active = GEditor->GetActiveViewport();
        for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
        {
            if (!Client || !Client->IsPerspective() || !Client->Viewport || !Client->GetWorld()) continue;
            if (Client->Viewport == Active) { Selected = Client; break; }
            if (!Selected) Selected = Client;
        }
        if (Selected)
        {
            Rotation = Selected->GetViewRotation();
            Transform = FTransform(Rotation, Selected->GetViewLocation());
            Frame.FieldOfView = Selected->ViewFOV;
            const FIntPoint Size = Selected->Viewport->GetSizeXY();
            Frame.AspectRatio = Selected->IsAspectRatioConstrained() ? Selected->AspectRatio
                : (Size.Y > 0 ? static_cast<float>(Size.X) / Size.Y : 0.0f);
            Frame.Source = UnrealAELink::CameraSource::EditorViewport;
            World = Selected->GetWorld();
        }
    }
#endif
    if (!World || Frame.FieldOfView <= 0 || Frame.AspectRatio <= 0) return false;
    const FVector Position = Transform.GetLocation();
    const FQuat Quaternion = Transform.GetRotation();
    const FVector Scale = Transform.GetScale3D();
    Frame.Position[0] = Position.X; Frame.Position[1] = Position.Y; Frame.Position[2] = Position.Z;
    Frame.Quaternion[0] = Quaternion.X; Frame.Quaternion[1] = Quaternion.Y;
    Frame.Quaternion[2] = Quaternion.Z; Frame.Quaternion[3] = Quaternion.W;
    Frame.Rotation[0] = Rotation.Pitch; Frame.Rotation[1] = Rotation.Yaw; Frame.Rotation[2] = Rotation.Roll;
    Frame.Scale[0] = Scale.X; Frame.Scale[1] = Scale.Y; Frame.Scale[2] = Scale.Z;
    Frame.TimeSeconds = World->GetTimeSeconds();
    Frame.Flags = UnrealAELink::CameraValid;
    return true;
}
}

// Engine-lifetime service. Ticker and commands are removed before unloading the module.
class FUnrealAELinkModule final : public IModuleInterface
{
public:
    void StartupModule() override
    {
        UE_LOG(LogUnrealAELink, Display, TEXT("Plugin startup: camera metadata protocol v1"));
        StartCommand = MakeUnique<FAutoConsoleCommand>(TEXT("UnrealAELink.Start"), TEXT("Start camera metadata bridge"),
            FConsoleCommandDelegate::CreateRaw(this, &FUnrealAELinkModule::Start));
        StopCommand = MakeUnique<FAutoConsoleCommand>(TEXT("UnrealAELink.Stop"), TEXT("Stop camera metadata bridge"),
            FConsoleCommandDelegate::CreateRaw(this, &FUnrealAELinkModule::Stop));
        StatusCommand = MakeUnique<FAutoConsoleCommand>(TEXT("UnrealAELink.Status"), TEXT("Print bridge status"),
            FConsoleCommandDelegate::CreateRaw(this, &FUnrealAELinkModule::Status));
        Start(); // Enabling the plugin is sufficient for milestone zero.
    }

    void ShutdownModule() override
    {
        Stop();
        StatusCommand.Reset(); StopCommand.Reset(); StartCommand.Reset();
        UE_LOG(LogUnrealAELink, Display, TEXT("Plugin shutdown"));
    }

private:
    void Start()
    {
        if (Bridge) { Status(); return; }
        auto Candidate = MakeUnique<UnrealAELink::Producer>();
        const auto Result = Candidate->Open();
        if (Result != UnrealAELink::Result::Ok)
        {
            UE_LOG(LogUnrealAELink, Error, TEXT("Bridge startup failed: %s (Win32=%u). Only one Unreal instance may own the bridge."),
                UTF8_TO_TCHAR(UnrealAELink::ResultName(Result)), Candidate->LastError());
            return;
        }
        Bridge = MoveTemp(Candidate);
        bReceiverConnected = false;
        LastLogTime = -1;
        Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FUnrealAELinkModule::Tick));
        UE_LOG(LogUnrealAELink, Display, TEXT("Bridge startup: Local\\UnrealAELink.Metadata.v1"));
    }

    void Stop()
    {
        if (Ticker.IsValid()) { FTSTicker::RemoveTicker(Ticker); Ticker.Reset(); }
        if (Bridge)
        {
            Bridge.Reset();
            if (bReceiverConnected) UE_LOG(LogUnrealAELink, Display, TEXT("Receiver disconnected: bridge stopping"));
            bReceiverConnected = false;
            UE_LOG(LogUnrealAELink, Display, TEXT("Bridge stopped"));
        }
    }

    void Status()
    {
        UE_LOG(LogUnrealAELink, Display, TEXT("Bridge=%s Receiver=%s"),
            Bridge ? TEXT("running") : TEXT("stopped"), bReceiverConnected ? TEXT("connected") : TEXT("disconnected"));
    }

    bool Tick(float)
    {
        UnrealAELink::FrameMetadata Frame{};
        Frame.FrameNumber = GFrameCounter;
        const bool bCamera = FindCamera(Frame);
        bool bConnected = bReceiverConnected;
        const auto Result = Bridge->Publish(Frame, bConnected);
        if (Result != UnrealAELink::Result::Ok) return true;
        if (bConnected != bReceiverConnected)
        {
            bReceiverConnected = bConnected;
            UE_LOG(LogUnrealAELink, Display, TEXT("Receiver %s"), bConnected ? TEXT("connected") : TEXT("disconnected"));
        }
        const double Now = FPlatformTime::Seconds();
        if (Now - LastLogTime >= 1.0)
        {
            LastLogTime = Now;
            if (bCamera)
                UE_LOG(LogUnrealAELink, Display, TEXT("Frame=%llu WorldTime=%.4f Camera=%u Position=(%.3f,%.3f,%.3f) Rotation=(P=%.3f,Y=%.3f,R=%.3f) FOV=%.3f Aspect=%.5f"),
                    static_cast<unsigned long long>(Frame.FrameNumber), Frame.TimeSeconds, static_cast<uint32>(Frame.Source),
                    Frame.Position[0], Frame.Position[1], Frame.Position[2], Frame.Rotation[0], Frame.Rotation[1], Frame.Rotation[2],
                    Frame.FieldOfView, Frame.AspectRatio);
            else UE_LOG(LogUnrealAELink, Display, TEXT("Frame=%llu: waiting for an active perspective viewport or player camera"),
                static_cast<unsigned long long>(Frame.FrameNumber));
        }
        return true;
    }

    TUniquePtr<UnrealAELink::Producer> Bridge;
    TUniquePtr<FAutoConsoleCommand> StartCommand, StopCommand, StatusCommand;
    FTSTicker::FDelegateHandle Ticker;
    bool bReceiverConnected = false;
    double LastLogTime = -1;
};

IMPLEMENT_MODULE(FUnrealAELinkModule, UnrealAELink)
