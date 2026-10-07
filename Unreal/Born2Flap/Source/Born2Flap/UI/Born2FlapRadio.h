#pragma once
// Born2FlapRadio.h — the unified, level-configurable radio.
//
// One implementation drives every level (Shiomori, Ravenstonefield, …):
//   Config/DefaultGame.ini  ──▶  UBorn2FlapRadioSettings (per-level playlists)
//                                     │
//   UBorn2FlapRadioStation  ── the audio half: a gapless, crossfading player
//                                     │  (two UAudioComponents overlap so tracks
//                                     │   never leave a silence gap)
//   ABorn2FlapRadioHUD      ── the face: the semantic UMG panel (new UI system)
//
// The playlist is plain data the user can edit later — add/remove/reorder the
// Tracks=("...") paths under a level's +LevelRadios= line.

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Born2FlapRadio.generated.h"

class UBorn2FlapUIRenderer;
class ABorn2FlapFlightPawn;

// One level's radio: a named station + an ordered track list (asset paths).
USTRUCT()
struct FBorn2FlapLevelRadio
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Config, Category = "Radio") FString LevelName;
    UPROPERTY(EditAnywhere, Config, Category = "Radio") FString StationName;
    UPROPERTY(EditAnywhere, Config, Category = "Radio") TArray<FString> Tracks;
    // Per-track cover art (asset ref, index-aligned with Tracks). Empty = auto-detect
    // from the track's folder/name; if nothing is found a placeholder is shown.
    UPROPERTY(EditAnywhere, Config, Category = "Radio") TArray<FString> Covers;
    UPROPERTY(EditAnywhere, Config, Category = "Radio") float Volume = 0.5f;
    UPROPERTY(EditAnywhere, Config, Category = "Radio") float CrossfadeSeconds = 2.0f;
};

// Data-driven radio configuration. Editable in Config/DefaultGame.ini:
//   [/Script/Born2Flap.Born2FlapRadioSettings]
//   +LevelRadios=(LevelName="Shiomori",StationName="SHIOMORI BAY / BEACH RADIO",
//                 Volume=0.5,Tracks=("/Game/Shiomori/Audio/SHIOMORI_BAY_I",
//                                    "/Game/Shiomori/Audio/SHIOMORI_BAY_II"))
UCLASS(config=Game, defaultconfig)
class BORN2FLAP_API UBorn2FlapRadioSettings : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Config, Category = "Radio")
    TArray<FBorn2FlapLevelRadio> LevelRadios;
};

// The audio half: a gapless playlist player. Owned by the GameMode; drives two
// transient UAudioComponents so the next track fades in while the current one
// fades out (classic radio — no silence between tracks).
UCLASS()
class BORN2FLAP_API UBorn2FlapRadioStation : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(UWorld* World, const FString& LevelName);
    void Tick(float DeltaSeconds);

    void TogglePlayPause();
    void SetPaused(bool bPaused);
    void AdjustVolume(float Delta);

    bool HasTrack() const { return Playlist.Num() > 0; }
    bool IsPlaying() const { return bPlaying; }
    float GetVolume() const { return Volume; }
    FString GetStationName() const { return StationName; }
    FString GetTrackName() const;
    FString GetCoverPath() const;

private:
    void StartTrack(int32 Index, float StartVolume);
    FString AutoDetectCover(const FString& TrackPath) const;
    static bool CoverExists(const FString& AssetRef);

    UPROPERTY(Transient) TObjectPtr<UAudioComponent> Active = nullptr;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> Fader = nullptr;
    // UPROPERTY keeps the SoundWaves referenced so the GC does not collect them
    // (they are only loaded via LoadObject, which returns non-rooted pointers).
    UPROPERTY(Transient) TArray<TObjectPtr<USoundWave>> Playlist;
    // Resolved cover-art asset refs, index-aligned with Playlist (empty = none).
    UPROPERTY(Transient) TArray<FString> TrackCovers;
    FString StationName = TEXT("FIELD RADIO");
    int32 TrackIndex = 0;
    bool bPlaying = true;
    float Volume = 0.5f;
    float Crossfade = 2.0f;
    float TrackStartTime = 0.0f;
    float TrackDuration = 0.0f;
    bool bCrossfading = false;
    float CrossfadeElapsed = 0.0f;
};

// The in-game face: builds the semantic "radio" panel on the new UI system and
// re-drives it each tick from the station (track name + volume + play state).
UCLASS()
class BORN2FLAP_API ABorn2FlapRadioHUD : public AActor
{
    GENERATED_BODY()

public:
    ABorn2FlapRadioHUD();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    void SetStation(UBorn2FlapRadioStation* InStation);

private:
    void BuildPanel();
    void Refresh();

    UPROPERTY() TObjectPtr<UBorn2FlapUIRenderer> Renderer;
    UPROPERTY() TObjectPtr<UBorn2FlapRadioStation> Station;
    ABorn2FlapFlightPawn* CachedBird = nullptr;
    FString LastCoverPath;
    FString LastTrackLine;
    int32 LastVolumePct = -1;
    bool bLastHidden = true;
};