#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapNeonVoid.generated.h"

class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class USceneComponent;

// Neon Abyss runtime director. The saved void map owns the cosmos backdrop and
// the Sanxingdui bronze ring; this actor owns what *moves* — the aether drift
// current, the neon jellyfish population, and the capture festival (fly-through
// netting). It is the C++ half of the capture game mode: it detects captures and
// exposes raw festival telemetry; the Ruby Brain's capture ruleset owns scoring.
UCLASS()
class BORN2FLAP_API ABorn2FlapNeonVoid : public AActor
{
    GENERATED_BODY()
  public:
    ABorn2FlapNeonVoid();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    // Capture festival telemetry (read by the Brain capture ruleset and the HUD).
    int32 GetCaptured() const { return Captured; }
    int32 GetTargetCount() const { return TargetCount; }
    int32 GetCombo() const { return Combo; }
    float GetTimeRemaining() const { return FestivalTime; }
    float GetBestScore() const { return BestScore; }
    bool IsFestivalRunning() const { return bFestivalRunning; }
    int32 GetActiveJellyfish() const { return ActiveCount; }
    // Raw per-tick capture score increment (count + speed + combo) for the HUD.
    int32 GetLastCaptureScore() const { return LastCaptureScore; }

  private:
    struct FJelly
    {
        TObjectPtr<USceneComponent> Root;
        TObjectPtr<UProceduralMeshComponent> Mesh;
        FVector Origin = FVector::ZeroVector;
        FVector Phase = FVector::ZeroVector;   // per-jelly pulse/drift phase
        FVector Drift = FVector::ZeroVector;    // slow orbital velocity
        float Scale = 1.f;
        bool bCaptured = false;
        float Respawn = 0.f;
    };

    void SpawnJellyfish(const FVector& Location);
    void BuildJellyMesh(UProceduralMeshComponent* Mesh);
    void Capture(FJelly& Jelly);
    FVector BirdLocation() const;
    float BirdSpeed() const;
    void UpdateFestival(float DeltaSeconds);

    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Swarm;
    TArray<FJelly> Jellies;
    TArray<FVector> SwarmPositions;
    TArray<FVector> SwarmDrift;
    FRandomStream Rng{1337};

    int32 TargetCount = 12;
    int32 Captured = 0;
    int32 Combo = 0;
    int32 ActiveCount = 0;
    int32 LastCaptureScore = 0;
    float ComboTime = 0.f;
    float FestivalTime = 120.f;
    float BestScore = 0.f;
    float Score = 0.f;
    bool bFestivalRunning = true;
    float CaptureRadius = 320.f;   // cm — a soft scoop, not a collision
};
