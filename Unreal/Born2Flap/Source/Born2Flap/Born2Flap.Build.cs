using UnrealBuildTool;
using System.IO;

public class Born2Flap : ModuleRules
{
    public Born2Flap(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.Add(ModuleDirectory);

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "InputCore", "PhysicsCore"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Slate", "SlateCore", "UMG", "ProceduralMeshComponent", "ApplicationCore",
            "AudioMixer", "Json", "MoviePlayer"
        });

        if (Target.Platform == UnrealTargetPlatform.Win64)
            { PublicSystemLibraries.AddRange(new[] { "dinput8.lib", "dxguid.lib", "ole32.lib" });
            RuntimeDependencies.Add("$(ProjectDir)/Binaries/ThirdParty/born2flap_math.dll", StagedFileType.NonUFS); }

        PrivateIncludePaths.Add(ModuleDirectory);
        RuntimeDependencies.Add("$(ProjectDir)/ThirdPartyNotices.txt", StagedFileType.NonUFS);

        PublicIncludePaths.Add(Path.GetFullPath(
            Path.Combine(ModuleDirectory, "../../../../Native/include")));
    }
}
