using UnrealBuildTool;
using System.IO;

public class UnrealAELink : ModuleRules
{
    public UnrealAELink(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "RHI", "RenderCore", "D3D12RHI", "LevelSequence", "MovieScene", "MovieSceneTracks" });
        AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
        PublicSystemLibraries.AddRange(new[] { "d3d12.lib", "dxgi.lib" });
        if (Target.bBuildEditor)
            PrivateDependencyModuleNames.Add("UnrealEd");
        string SharedRoot = Path.GetFullPath(Path.Combine(PluginDirectory, "../../Shared"));
        if (!File.Exists(Path.Combine(SharedRoot, "src", "Transport.cpp")))
            throw new BuildException("UnrealAELink requires Shared/ next to UnrealPlugin/. Keep the repository together; see docs/build.md.");
        PrivateIncludePaths.Add(Path.Combine(SharedRoot, "include"));
        PrivateIncludePaths.Add(Path.Combine(SharedRoot, "src"));
        PrivateDefinitions.Add("UNREAL_AE_LINK_WITH_UNREAL=1");
        ExternalDependencies.Add(Path.Combine(SharedRoot, "src", "Transport.cpp"));
        ExternalDependencies.Add(Path.Combine(SharedRoot, "src", "GpuTransport.cpp"));
        ExternalDependencies.Add(Path.Combine(SharedRoot, "src", "RequestTransport.cpp"));
        ExternalDependencies.Add(Path.Combine(SharedRoot, "include", "UnrealAELink", "RequestProtocol.h"));
        ExternalDependencies.Add(Path.Combine(SharedRoot, "include", "UnrealAELink", "GpuTransport.h"));
        ExternalDependencies.Add(Path.Combine(SharedRoot, "include", "UnrealAELink", "GpuProtocol.h"));
        ExternalDependencies.Add(Path.Combine(SharedRoot, "include", "UnrealAELink", "Protocol.h"));
        ExternalDependencies.Add(Path.Combine(SharedRoot, "include", "UnrealAELink", "Transport.h"));
    }
}
