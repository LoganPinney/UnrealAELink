#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "HAL/PlatformTime.h"

namespace
{
class FMoveCameraForReceiver final : public IAutomationLatentCommand
{
public:
    explicit FMoveCameraForReceiver(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        if (!Client)
        {
            for (auto* Candidate : GEditor->GetLevelViewportClients())
                if (Candidate && Candidate->IsPerspective() && Candidate->Viewport) { Client = Candidate; break; }
            if (!Client) { Test->AddError(TEXT("No editor perspective viewport; launch UnrealEditor, not a commandlet")); return true; }
            OriginalLocation = Client->GetViewLocation();
            OriginalRotation = Client->GetViewRotation();
            OriginalFOV = Client->ViewFOV;
            OriginalFOVAngle = Client->FOVAngle;
            Begin = FPlatformTime::Seconds();
        }
        const double Elapsed = FPlatformTime::Seconds() - Begin;
        if (Elapsed >= 8.0)
        {
            Client->SetViewLocation(OriginalLocation);
            Client->SetViewRotation(OriginalRotation);
            Client->ViewFOV = OriginalFOV;
            Client->FOVAngle = OriginalFOVAngle;
            Client->Invalidate();
            Test->AddInfo(TEXT("Real editor camera changed for 8 seconds; verify the external ReceiverTest --expect-motion result."));
            return true;
        }
        Client->SetViewLocation(FVector(120.0 + Elapsed * 100.0, -40.0 + Elapsed * 10.0, 185.0));
        Client->SetViewRotation(FRotator(-10.0, 42.0 + Elapsed * 5.0, 0.0));
        Client->ViewFOV = static_cast<float>(50.0 + Elapsed);
        Client->FOVAngle = Client->ViewFOV;
        Client->Invalidate();
        return false;
    }
private:
    FAutomationTestBase* Test;
    FLevelEditorViewportClient* Client = nullptr;
    FVector OriginalLocation;
    FRotator OriginalRotation;
    float OriginalFOV = 0;
    float OriginalFOVAngle = 0;
    double Begin = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAELinkCameraTest, "UnrealAELink.Metadata.EditorCamera",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealAELinkCameraTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FMoveCameraForReceiver(this));
    return true;
}
#endif
