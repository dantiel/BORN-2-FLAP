using UnrealBuildTool;
using System.IO;

public class Born2Flap : ModuleRules
{
    public Born2Flap(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "InputCore"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Slate", "SlateCore", "UMG"
        });

        PrivateIncludePaths.Add(ModuleDirectory);

        PublicIncludePaths.Add(Path.GetFullPath(
            Path.Combine(ModuleDirectory, "../../../../Native/include")));
    }
}
