// BORN 2 FLAP — Shiomori Bay coastal water system.
//
// AShiomoriWaterDirector is the central coordinator for the coastal water.
// It OWNS the global ocean parameters and the Material Parameter Collection
// binding, and pushes the single source of truth every frame. It does not
// simulate anything expensive — subsystems (breakers, foam, spray, local sims)
// read from it instead of duplicating global logic.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Water/Born2FlapWater.h"
#include "Born2FlapWaterDirector.generated.h"

class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;

UCLASS(BlueprintType)
class BORN2FLAP_API AShiomoriWaterDirector : public AActor
{
    GENERATED_BODY()

public:
    AShiomoriWaterDirector();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    // Shared water-surface query used by gameplay systems (flight pawn, foam
    // emitters). Returns world Z (cm) of the animated surface at (X,Y).
    UFUNCTION(BlueprintCallable, Category = "Water")
    float GetWaterHeightAt(float X, float Y) const;

    UFUNCTION(BlueprintPure, Category = "Water")
    float GetOceanLevel() const { return Parameters.OceanLevel; }

    UFUNCTION(BlueprintPure, Category = "Water")
    FOceanParameters GetParameters() const { return Parameters; }

    // Central accessor (spawned once by ABorn2FlapGameMode on coast levels).
    static AShiomoriWaterDirector* Get(const UWorld* World);

protected:
    void ApplyParameters();      // push the full parameter set into the MPC
    void PushDynamic(float Time); // per-frame time / LOD / quality updates

    // Authorable global ocean parameters. If ParametersAsset is set and loads,
    // its values override these inline defaults.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
    FOceanParameters Parameters;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
    TSoftObjectPtr<UOceanParametersAsset> ParametersAsset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
    TSoftObjectPtr<UMaterialParameterCollection> OceanMPC;

    // Strong reference to the loaded MPC. A soft reference + the world's weak
    // MPC-instance reference are NOT enough to keep the collection alive — GC
    // can collect it between BeginPlay and Tick, tripping
    // `check(Collection.IsValid())` in SetScalarParameterValue.
    UPROPERTY(Transient)
    TObjectPtr<UMaterialParameterCollection> LoadedMPC;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialParameterCollectionInstance> MPCInstance;

    float TimeSeconds = 0.0f;
};