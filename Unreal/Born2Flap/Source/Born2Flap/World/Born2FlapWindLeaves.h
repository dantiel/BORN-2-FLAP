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
#include "World/Born2FlapWeather.h"
#include "Born2FlapWindLeaves.generated.h"

class UInstancedStaticMeshComponent;
class UNiagaraComponent;

UCLASS()
class BORN2FLAP_API ABorn2FlapWindLeaves : public AActor
{
    GENERATED_BODY()

public:
    ABorn2FlapWindLeaves();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    // Fire a one-shot sand/dust burst (bird landing, wing touching ground).
    // WorldPos is the contact point; Strength 0..1 scales mote count + eject
    // speed. Motes are injected into the ambient Dust pool and live out their
    // life regardless of the current gust/wind activity level.
    void EmitSandBurst(const FVector& WorldPos, float Strength);
    // Yunsha desert: a big rolling sand cloud that occasionally sweeps over the
    // dunes. Internal — fired by the dune-gust cadence in Tick.
    void EmitDuneGust();

private:
    struct FLeaf
    {
        FVector Pos;      // world cm
        FVector Vel;      // cm/s
        FRotator Rot;
        FRotator Spin;    // deg/s tumbling
        float Scale;
        double Life;
        double LifeMax = 0.0;   // initial Life, for end-of-life fade-out
        bool bBurst = false;   // one-shot sand/dust mote (landing, wing-touch)
        bool bCloud = false;   // soft dust puff (larger, lighter, lingers) vs fine grain
        // Realistic flutter — deterministic per-leaf rhythm, set at spawn, so
        // each leaf sways, glides-and-drops, and flips with its own cadence
        // instead of rigidly tumbling on one fixed axis.
        double Phase = 0.0;        // base phase (radians)
        float SwayFreq = 1.0f;     // lateral zigzag, cycles/sec
        float SwayAmp = 30.0f;     // lateral zigzag, cm
        float GlideFreq = 0.8f;    // stall-and-drop, cycles/sec
        float GlideAmp = 30.0f;    // sink-speed oscillation, cm/s
        float TumbleFreq = 1.2f;   // flip rate, cycles/sec
        int32 MeshIdx = 0;         // 0 → Leaves, 1 → Leaves2 (per-level leaf type)
        int32 InstanceIdx = 0;     // component-local instance index (fixed at spawn)
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
    FLeaf SpawnDust(FRandomStream& R, const FVector& Cam, const FVector& Bird, bool bNearBird);

    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Leaves;
    // Second leaf type so the sky holds mixed foliage (two shapes per level)
    // instead of a single repeated blade.
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Leaves2;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Blades;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Dust;
    // GPU Niagara sand path (High/Epic): DustFX is the looping ambient fine-sand
    // field; SandBurstFX is the one-shot landing/wing-touch projectile burst.
    UPROPERTY() TObjectPtr<UNiagaraComponent> DustFX;
    UPROPERTY() TObjectPtr<UNiagaraComponent> SandBurstFX;
    // Yunsha desert: one-shot big rolling sand cloud (GPU Niagara path). Fired
    // on a slow cadence, repositioned upwind so it sweeps across the camera.
    UPROPERTY() TObjectPtr<UNiagaraComponent> DuneGustFX;
    bool bNiagaraSand = false;   // true when the GPU Niagara path is active
    bool bDesert = false;        // true in the Yunsha desert (sandy wind)
    double NextGustTime = 0.0;   // world time of the next dune gust
    // Sluggish mean wind heading the dune-gust clouds ride: eases toward the
    // live horizontal wind so each gust follows the weather's average drift
    // rather than the frame-to-frame wobble (a natural, heavy air-mass lag).
    FVector GustWindDir = FVector(1.0f, 0.0f, 0.0f);
    bool bGustWindSeeded = false;

    TArray<FLeaf> LeafPool;
    TArray<FBlade> BladePool;
    TArray<FLeaf> DustPool;
    int32 LastBurstIdx = 0;   // round-robin cursor so bursts don't stomp each other
    // Cached tree locations (StaticMeshActors with tree meshes) so leaves shed
    // from actual canopies rather than drifting anywhere around the camera.
    TArray<FVector> TreePositions;
    TArray<float> TreeHeights;
    FVector LastCam = FVector::ZeroVector;
    bool bInitialized = false;
    // Season shapes the airborne foliage: autumn fills the sky with shed leaves
    // even in a light breeze, winter strips the canopies bare, spring is sparse
    // and green. (Leaf colour is a hermetic cue documented in world-design.md.)
    FBorn2FlapSeason Season;

    // Airborne leaf count tracks horizontal wind strength: calm air keeps a few
    // stragglers aloft, a gale strips the canopy. The pool stays MaxLeaves; the
    // inactive tail is parked at zero scale until stronger wind reveals it.
    static constexpr int32 MinLeaves = 50;
    static constexpr int32 MaxLeaves = 520;
    static constexpr int32 MaxBlades = 320;
    static constexpr int32 MaxDust = 640;
};