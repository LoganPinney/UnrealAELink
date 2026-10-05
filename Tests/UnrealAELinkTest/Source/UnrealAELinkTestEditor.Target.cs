using UnrealBuildTool;
public class UnrealAELinkTestEditorTarget : TargetRules
{
    public UnrealAELinkTestEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("UnrealAELinkTest");
    }
}
