#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Born2FlapTerrainProbe.generated.h"

/**
 * -B2FTerrainProbe runtime diagnostic (standalone -game, uncooked map).
 *
 * The Mesh Partition terrain renders at RUNTIME only: the compiled-section
 * actors (ACompiledSection, editorRelevant=0) stream in through the World
 * Partition runtime hash and draw via UMeshPartitionStaticMeshComponent.
 * Editor-side probes can never see them, so this subsystem samples the live
 * game world: it teleports the player pawn across the map, counts streamed
 * compiled sections / their mesh+collision components, and line-traces the
 * terrain points. Verdict logged as B2FTERRAIN_VERDICT, then it exits.
 */
UCLASS()
class BORN2FLAP_API UBorn2FlapTerrainProbe : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return bEnabled && !bDone; }
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UBorn2FlapTerrainProbe, STATGROUP_Tickables);
	}

private:
	void DumpState(UWorld* World);
	void TraceAt(UWorld* World, const FVector& Start, const FVector& End, const FString& Label);
	void RunPoint(UWorld* World);
	void Finish(UWorld* World);

	bool bEnabled = false;
	bool bDone = false;
	bool bShotRequested = false;
	bool bShotPending = false;
	float ShotDelay = 0.f;
	TWeakObjectPtr<APawn> ProbeOriginalPawn;
	float RestoreWait = -1.f;
	bool bDropTestDone = false;
	float Time = 0.f;
	float NextDump = 0.f;
	int32 PointIndex = -1;
	float PointWait = 0.f;
	float DropWait = 0.f;
	TArray<TPair<FString, FVector>> Points;
	FString MapName;
	int32 TotalHits = 0;
	int32 TotalTraces = 0;
};