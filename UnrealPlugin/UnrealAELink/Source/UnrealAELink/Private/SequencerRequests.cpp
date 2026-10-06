#include "SequencerRequests.h"
#include "BeautyCapture.h"
#include "LevelSequence.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"
#include "MovieScene.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceRedirector.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealAELinkSequencer, Log, All);
static TAutoConsoleVariable<int32> LogEvaluatedBindings(TEXT("UnrealAELink.LogEvaluatedBindings"), 0, TEXT("Log bound actor transforms after direct sequence evaluation (acceptance diagnostics)"));
FSequencerRequests::FSequencerRequests()
{
    check(IsInGameThread());
    bOpen = Channel.Open(true) == UnrealAELink::Result::Ok;
    if (!bOpen) UE_LOG(LogUnrealAELinkSequencer, Error, TEXT("Request channel startup failed"));
}
FSequencerRequests::~FSequencerRequests() { check(IsInGameThread()); Channel.Close(); ResetPlayer(); }
void FSequencerRequests::ResetPlayer()
{
    if (Player.IsValid()) Player->Stop();
    Player.Reset();
    if (Actor.IsValid()) Actor->Destroy();
    Actor.Reset(); Sequence.Reset();
}
void FSequencerRequests::Select(const TArray<FString>& Args)
{
    check(IsInGameThread());
    if (bPending || Args.Num() != 1)
    {
        UE_LOG(LogUnrealAELinkSequencer, Error, TEXT("Select one sequence while idle: UnrealAELink.Sequence /Game/Test/MySequence")); return;
    }
    ResetPlayer(); SequencePath = Args[0];
    UE_LOG(LogUnrealAELinkSequencer, Display, TEXT("Selected sequence: %s"), *SequencePath);
}
bool FSequencerRequests::Resolve(UWorld* World)
{
    if (!World || !World->Scene || SequencePath.IsEmpty()) return false;
    if (Player.IsValid() && Actor.IsValid() && Actor->GetWorld() == World) return true;
    ResetPlayer();
    ULevelSequence* Found = FindObject<ULevelSequence>(nullptr, *SequencePath);
    if (!Found) Found = LoadObject<ULevelSequence>(nullptr, *SequencePath);
    if (!Found) return false;
    Sequence.Reset(Found);
    FMovieSceneSequencePlaybackSettings Settings;
    Settings.bAutoPlay = false;
    Settings.bDisableCameraCuts = false;
    ALevelSequenceActor* NewActor = nullptr;
    Player.Reset(ULevelSequencePlayer::CreateLevelSequencePlayer(World, Found, Settings, NewActor));
    Actor = NewActor;
    return Player.IsValid();
}
bool FSequencerRequests::Tick(UWorld* World, UnrealAELink::FrameMetadata Camera, FBeautyCapture& Beauty)
{
    check(IsInGameThread());
    if (!bOpen) return false;
    bool bDeterministic = false;
    UnrealAELink::FrameRequest Incoming;
    // Do not consume the next request until this scene's GPU work has retired,
    // even if its client timed out and submitted another request.
    if (!bPending)
    {
        const auto Code = Channel.Poll(Incoming, bDeterministic);
        if (Code != UnrealAELink::Result::Ok) return bDeterministic || Code == UnrealAELink::Result::Busy;
        Request = Incoming; bPending = true; bCapturing = false; bHaveResponse = false;
        Started = FPlatformTime::Seconds();
        Response = UnrealAELink::FrameResponse{};
        Response.RequestId = Request.RequestId;
        Response.RequestedTimeValue = Request.TimeValue; Response.RequestedTimeScale = Request.TimeScale;
        UE_LOG(LogUnrealAELinkSequencer, Display, TEXT("REQUEST id=%llu time=%lld/%lld"), Request.RequestId, Request.TimeValue, Request.TimeScale);
        if (Request.StructBytes != sizeof(Request) || Request.Version != UnrealAELink::RequestVersion ||
            Request.Width != UnrealAELink::BeautyWidth || Request.Height != UnrealAELink::BeautyHeight)
        { Response.Status = UnrealAELink::FrameStatus::CaptureFailed; bHaveResponse = true; }
        else if (!Resolve(World)) { Response.Status = UnrealAELink::FrameStatus::NoSequence; bHaveResponse = true; }
        else
        {
            const FFrameRate Rate = Player->GetFrameRate();
            int32 Whole = 0; float Fraction = 0;
            if (!UnrealAELink::RationalToFrame(Request.TimeValue, Request.TimeScale, Rate.Numerator, Rate.Denominator, Whole, Fraction))
            { Response.Status = UnrealAELink::FrameStatus::InvalidTime; bHaveResponse = true; }
            else
            {
                const FFrameTime Time(FFrameNumber(Whole), Fraction);
                // The prototype maps AE comp zero to sequence absolute zero and
                // rejects requests outside the sequence's authored playback range.
                const FFrameTime TickTime = FFrameRate::TransformTime(Time, Rate, Sequence->GetMovieScene()->GetTickResolution());
                if (!Sequence->GetMovieScene()->GetPlaybackRange().Contains(TickTime.GetFrame()))
                { Response.Status = UnrealAELink::FrameStatus::InvalidTime; bHaveResponse = true; }
                else
                {
                    FMovieSceneSequencePlaybackParams Params(Time, EUpdatePositionMethod::Jump);
                    Params.bHasJumped = true;
                    Player->SetPlaybackPosition(Params); // synchronous direct evaluation; no Play()
                    const auto Evaluated = Player->GetCurrentTime();
                    if (Evaluated.Time != Time)
                    { Response.Status = UnrealAELink::FrameStatus::InvalidTime; bHaveResponse = true; }
                    else
                    {
                        UE_LOG(LogUnrealAELinkSequencer, Display, TEXT("SEQUENCER id=%llu time=%lld/%lld evaluated=%d+%.9f rate=%d/%d seconds=%.9f"),
                            Request.RequestId, Request.TimeValue, Request.TimeScale, Whole, Fraction, Rate.Numerator, Rate.Denominator, Evaluated.AsSeconds());
                        if (LogEvaluatedBindings.GetValueOnGameThread())
                        {
                            for (const auto& Binding : static_cast<const UMovieScene*>(Sequence->GetMovieScene())->GetBindings())
                                for (const auto& Bound : Player->FindBoundObjects(Binding.GetObjectGuid(), MovieSceneSequenceID::Root))
                                    if (const AActor* BoundActor = Cast<AActor>(Bound.Get()))
                                    {
                                        const auto Position = BoundActor->GetActorLocation();
                                        UE_LOG(LogUnrealAELinkSequencer, Display, TEXT("STATE id=%llu binding=%s position=(%.6f,%.6f,%.6f)"),
                                            Request.RequestId, *BoundActor->GetActorNameOrLabel(), Position.X, Position.Y, Position.Z);
                                    }
                        }
                        if (UCameraComponent* View = Player->GetActiveCameraComponent())
                        {
                            const FVector Position = View->GetComponentLocation(); const FRotator Rotation = View->GetComponentRotation();
                            const FQuat Quaternion = Rotation.Quaternion();
                            Camera.Position[0] = Position.X; Camera.Position[1] = Position.Y; Camera.Position[2] = Position.Z;
                            Camera.Rotation[0] = Rotation.Pitch; Camera.Rotation[1] = Rotation.Yaw; Camera.Rotation[2] = Rotation.Roll;
                            Camera.Quaternion[0] = Quaternion.X; Camera.Quaternion[1] = Quaternion.Y; Camera.Quaternion[2] = Quaternion.Z; Camera.Quaternion[3] = Quaternion.W;
                            Camera.FieldOfView = View->FieldOfView;
                        }
                        Camera.Flags = UnrealAELink::CameraValid;
                        Camera.TimeSeconds = Evaluated.AsSeconds(); EvaluatedCamera = Camera;
                    }
                }
            }
        }
    }
    if (!bHaveResponse)
    {
        uint64 Session = 0, SequenceNumber = 0;
        const int Result = bCapturing ? Beauty.PollRequest(Session, SequenceNumber) : 4;
        if (Result == 2)
        {
            Response.BeautySession = Session; Response.BeautySequence = SequenceNumber;
            Response.Status = UnrealAELink::FrameStatus::Success; bHaveResponse = true;
            UE_LOG(LogUnrealAELinkSequencer, Display, TEXT("PUBLISH id=%llu beauty=%llu session=%llu"), Request.RequestId, SequenceNumber, Session);
        }
        else if (Result == 3 || (!bCapturing && FPlatformTime::Seconds() - Started > UnrealAELink::RequestTimeoutMs / 1000.0))
        { Response.Status = UnrealAELink::FrameStatus::CaptureFailed; bHaveResponse = true; }
        else if (Result == 4)
        {
            if (FPlatformTime::Seconds() - Started > UnrealAELink::RequestTimeoutMs / 1000.0)
            { Response.Status = UnrealAELink::FrameStatus::Timeout; bHaveResponse = true; }
            else
            {
                bCapturing = Beauty.Tick(World, EvaluatedCamera, {Request.RequestId, Request.TimeValue, Request.TimeScale});
                UE_LOG(LogUnrealAELinkSequencer, Display, TEXT("CAPTURE id=%llu"), Request.RequestId);
            }
        }
    }
    if (bHaveResponse)
    {
        const auto Code = Channel.Complete(Response);
        if (Code != UnrealAELink::Result::Busy)
        {
            if (Response.Status != UnrealAELink::FrameStatus::Success)
            {
                UE_LOG(LogUnrealAELinkSequencer, Error, TEXT("FAILED id=%llu time=%lld/%lld status=%d"),
                    Request.RequestId, Request.TimeValue, Request.TimeScale, int(Response.Status));
            }
            // Keep the identity chain on disk even if the isolated host harness
            // exits immediately after the last returned frame.
            if (GLog) { GLog->FlushThreadedLogs(); GLog->Flush(); }
            bPending = false;
        }
    }
    else
    {
        Channel.Heartbeat(); // do not consume a replacement request during GPU retirement
    }
    return true;
}
