#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapRacing.generated.h"

class ABorn2FlapFlightPawn;
class ABorn2FlapGhost;

// One captured pose of a ghost flight. Positions are UE units (cm), times are
// seconds since round start, flaps are degrees.
struct FB2FGhostFrame
{
    double Time = 0;
    FVector Position = FVector::ZeroVector;
    FQuat Rotation = FQuat::Identity;
    float LeftFlap = 0, RightFlap = 0;
};

// A complete recorded round, replayable by a shadow doppelgänger.
struct FB2FGhostRecording
{
    FString Name;
    double Duration = 0;
    double Distance = 0;   // metres travelled
    float MaxSpeed = 0, MaxAltitude = 0;
    int32 GatesPassed = 0;
    TArray<FB2FGhostFrame> Frames;

    bool IsValid() const { return Frames.Num() >= 2 && Duration > 0; }
    // Interpolated pose at Time (clamped to [0, Duration]); false when empty.
    bool Sample(double Time, FVector &OutPos, FQuat &OutRot, float &OutLFlap, float &OutRFlap) const;
};

bool SaveGhostRecording(FB2FGhostRecording &Recording, const FString &Path);
bool LoadGhostRecording(const FString &Path, FB2FGhostRecording &Recording);

// Orchestrates the racing/ghost loop: records the player's flight each round,
// persists it to Saved/Racing/Ghosts, and spawns every past round as a ghost on
// the next launch. The single best round (longest distance) is the "champion"
// and is rendered in gold — the shadow you are trying to molt out of.
UCLASS()
class BORN2FLAP_API ABorn2FlapRacingManager : public AActor
{
    GENERATED_BODY()
  public:
    ABorn2FlapRacingManager();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    int32 GetRoundCount() const { return LoadedGhosts.Num(); }
    int32 GetGhostCount() const { return GhostActors.Num(); }
    double GetRoundTime() const { return RoundTime; }
    double GetActiveDistance() const { return ActiveRecording.Distance; }
    double GetBestDistance() const { return BestDistance; }
    double GetBestDuration() const { return BestDuration; }
    bool IsRecording() const { return bRecording; }
    bool IsSurpassingBest() const { return bRecording && BestDistance > 0 && ActiveRecording.Distance > BestDistance; }
    FString GetRacingStatus() const;
    FString GetBestLine() const;

  private:
    void LoadAllGhosts();
    void BeginRound();
    void EndRound();
    void SpawnGhosts();
    void SpawnGhost(const FB2FGhostRecording &Recording, bool bChampion);
    void ClearGhostActors();
    void RecordFrame(const ABorn2FlapFlightPawn *Pawn);

    TArray<FB2FGhostRecording> LoadedGhosts;
    TArray<TWeakObjectPtr<ABorn2FlapGhost>> GhostActors;
    FB2FGhostRecording ActiveRecording;
    TWeakObjectPtr<ABorn2FlapFlightPawn> Bird;
    bool bRecording = false;
    bool bEnabled = true;
    double RoundTime = 0;
    double BestDistance = 0, BestDuration = 0;
    FVector LastPosition = FVector::ZeroVector;
    FString GhostDir;
};