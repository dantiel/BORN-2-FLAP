#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Born2FlapMeshPartitionRuntime.generated.h"

namespace UE::MeshPartition
{
class ACompiledSection;
}

/**
 * Runtime caretaker for Mesh Partition terrain in-game.
 *
 * UE 5.8's MeshPartition runtime is a stub: the compiled-section actors are
 * marked RuntimeOnly in the World Partition descriptor stream, so the runtime
 * hash loads their packages and adds them to the level but NEVER initializes
 * them — components stay unregistered, nothing renders, nothing collides.
 * (The editor preview works through the editor module; packaged/uncooked
 * play does not.)
 *
 * This subsystem periodically registers any unregistered ACompiledSection
 * actor in the world (required for the section meshes to render and the
 * collision components to build their physics bodies), replaces the
 * engine-default channel material (editor MaterialCache only — weightmap
 * colors or invisible in game) with the real terrain material, and enforces
 * the desktop variant data-layer policy (common + high-end loaded, low-end
 * unloaded) so the terrain never renders as overlapping duplicate copies.
 */
UCLASS()
class BORN2FLAP_API UBorn2FlapMeshPartitionRuntime : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UBorn2FlapMeshPartitionRuntime, STATGROUP_Tickables);
	}

	/** Register all unregistered compiled-section actors now (also public for
	 *  diagnostics/tests). Returns the number of actors registered. */
	int32 RegisterPendingSections();

	/** True when this world owns Mesh Partition terrain (actor or sections). */
	bool HasMeshPartitionTerrain() const;

	/** Blocking-trace readiness probe: has terrain (or any static ground)
	 *  collision finished cooking below Pos? The MP collision bodies cook
	 *  asynchronously at runtime; until they exist the bird would fall
	 *  through. IgnoreActor keeps the probe from hitting the bird itself. */
	bool IsTerrainCollisionReadyAt(const FVector& Pos, const AActor* IgnoreActor = nullptr) const;

private:
	/** Keep the variant data layers at the desktop policy: low-end render
	 *  variant unloaded (avoids overlapping terrain copies), common + high-end
	 *  loaded/activated. Self-heals maps finalized before the policy existed. */
	void EnforceVariantDataLayerPolicy();

	/** The engine default channel material renders only via the editor-side
	 *  MaterialCache; in game it shows weightmap colors or nothing. Replace it
	 *  with the real terrain material so the terrain renders properly. */
	UMaterialInterface* GetCachedTerrainMaterial();
	void ApplyTerrainMaterialFix(UE::MeshPartition::ACompiledSection* Section);
	void ApplyTerrainMaterialFixToAllSections();

	double LastScanTime = 0.0;
	double LastMaterialPass = 0.0;
	double LastPolicyPass = -100.0;
	TWeakObjectPtr<UMaterialInterface> CachedTerrainMaterial;
	bool bSelfShotDone = false;
	bool bSelfShotDone2 = false;
};