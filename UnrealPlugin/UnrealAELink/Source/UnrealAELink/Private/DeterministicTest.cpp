#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/PointLight.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "HAL/IConsoleManager.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
TAutoConsoleVariable<float> DeterministicTestSeconds(TEXT("UnrealAELink.DeterministicTestSeconds"), 300.0f,
    TEXT("Lifetime of the transient deterministic Sequencer acceptance fixture"));
class FDeterministicFixture final : public IAutomationLatentCommand
{
public:
    explicit FDeterministicFixture(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        if (!Client)
        {
            for (auto* View : GEditor->GetLevelViewportClients())
                if (View && View->IsPerspective() && View->Viewport) { Client = View; break; }
            if (!Client) { Test->AddError(TEXT("No perspective viewport")); return true; }
            Location = Client->GetViewLocation(); Rotation = Client->GetViewRotation();
            FOV = Client->ViewFOV; FOVAngle = Client->FOVAngle;
            UWorld* World = Client->GetWorld();
            FActorSpawnParameters Params; Params.ObjectFlags = RF_Transient;
            Cube = World->SpawnActor<AStaticMeshActor>(FVector(600, -240, 100), FRotator::ZeroRotator, Params);
            auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
            if (!Cube || !Mesh) { Test->AddError(TEXT("Cube creation failed")); return true; }
            Cube->SetActorLabel(TEXT("DeterministicCube"));
            Cube->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Cube->GetStaticMeshComponent()->SetStaticMesh(Mesh);
            Light = World->SpawnActor<APointLight>(FVector(200, 0, 350), FRotator::ZeroRotator, Params);
            if (Light)
            {
                Light->PointLightComponent->SetMobility(EComponentMobility::Movable);
                Light->PointLightComponent->SetIntensity(150000.0f);
                Light->PointLightComponent->SetAttenuationRadius(2000.0f);
            }
            Sequence.Reset(NewObject<ULevelSequence>(GetTransientPackage(), TEXT("UnrealAELinkDeterministicFixture"), RF_Transient));
            Sequence->Initialize();
            UMovieScene* Scene = Sequence->GetMovieScene();
            Scene->SetDisplayRate(FFrameRate(30, 1)); Scene->SetTickResolutionDirectly(FFrameRate(24000, 1));
            Scene->SetPlaybackRange(0, 72000);
            const FGuid Binding = Scene->AddPossessable(TEXT("DeterministicCube"), Cube->GetClass());
            Sequence->BindPossessableObject(Binding, *Cube, World);
            auto* Track = Scene->AddTrack<UMovieScene3DTransformTrack>(Binding);
            auto* Section = CastChecked<UMovieScene3DTransformSection>(Track->CreateNewSection());
            Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(72000)));
            Track->AddSection(*Section);
            auto Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
            const double Defaults[] = {600, -240, 100, 0, 0, 0, 1, 1, 1};
            for (int32 Index = 0; Index < 9; ++Index) Channels[Index]->SetDefault(Defaults[Index]);
            Channels[1]->AddLinearKey(FFrameNumber(0), -240);
            Channels[1]->AddLinearKey(FFrameNumber(48000), 240); // 60 authored frames at 30 fps
            Client->SetViewLocation(FVector(0, 0, 100)); Client->SetViewRotation(FRotator::ZeroRotator);
            Client->ViewFOV = 70; Client->FOVAngle = 70; Client->Invalidate();
            GEngine->Exec(World, *FString::Printf(TEXT("UnrealAELink.Sequence %s"), *Sequence->GetPathName()));
            Begin = FPlatformTime::Seconds();
            UE_LOG(LogTemp, Display, TEXT("UnrealAELink deterministic fixture ready: sequence=%s rate=30/1 tick=24000/1 cube Y=-240+240*timeSeconds"), *Sequence->GetPathName());
        }
        if (FPlatformTime::Seconds() - Begin < DeterministicTestSeconds.GetValueOnGameThread()) return false;
        Client->SetViewLocation(Location); Client->SetViewRotation(Rotation);
        Client->ViewFOV = FOV; Client->FOVAngle = FOVAngle; Client->Invalidate();
        if (Cube) Cube->Destroy(); if (Light) Light->Destroy();
        Sequence.Reset();
        return true;
    }
private:
    FAutomationTestBase* Test;
    FLevelEditorViewportClient* Client = nullptr;
    AStaticMeshActor* Cube = nullptr;
    APointLight* Light = nullptr;
    TStrongObjectPtr<ULevelSequence> Sequence;
    FVector Location; FRotator Rotation;
    float FOV = 0, FOVAngle = 0;
    double Begin = 0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeterministicSequenceTest, "UnrealAELink.Sequencer.HostFixture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeterministicSequenceTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FDeterministicFixture(this)); return true;
}
#endif
