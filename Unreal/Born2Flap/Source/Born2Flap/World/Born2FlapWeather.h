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
struct FBorn2FlapConditions
{
    FString Weather, Time;
    // Wind overrides (per-level, persisted). Negative = "auto": follow the
    // weather/time profile defaults. Explicit non-negative values are the
    // player's further tuning on top of those defaults.
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
    TArray<FString> WeatherOptions(const FString& Level);
    TArray<FString> TimeOptions(const FString& Level, const FString& Weather=FString());
    FBorn2FlapConditions Defaults(const FString& Level);
    FBorn2FlapConditions Validate(const FString& Level, const FBorn2FlapConditions& Conditions);
    const FBorn2FlapWeather& Profile(const FString& Id);
    const FBorn2FlapDayTime& DayTime(const FString& Id);
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
    void Respawn(int32 Index, const FVector& Camera);
    float SurfaceHeight(const FVector& Position) const;
    void UpdateSurfaces(float DeltaSeconds);
    void CheckTest();
    void CheckExperienceTest();
    FString Level;
    FBorn2FlapConditions Conditions;
    bool bApplied=false, bCaptured=false, bTestDone=false;
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
    UPROPERTY() TObjectPtr<UVolumetricCloudComponent> Clouds;
    UPROPERTY() TObjectPtr<UPostProcessComponent> PostProcess;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HaloMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;
};