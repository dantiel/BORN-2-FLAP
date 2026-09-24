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
            "Slate", "SlateCore", "UMG", "ProceduralMeshComponent", "ApplicationCore"
        });

        if (Target.Platform == UnrealTargetPlatform.Win64)
            PublicSystemLibraries.AddRange(new[] { "dinput8.lib", "dxguid.lib", "ole32.lib" });

        PrivateIncludePaths.Add(ModuleDirectory);

        PublicIncludePaths.Add(Path.GetFullPath(
            Path.Combine(ModuleDirectory, "../../../../Native/include")));
    }
}
