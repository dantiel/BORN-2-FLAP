using UnrealBuildTool;
using System.IO;

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
            "UnrealEd", "Niagara", "NiagaraEditor", "AssetRegistry",
            "EditorSubsystem",
            "Born2Flap", "MeshDescription", "StaticMeshDescription",
            "MeshPartition", "MeshPartitionEditor", "MeshConversion",
            "DynamicMesh", "GeometryFramework", "GeometryCore",
            "GeometryAlgorithms", "ModelingComponents"
        });

        // MeshPartition public headers include internal Engine headers
        // (MaterialCache/MaterialCacheVirtualTexture.h) that live under
        // Source/Runtime/Engine/Internal. Mirror the engine plugin's access.
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source", "Runtime", "Engine", "Internal"));

        // WorldPartitionMeshPartitionBuilder.h includes MeshPartitionModifierTaskGraph.h,
        // which lives in the plugin's Private folder. Mirror the plugin's own access.
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Plugins", "Experimental", "MeshPartition", "Source", "MeshPartitionEditor", "Private"));
    }
}