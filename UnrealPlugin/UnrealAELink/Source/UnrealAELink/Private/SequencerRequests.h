#pragma once
#include "CoreMinimal.h"
#include "UnrealAELink/RequestProtocol.h"
#include "UObject/StrongObjectPtr.h"
class ULevelSequence;
class ULevelSequencePlayer;
class ALevelSequenceActor;
class FBeautyCapture;
class UWorld;

// All methods, including UObject creation/evaluation/destruction, run on the
// game thread. Render-thread fence status is read through FBeautyCapture atomics.
class FSequencerRequests
{
public:
    FSequencerRequests();
    ~FSequencerRequests();
    void Select(const TArray<FString>& Args);
    bool Tick(UWorld* World, UnrealAELink::FrameMetadata Camera, FBeautyCapture& Beauty);
private:
    void ResetPlayer();
    bool Resolve(UWorld* World);
    UnrealAELink::RequestChannel Channel;
    bool bOpen = false, bPending = false, bCapturing = false, bHaveResponse = false;
    double Started = 0;
    FString SequencePath;
    TStrongObjectPtr<ULevelSequence> Sequence;
    TStrongObjectPtr<ULevelSequencePlayer> Player;
    TWeakObjectPtr<ALevelSequenceActor> Actor;
    UnrealAELink::FrameRequest Request;
    UnrealAELink::FrameResponse Response;
    UnrealAELink::FrameMetadata EvaluatedCamera;
};
