#pragma once
// Born2FlapWindLeaves.h — makes the wind VISIBLE: free-flying foliage carried
// by the atmospheric field, plus anchored grass blades that bend and flutter.
//
// Both are instanced static meshes reusing the valley's own grass assets, so
// no new material or particle-system asset is required. The field it samples is
// Born2FlapWind::Sample — the same wind that drives the flight physics and the
// aero-audio voices, so what you see is literally what the bird feels and hears.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapWindLeaves.generated.h"

class UInstancedStaticMeshComponent;

UCLASS()
class BORN2FLAP_API ABorn2FlapWindLeaves : public AActor
{
    GENERATED_BODY()

public:
    ABorn2FlapWindLeaves();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    struct FLeaf
    {
        FVector Pos;      // world cm
        FVector Vel;      // cm/s
        FRotator Rot;
        FRotator Spin;    // deg/s tumbling
        float Scale;
        double Life;
    };
    struct FBlade
    {
        FVector Anchor;   // world cm, on the ground
        FRotator Base;
        float Scale;
        double Phase;
    };

    FVector CameraLocation() const;
    void ScanTrees();
    void RespawnBlades(bool bForce);
    FLeaf SpawnLeaf(FRandomStream& R, const FVector& Cam);
    FBlade SpawnBlade(FRandomStream& R, const FVector& Cam);

    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Leaves;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Blades;

    TArray<FLeaf> LeafPool;
    TArray<FBlade> BladePool;
    // Cached tree locations (StaticMeshActors with tree meshes) so leaves shed
    // from actual canopies rather than drifting anywhere around the camera.
    TArray<FVector> TreePositions;
    TArray<float> TreeHeights;
    FVector LastCam = FVector::ZeroVector;
    bool bInitialized = false;

    // Airborne leaf count tracks horizontal wind strength: calm air keeps a few
    // stragglers aloft, a gale strips the canopy. The pool stays MaxLeaves; the
    // inactive tail is parked at zero scale until stronger wind reveals it.
    static constexpr int32 MinLeaves = 50;
    static constexpr int32 MaxLeaves = 520;
    static constexpr int32 MaxBlades = 320;
};