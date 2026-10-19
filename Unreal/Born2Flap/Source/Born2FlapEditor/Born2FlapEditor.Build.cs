using UnrealBuildTool;

public class Born2FlapEditor : ModuleRules
{
    public Born2FlapEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "UnrealEd", "Niagara", "NiagaraEditor", "AssetRegistry"
        });
    }
}
