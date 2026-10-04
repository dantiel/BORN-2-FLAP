#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Born2FlapPoi.h"
#include "World/Born2FlapWeather.h"
#include "Born2FlapGameMode.generated.h"

class UBorn2FlapRadioStation;
class ABorn2FlapRadioHUD;
class ABorn2FlapSplash;
class ABorn2FlapMenu;
class AStaticMeshActor;
class UMaterialInstanceDynamic;
class ABorn2FlapPoiBeacon;
class IInputProcessor;

UCLASS()
class BORN2FLAP_API ABorn2FlapGameMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    ABorn2FlapGameMode();
    virtual void InitGame(const FString &MapName, const FString &Options, FString &ErrorMessage) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    int32 GetGatesPassed() const { return GatesPassed; }
    int32 GetGateCount() const { return Gates.Num(); }
    bool IsNatureLevel() const { return bNatureLevel; }
    // Gate-race telemetry (Training course). RaceTime runs while the bird is
    // airborne; a completed lap stops it and records the best time.
    double GetRaceTime() const { return RaceTime; }
    double GetBestLapTime() const { return BestLapTime; }
    bool IsRaceComplete() const { return Gates.Num() > 0 && GatesPassed >= Gates.Num(); }
    bool IsCoastLevel() const { return bCoastLevel; }
    bool IsWater(double X,double Y) const;
    double WaterHeight() const;
    FVector WindAt(const FVector& P,double Time) const;
    FString GetLevelId() const { return bCoastLevel ? TEXT("Shiomori") : (bNatureLevel ? TEXT("Ravenstonefield") : TEXT("Training")); }
    const FBorn2FlapConditions& GetConditions() const { return Conditions; }
    double GroundHeight(double X, double Y) const;
    bool HasRadioTrack() const;
    // Points of interest: named, selectable reset/launch points per level.
    const TArray<FBorn2FlapPoi>& GetPOIs() const { return POIs; }
    void HighlightPoi(int32 Index);

    // Main menu / level selector. On a plain boot (no explicit level) it shows
    // after the splash; from inside a level the player re-opens it with Escape.
    void ShowMainMenu(bool bInLevel);
    void CloseMainMenu();

  private:
    void PopulatePOIs();
    void SpawnPoiBeacons();
    void AddPoi(const FString& Key, double X, double Y, float Clearance = 80.f);
    void AddPoi(const FString& Key, const FVector& Position, float Yaw = 0.f);

  private:
    void UpdateGateColors();

    TArray<FVector> Gates;
    TArray<TObjectPtr<AStaticMeshActor>> GateActors;
    TArray<TObjectPtr<UMaterialInstanceDynamic>> GateMaterials;
    int32 GatesPassed = 0;
    TArray<FBorn2FlapPoi> POIs;
    TArray<TObjectPtr<ABorn2FlapPoiBeacon>> PoiBeacons;
    bool bNatureLevel = true;
    bool bCoastLevel = false;
    FBorn2FlapConditions Conditions;
    UPROPERTY() TObjectPtr<ABorn2FlapWeather> WeatherActor;
    float CoastTestTime=0;
    int32 CoastCaptureStage=0;
    UPROPERTY(Transient)
    TObjectPtr<UBorn2FlapRadioStation> RadioStation;
    UPROPERTY(Transient)
    TObjectPtr<ABorn2FlapRadioHUD> RadioHUD;

    // Startup splash (image + loading bar), shown on real launches only.
    UPROPERTY(Transient)
    TObjectPtr<ABorn2FlapSplash> SplashWidget;
    float SplashElapsed = -1.f;

    // Main menu / level selector, opened once the splash fades on a plain boot
    // (no ?Level=, no ?SkipMenu=1). Selecting a level re-opens with SkipMenu.
    // Escape re-opens it from inside a level (bSkipMenu true, menu closed).
    UPROPERTY(Transient)
    TObjectPtr<ABorn2FlapMenu> MenuWidget;
    bool bSkipMenu = false;
    bool bMenuOpen = false;

    // Slate pre-processor: captures Escape while the menu is open (UIOnly input
    // mode routes the key away from PlayerInput). Registered in BeginPlay.
    TSharedPtr<IInputProcessor> MenuKeyProcessor;

    // Gate-race state (Training course).
    bool bRaceRunning = false;
    double RaceTime = 0;
    double BestLapTime = 0;
};
