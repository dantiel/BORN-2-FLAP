#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Born2FlapMeshPartitionPreview.generated.h"

// Editor-viewport visualization for MeshPartition terrain.
//
// The compiled terrain sections are runtime-only actors
// (ACompiledSection::IsRuntimeOnly() == true): World Partition never streams
// them into the editor world, and the authoring modifier actors are stripped
// after every build (required for uncooked -game loads). Without this
// subsystem the terrain is therefore invisible in the editor viewport even
// though it renders and collides correctly in game sessions.
//
// On map open this subsystem loads the compiled-section packages of the map
// directly and copies their static-mesh components into transient proxy
// actors (RF_Transient, hidden in game, never saved).

namespace UE::MeshPartition
{
class ACompiledSection;
}

UCLASS()
class UBorn2FlapMeshPartitionPreview : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void OnMapOpened(const FString& Filename, bool bAsTemplate);
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	bool Tick(float DeltaTime);

	// How long the map-open scan keeps retrying while the World Partition
	// streaming and the asset registry settle, before the ungated fallback.
	static constexpr double MaxPendingSeconds = 8.0;
	static constexpr double MinAttemptIntervalSeconds = 0.25;

	// Diagnostic/test hook: run the scan immediately for the given world and
	// return the number of spawned proxy actors.
	UFUNCTION(BlueprintCallable, Category = "Born2Flap|MeshPartition")
	int32 ForceScanForPreview(UWorld* World);

private:
	int32 ScanAndSpawnProxies(UWorld* World, bool bRequireMeshPartitionActor);
	void SpawnProxyForSection(UE::MeshPartition::ACompiledSection* Section, UWorld* World);
	class UMaterialInterface* GetCachedTerrainMaterial();

	TWeakObjectPtr<UWorld> PendingWorld;
	TWeakObjectPtr<class UMaterialInterface> CachedTerrainMaterial;
	FTSTicker::FDelegateHandle TickerHandle;
	int32 SpawnedProxyCount = 0;
	double PendingStartTime = 0.0;
	double LastAttemptTime = 0.0;
	int32 PendingAttempts = 0;
};