#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/PointLight.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"

namespace
{
class FBeautySceneForReceiver final : public IAutomationLatentCommand
{
public:
    explicit FBeautySceneForReceiver(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        if (!Client)
        {
            for (auto* Candidate : GEditor->GetLevelViewportClients())
                if (Candidate && Candidate->IsPerspective() && Candidate->Viewport) { Client = Candidate; break; }
            if (!Client) { Test->AddError(TEXT("No perspective viewport")); return true; }
            Location = Client->GetViewLocation(); Rotation = Client->GetViewRotation();
            FOV = Client->ViewFOV; FOVAngle = Client->FOVAngle;
            UWorld* World = Client->GetWorld();
            FActorSpawnParameters Params; Params.ObjectFlags = RF_Transient;
            Cube = World->SpawnActor<AStaticMeshActor>(FVector(600, 0, 100), FRotator::ZeroRotator, Params);
            auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
            if (!Cube || !Mesh) { Test->AddError(TEXT("Test cube creation failed")); return true; }
            Cube->GetStaticMeshComponent()->SetStaticMesh(Mesh);
            Light = World->SpawnActor<APointLight>(FVector(200, -100, 300), FRotator::ZeroRotator, Params);
            if (Light)
            {
                Light->PointLightComponent->SetMobility(EComponentMobility::Movable);
                Light->PointLightComponent->SetIntensity(100000.0f);
                Light->PointLightComponent->SetAttenuationRadius(2000.0f);
            }
            Begin = FPlatformTime::Seconds();
        }
        const double Elapsed = FPlatformTime::Seconds() - Begin;
        if (Elapsed >= 12.0)
        {
            Client->SetViewLocation(Location); Client->SetViewRotation(Rotation);
            Client->ViewFOV = FOV; Client->FOVAngle = FOVAngle; Client->Invalidate();
            if (Cube) Cube->Destroy(); if (Light) Light->Destroy();
            Test->AddInfo(TEXT("Transient illuminated scene and moving camera exercised real SceneCapture Beauty for 12 seconds."));
            return true;
        }
        Client->SetViewLocation(FVector(Elapsed * 8, -130 + Elapsed * 20, 110));
        Client->SetViewRotation(FRotator(-1, 0, 0));
        Client->ViewFOV = 70; Client->FOVAngle = 70; Client->Invalidate();
        return false;
    }
private:
    FAutomationTestBase* Test;
    FLevelEditorViewportClient* Client = nullptr;
    AStaticMeshActor* Cube = nullptr;
    APointLight* Light = nullptr;
    FVector Location; FRotator Rotation;
    float FOV = 0, FOVAngle = 0;
    double Begin = 0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAELinkBeautyTest, "UnrealAELink.Beauty.FrameTransfer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealAELinkBeautyTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FBeautySceneForReceiver(this));
    return true;
}
#endif
