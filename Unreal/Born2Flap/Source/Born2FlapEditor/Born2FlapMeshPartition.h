#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Born2FlapMeshPartition.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UWorld;

namespace UE::MeshPartition
{
class AMeshPartition;
class UMeshPartitionDefinition;
class UMeshPartitionEditorComponent;
}

/**
 * Native bridge between the Python world generators and the Mesh Partition
 * (Mesh Terrain) editing stack. All functions are editor-only, deterministic
 * and safe to call from -ExecutePythonScript batch generation.
 *
 * The build itself (compiled section generation) is NOT done here; it is
 * driven by the WorldPartitionMeshPartitionBuilder commandlet:
 *   UnrealEditor-Cmd.exe <uproject> /Game/.../Map -run=WorldPartitionBuilderCommandlet
 *       -Builder=WorldPartitionMeshPartitionBuilder -ReuseMode=ByModifierHash
 *       -AllowCommandletRendering
 */
UCLASS()
class BORN2FLAPEDITOR_API UBorn2FlapMeshPartition : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** True when the MeshPartition editor modules are available. */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static bool IsAvailable();

	/**
	 * Ensure the world is a World Partition world (Mesh Partition prerequisite).
	 * Creates/repairs the WorldPartition on the world's settings.
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static bool EnsureWorldPartition(UWorld* InWorld);

	/**
	 * Re-author the channel map of a Mesh Partition definition:
	 * clears all channels and installs the given ones (order = channel slot index,
	 * which matches the material-ID indices used by base modifier meshes).
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static bool ConfigureDefinition(UE::MeshPartition::UMeshPartitionDefinition* InDefinition,
		const TArray<FName>& InChannels, float InChannelTexelSize);

	/**
	 * Create (or replace) a definition asset at InPackagePath/InAssetName. When a
	 * template definition path is given, all its properties (material, build
	 * variants, transformer pipelines, platform settings) are deep-copied first —
	 * the engine default /MeshPartition/DataAssets/MPD_Default is the intended
	 * template. Channels are then re-authored and the asset saved immediately.
	 * Safe to call repeatedly (overwrites a previous generation).
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static UE::MeshPartition::UMeshPartitionDefinition* CreateDefinition(
		const FString& InPackagePath, const FString& InAssetName,
		const FString& InTemplatePath, const TArray<FName>& InChannels, float InChannelTexelSize);

	/** Spawn AMeshPartition + its editor component, assign the definition. */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static UE::MeshPartition::AMeshPartition* CreateMeshPartition(UWorld* InWorld,
		UE::MeshPartition::UMeshPartitionDefinition* InDefinition,
		const FString& InLabel, const FTransform& InTransform);

	/** Editor component accessor (GetMeshPartitionComponent is not a UFUNCTION). */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static UE::MeshPartition::UMeshPartitionEditorComponent* GetEditorComponent(
		UE::MeshPartition::AMeshPartition* InMeshPartition);

	/**
	 * Convert a static mesh (LOD0) to a dynamic mesh and register it as a base
	 * modifier on the Mesh Partition. All triangles are stamped with InMaterialID;
	 * material slot index = channel index (0 = first channel in the definition).
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static AActor* AddBaseModifierFromStaticMesh(UE::MeshPartition::UMeshPartitionEditorComponent* InEditorComponent,
		UStaticMesh* InMesh, const FTransform& InTransform, const TArray<UMaterialInterface*>& InMaterials,
		int32 InMaterialID);

	/**
	 * Boolean modifier using a (closed) static mesh as tool.
	 * Operation: 0=Union, 1=Subtract (carve, capped into a locally-closed
	 * surface), 2=Trim (cut away geometry inside the tool, open hole at the
	 * boundary — no cap). Optionally writes a constant weight to a channel
	 * on the cut walls.
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static AActor* AddBooleanModifier(UE::MeshPartition::UMeshPartitionEditorComponent* InEditorComponent,
		UStaticMesh* InToolMesh, const FTransform& InTransform, int32 Operation,
		FName InChannelToWrite, float InWeight);

	/** Localized remesh modifier (target edge length in cm). */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static AActor* AddRemeshModifier(UE::MeshPartition::UMeshPartitionEditorComponent* InEditorComponent,
		const FVector& InWorldLocation, const FVector& InUnscaledCoverage, float InTargetEdgeLength);

	/** Number of compiled section actors currently present in the world. */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static int32 CountCompiledSections(UWorld* InWorld);

	/**
	 * Winding audit for compiled-section render meshes: for each probe point,
	 * finds the nearest triangle of every section's LOD0 and reports the face
	 * normal implied by the index winding (normalZ > 0 faces UP, < 0 faces
	 * DOWN). The WorldPartitionMeshPartitionBuilder recomputes section normals
	 * from this winding, so DOWN-facing sections are invisible from above
	 * (backface culling) while their collision still works.
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static TArray<FString> AuditSectionWinding(UWorld* InWorld, const TArray<FVector>& InProbePoints);

	/**
	 * Save the dirty external actor packages of a World Partition world
	 * (modifier actors, compiled sections). UEditorLoadingAndSavingUtils::SaveMap
	 * does NOT save external actors - only the interactive level save does -
	 * so batch generation must do it explicitly. Returns the number saved.
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static int32 SaveExternalActorPackages(UWorld* InWorld);

	/** Debug: list all TActorIterator-visible actors as "ClassName Name" strings. */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static TArray<FString> DebugListActors(UWorld* InWorld);

	/** Force-load a World Partition region (shape loader adapter) so spawned
	 *  streaming actors (compiled sections) are available to headless probes. */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static bool LoadWorldPartitionRegion(UWorld* InWorld, const FVector& InExtent);

	/** Activate every data layer for editor streaming AND runtime: sets
	 *  bIsLoadedInEditor + Activated runtime state (Mesh Terrain compiled
	 *  sections hide in per-build-variant RUNTIME data layers created with
	 *  Unloaded initial runtime state). Returns the number touched (-1 when
	 *  unavailable). */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static int32 ActivateAllDataLayers(UWorld* InWorld);

	/**
	 * PERSISTENT runtime fix: the Mesh Partition runtime renders its terrain via
	 * ACompiledSection actors that stream in like any World Partition actor, but
	 * the engine creates each build-variant data layer (MPDL_*) with
	 * InitialRuntimeState=Unloaded and NOTHING in the engine or plugin ever
	 * activates it (cook only strips the platform-irrelevant variants' cells).
	 * Result: sections never load in-game and the terrain is invisible.
	 * This sets the initial runtime state of every MeshPartition-owned variant
	 * data layer to Loaded (bLoaded=true) or Unloaded, marks the WorldDataLayers
	 * package dirty and returns the number of layers touched (-1 unavailable).
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static int32 SetVariantDataLayersInitialRuntimeState(UWorld* InWorld, bool bLoaded);

	/** Dump definition facts (channels, variants, platforms) to the log. */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static void LogDefinitionInfo(UE::MeshPartition::UMeshPartitionDefinition* InDefinition);

	/**
	 * Diagnostic: dump the World Partition actor descriptor container state
	 * (descriptor count + one line per descriptor: class, name, guid, package).
	 * Used to verify persistence: descriptors are registered when an actor's
	 * external package is saved (OnObjectPreSave), NOT at spawn time, so the
	 * save ORDER determines whether the map package records them.
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static TArray<FString> DumpActorDescState(UWorld* InWorld);

	/** Number of registered actor descriptors (for python assertions). */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static int32 CountActorDescs(UWorld* InWorld);

	/** Diagnostic: one line per data layer instance (name, runtime flag,
	 *  editor-loaded flag, initial/current runtime state, effective state). */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static TArray<FString> DumpDataLayerStates(UWorld* InWorld);

	/** Diagnostic: one line per actor desc instance (class, spatial/runtime
	 *  flags, editor relevance, loaded state, editor bounds, data layers). */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static TArray<FString> DumpActorDescDetails(UWorld* InWorld);

	/** Load every compiled-section actor via direct FWorldPartitionReference
	 *  (bypasses cell/filter gating) and PIN the references so the actors stay
	 *  alive for the probe's mesh statistics. Returns the number loaded. */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static int32 LoadAllCompiledSections(UWorld* InWorld);

	/**
	 * Create a new editor world from an engine template map (e.g.
	 * /Engine/Maps/Templates/OpenWorld). Wraps
	 * UEditorLoadingAndSavingUtils::NewMapFromTemplate, which is not a
	 * UFUNCTION and therefore not exposed to Python. The template's World
	 * Partition setup (runtime hash, grids, data layers) comes along.
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static UWorld* NewMapFromTemplateCpp(const FString& InTemplatePath);

	/**
	 * Save a World Partition world with the INTERACTIVE-EDITOR-COMPATIBLE
	 * order: external actor packages first (registers their descriptors via
	 * OnObjectPreSave), then the map package (serializes the container).
	 * UEditorLoadingAndSavingUtils::SaveMap alone saves the map first, so a
	 * first-time headless save writes a map without external actor descs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static bool SaveWorldExternalFirst(UWorld* InWorld, const FString& InMapPath);

	/**
	 * Run the WorldPartitionMeshPartitionBuilder IN-PROCESS (same editor
	 * session as generation), instead of the separate commandlet process.
	 * Requires -AllowCommandletRendering when run headless.
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static bool BuildMeshPartitionInProcess(UWorld* InWorld, bool bReuseByModifierHash);

	/**
	 * Destroy the Mesh Partition AUTHORING actors of the world: modifier
	 * actors (UE::MeshPartition::AModifierActor) and any interactive/preview
	 * section actors. These classes live in the editor-only module; their
	 * baked result lives in the compiled sections. If they remain in the
	 * saved map, an uncooked -game load asserts in
	 * FWorldPartitionActorDesc::operator<< (WorldPartitionActorDesc.cpp:944):
	 * UModifierComponent is the only plugin class overriding
	 * CreateClassComponentDesc, so its component desc enters the descriptor
	 * stream and cannot resolve when the editor module is absent (cook strips
	 * the actors via bIsEditorOnlyActor, but standalone -game reads the
	 * uncooked map). Returns the number destroyed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static int32 StripAuthoringActors(UWorld* InWorld);

	/**
	 * Diagnostic: one line per component desc in the actor descriptor stream
	 * ("ActorClass comp class=<path> name=<name>"). Used to find component
	 * classes that cannot resolve in a -game load (editor-module classes
	 * assert at WorldPartitionActorDesc.cpp:944).
	 */
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	static TArray<FString> DumpDescComponents(UWorld* InWorld);
};