#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapRacing.generated.h"

class ABorn2FlapFlightPawn;
class ABorn2FlapSpirit;

// One captured pose of a replay-spirit flight. Positions are UE units (cm),
// times are seconds since round start, flaps are degrees.
struct FB2FSpiritFrame
{
    double Time = 0;
    FVector Position = FVector::ZeroVector;
    FQuat Rotation = FQuat::Identity;
    float LeftFlap = 0, RightFlap = 0;
};

// A complete recorded round, replayable by a shadow doppelgänger.
struct FB2FSpiritRecording
{
    FString Name;
    double Duration = 0;
    double Distance = 0;   // metres travelled
    float MaxSpeed = 0, MaxAltitude = 0;
    int32 GatesPassed = 0;
    // Absolute path of the .b2fs file this round was (or will be) saved to.
    // Not serialized — it lives only in memory to allow culling the worst file.
    FString SourceFile;
    TArray<FB2FSpiritFrame> Frames;

    bool IsValid() const { return Frames.Num() >= 2 && Duration > 0; }
    // Interpolated pose at Time (clamped to [0, Duration]); false when empty.
    bool Sample(double Time, FVector &OutPos, FQuat &OutRot, float &OutLFlap, float &OutRFlap) const;
};

bool SaveSpiritRecording(FB2FSpiritRecording &Recording, const FString &Path);
bool LoadSpiritRecording(const FString &Path, FB2FSpiritRecording &Recording);

// Orchestrates the racing/replay-spirit loop: records the player's flight each
// round, persists it to Saved/Racing/Spirits, and spawns every past round as a
// spirit on the next launch. The single best round (longest distance) is the
// "champion" and is rendered in gold — the shadow you are trying to molt out of.
UCLASS()
class BORN2FLAP_API ABorn2FlapRacingManager : public AActor
{
    GENERATED_BODY()
  public:
    ABorn2FlapRacingManager();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    int32 GetRoundCount() const { return LoadedSpirits.Num(); }
    int32 GetSpiritCount() const { return SpiritActors.Num(); }
    double GetRoundTime() const { return RoundTime; }
    double GetActiveDistance() const { return ActiveRecording.Distance; }
    double GetBestDistance() const { return BestDistance; }
    double GetBestDuration() const { return BestDuration; }
    bool IsRecording() const { return bRecording; }
    bool IsSurpassingBest() const { return bRecording && BestDistance > 0 && ActiveRecording.Distance > BestDistance; }
    FString GetRacingStatus() const;
    FString GetBestLine() const;
    // Shadow-doppelgänger visibility: on by default, can be toggled off in the
    // flight desk and all saved recordings can be purged from disk.
    bool IsSpiritsEnabled() const { return bSpiritsEnabled; }
    void SetSpiritsEnabled(bool bOn);
    void DeleteAllSpirits();

  private:
    void LoadAllSpirits();
    void TrimToLimit();
    void BeginRound();
    void EndRound();
    void SpawnSpirits();
    void SpawnSpirit(const FB2FSpiritRecording &Recording, bool bChampion);
    void ClearSpiritActors();
    void RecordFrame(const ABorn2FlapFlightPawn *Pawn);

    TArray<FB2FSpiritRecording> LoadedSpirits;
    TArray<TWeakObjectPtr<ABorn2FlapSpirit>> SpiritActors;
    FB2FSpiritRecording ActiveRecording;
    TWeakObjectPtr<ABorn2FlapFlightPawn> Bird;
    bool bRecording = false;
    bool bEnabled = true;
    bool bSpiritsEnabled = true;
    double RoundTime = 0;
    double BestDistance = 0, BestDuration = 0;
    FVector LastPosition = FVector::ZeroVector;
    FString SpiritDir;
};