#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapWeather.generated.h"

struct FBorn2FlapWeather
{
    FString Id, Label;
    float WindSpeed, Gust, Thermals, Cloud, Fog, Sunlight;
    bool Rain=false, Snow=false, Parhelion=false;
};
struct FBorn2FlapDayTime
{
    FString Id, Label;
    float Elevation, Yaw, Lux, Exposure;
    bool Night=false;
};
// A season is a single "stage-art" cue, not a simulation parameter. Choosing one
// hermetically re-derives the whole living world — sun path, light spectrum,
// fog, insect community, foliage and (in future) the radio playlist — from a
// handful of hidden numbers below. Players never see these fields; they only
// pick Spring/Summer/Autumn/Winter and the scene composes itself.
struct FBorn2FlapSeason
{
    FString Id, Label;
    // Sky & light — how the season bends the sun path and its spectrum.
    float SunAzimuth = 0.f;      // +deg yaw bias (shifts the sun's daily arc)
    float SunElevation = 0.f;    // +deg elevation bias (low winter, high summer sun)
    float SunLux = 1.f;          // lux multiplier (weaker winter light)
    FLinearColor LightTint{1.f,1.f,1.f}; // spectral tint layered over the sun colour
    float SkyIntensity = 1.f;    // skylight multiplier
    float FogBias = 0.f;         // added fog density (autumn haze, crisp winter air)
    float ExposureBias = 0.f;    // added exposure stops
    // Living world abundance.
    float InsectFactor = 1.f;    // overall insect population scale
    float LeafFactor = 1.f;      // airborne foliage density
    float LeafShed = 1.f;        // how readily canopies shed (autumn leaf-storm)
    FLinearColor LeafTint{1.f,1.f,1.f}; // foliage colour cue (hermetic, documented)
    // Weather affinity — the season's most natural weather, plus snowfall bias.
    FString NativeWeather;       // the weather this season drifts toward
    float SnowBias = 0.f;        // deepens winter snow, warms summer rain
    // (future) radio cue — seasonal soundtrack playlist hint.
    FString RadioCue;
    // Morning ground-mist affinity (0..1): how readily valley floors, river banks
    // and oases exhale low-lying mist banks at dawn/morning. Hermetic — the player
    // only picks the season; this one number sets where morning mist lives.
    float MorningMist = 0.f;
    // Wind narrative — the season's prevailing-wind defaults. These are the base
    // multipliers the weather/time profile rides on, and the player's explicit
    // conditions overrides (non-negative) multiply on top as before. Negative
    // override = "auto" → follow these season defaults (then weather, then time).
    float WindSpeedScale = 1.f;   // multiplier on base horizontal wind strength
    float WindGustScale = 1.f;    // multiplier on gust turbulence magnitude
    float WindRandom = 1.f;       // heading wander / directional randomness (1 → .12/.06 rad drift)
    float WindThermalScale = 1.f; // multiplier on thermal updraft strength
    float WindDirBias = 0.f;      // +deg added to the level's prevailing wind heading
};
struct FBorn2FlapConditions
{
    FString Weather, Time;
    // Season — "" until validated, then a concrete season id. A single visible
    // knob that cascades into the whole hidden stage art (see FBorn2FlapSeason).
    FString Season;
    // Wind overrides (per-level, persisted). Negative = "auto": follow the
    // season's prevailing-wind defaults (then the weather/time profile).
    // Explicit non-negative values are the player's further tuning on top.
    //   WindDir      — compass heading the wind blows toward, degrees 0..360
    //   WindSpeed    — multiplier on base horizontal wind strength
    //   WindGust     — multiplier on gust turbulence magnitude
    //   WindRandom   — multiplier on heading drift / directional wander
    //   WindThermal  — multiplier on thermal updraft strength
    float WindDir = -1.f;
    float WindSpeed = -1.f;
    float WindGust = -1.f;
    float WindRandom = -1.f;
    float WindThermal = -1.f;
};
namespace Born2FlapWeather
{
    const TArray<FBorn2FlapWeather>& Profiles();
    const TArray<FBorn2FlapDayTime>& DayTimes();
    const TArray<FBorn2FlapSeason>& Seasons();
    TArray<FString> WeatherOptions(const FString& Level);
    TArray<FString> TimeOptions(const FString& Level, const FString& Weather=FString());
    TArray<FString> SeasonOptions(const FString& Level);
    FString DefaultSeason(const FString& Level);
    FBorn2FlapConditions Defaults(const FString& Level);
    FBorn2FlapConditions Validate(const FString& Level, const FBorn2FlapConditions& Conditions);
    const FBorn2FlapWeather& Profile(const FString& Id);
    const FBorn2FlapDayTime& DayTime(const FString& Id);
    const FBorn2FlapSeason& Season(const FString& Id);
    FBorn2FlapConditions Load(const FString& Level);
    void Save(const FString& Level, const FBorn2FlapConditions& Conditions);
    FString TravelOptions(const FString& Level, const FBorn2FlapConditions& Conditions);
    FVector Wind(const FString& Level, const FBorn2FlapConditions& Conditions, const FVector& P, double Time);
    float Exposure(const FBorn2FlapConditions& Conditions);
    // The level's base wind heading in degrees (used as the "auto" direction
    // default and by the menu to label the wind-direction override).
    float DefaultWindDirection(const FString& Level);
}

class UInstancedStaticMeshComponent;
class UVolumetricCloudComponent;
class UPostProcessComponent;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UStaticMesh;
class UMaterialInterface;
class UNiagaraComponent;

UCLASS()
class BORN2FLAP_API ABorn2FlapWeather : public AActor
{
    GENERATED_BODY()
public:
    ABorn2FlapWeather();
    virtual void Tick(float DeltaSeconds) override;
    void Configure(const FString& InLevel, const FBorn2FlapConditions& InConditions);
    bool IsApplied() const { return bApplied; }
private:
    void Apply();
    void EnsureMobileSky();
    void Respawn(int32 Index, const FVector& Camera);
    float SurfaceHeight(const FVector& Position) const;
    void UpdateSurfaces(float DeltaSeconds);
    void UpdateMist(const FVector& Camera, float DeltaSeconds);
    float MorningMistAmount() const;
    void CollectMistAnchors(const FVector& Camera, TArray<FVector>& Out, int32 MaxCount);
    void CheckTest();
    void CheckExperienceTest();
    FString Level;
    FBorn2FlapConditions Conditions;
    bool bApplied=false, bCaptured=false, bTestDone=false, bMobileSkySpawned=false;
    float Elapsed=0;
    FRandomStream Random{4202026};
    TArray<FVector> Positions;
    TArray<float> Floors;
    TArray<FVector> SplashPositions;
    TArray<float> SplashAges;
    int32 NextSplash=0, SurfaceFrame=0;
    float Wetness=0, SnowCoverage=0;
    int32 ExperienceCapture=0;
    UPROPERTY() TObjectPtr<AActor> ExperienceCamera;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Splashes;
    UPROPERTY() TObjectPtr<UMaterialParameterCollection> SurfaceCollection;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Precipitation;
    // GPU-Niagara replacement for Precipitation (HIGH/EPIC only). The ISM path
    // above stays as the software fallback for LOW/MEDIUM.
    UPROPERTY() TObjectPtr<UNiagaraComponent> PrecipitationFX;
    bool bNiagara=false;
    // Localized morning mist banks (valley floors / water bodies). Each bank is a
    // local-space Niagara system anchored near a depression; opacity fades with
    // MPC_Weather.MistAmount, gated by day-time + season.
    UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> MistBanks;
    TArray<FVector> MistAnchors;
    float MistAnchorRefresh=0.f;
    UPROPERTY() TObjectPtr<UVolumetricCloudComponent> Clouds;
    UPROPERTY() TObjectPtr<UPostProcessComponent> PostProcess;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HaloMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;
    // Yunsha's desert-sky optics (dust aureole, dust sun-pillar, horizon mirage,
    // night star field). The gorge's analogue of HaloMaterial (M_SunParhelion).
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> YunshaSkyMaterial;
    // Mobile sky-sphere dome (cooked via CDO reference — SkyAtmosphere is SM5-only).
    UPROPERTY() TObjectPtr<UStaticMesh> MobileSkyMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> MobileSkyMaterial;
    // The spawned dome actor, re-centred on the camera each tick.
    UPROPERTY() TObjectPtr<AActor> MobileSkyDome;
};