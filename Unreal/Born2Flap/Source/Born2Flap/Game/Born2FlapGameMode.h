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
class ABorn2FlapUIBridge;
class AStaticMeshActor;
class UMaterialInstanceDynamic;
class UTexture2D;
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
    bool IsMenuOpen() const { return bMenuOpen; }
    bool IsWater(double X,double Y) const;
    double WaterHeight() const;
    FVector WindAt(const FVector& P,double Time) const;
    FString GetLevelId() const { return bCoastLevel ? TEXT("Shiomori") : (bNatureLevel ? TEXT("Ravenstonefield") : TEXT("Training")); }
    const FBorn2FlapConditions& GetConditions() const { return Conditions; }
    double GroundHeight(double X, double Y) const;
    bool HasRadioTrack() const;
    // Radio station (owned here) — the menu reads/writes its user volume.
    UBorn2FlapRadioStation* GetRadioStation() const { return RadioStation; }
    // Points of interest: named reset/launch points per level. The start point
    // is chosen in the level-select menu (?PoiKey=/?PoiNum=) and resolved here.
    const TArray<FBorn2FlapPoi>& GetPOIs() const { return POIs; }
    int32 GetInitialPoiIndex() const;

    // Main menu / level selector. On a plain boot (no explicit level) it shows
    // after the splash; from inside a level the player re-opens it with F10.
    void ShowMainMenu(bool bInLevel);
    void CloseMainMenu();

    // --- Ruby Brain / UMGHAML (the live UI authoring path) ------------------
    // True once the Ruby Brain process is running and the C++ bridge is the
    // single UMG host: native panels (cockpit/menu/poi/radio/splash) stay
    // dormant and the Brain authors them from BuildBrainTelemetry().
    bool IsBrainActive() const;
    // Route a semantic component action (button/slider/select) from the Brain's
    // renderer into game logic. Mirrors ABorn2FlapMenu::OnAction + the POI
    // overlay handler so the Ruby path and the native path share one behaviour.
    void HandleBrainAction(const FString& Action, float Value, const FString& Text);
    // Full JSON telemetry for the Brain: panel visibility + menu catalog + POI
    // list + radio + RC + splash + flight readouts (single source of truth).
    FString BuildBrainTelemetry() const;
    // Menu world-select state (mirrors ABorn2FlapMenu so the Brain can author it).
    int32 GetMenuWorldIndex() const { return MenuWorldIndex; }
    const TMap<FString, FBorn2FlapConditions>& GetMenuLevelConditions() const { return MenuLevelConditions; }

  private:
    void PopulatePOIs();
    void AddPoi(const FString& Key, double X, double Y, float Clearance = 80.f);
    void AddPoi(const FString& Key, const FVector& Position, float Yaw = 0.f);

  private:
    void UpdateGateColors();

    TArray<FVector> Gates;
    TArray<TObjectPtr<AStaticMeshActor>> GateActors;
    TArray<TObjectPtr<UMaterialInstanceDynamic>> GateMaterials;
    TArray<TObjectPtr<UTexture2D>> GateRedTextures;    // active-gate artwork (4 variants)
    TArray<TObjectPtr<UTexture2D>> GateGreenTextures;  // passed-gate artwork (3 variants)
    TArray<TObjectPtr<UTexture2D>> GateQuasiTextures;  // ahead/ghost artwork (4 distance tiers)
    TArray<int32> GateRedVariant;     // per-gate random red pick
    TArray<int32> GateGreenVariant;   // per-gate random green pick
    int32 GatesPassed = 0;
    TArray<FBorn2FlapPoi> POIs;
    FString InitialPoiKey;    // ?PoiKey= travel token from the level-select menu
    int32 InitialPoiNumber = 0; // ?PoiNum= (gates), 0 = none
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

    // Ruby Brain bridge — when active it is the single UMG host (native panels
    // are suppressed and the Brain authors the full dashboard).
    UPROPERTY(Transient)
    TObjectPtr<ABorn2FlapUIBridge> BrainBridge;

    // Menu world-select state (mirrors ABorn2FlapMenu for the Brain path).
    int32 MenuWorldIndex = 0;
    int32 MenuPage = 0;
    int32 SettingsPage = 0;
    // Native-menu view persistence: F10 re-opens on the page (and FLIGHT DESK
    // sub-tab) the player last had open. The menu actor is destroyed on close,
    // so its NavPage / SettingsPage survive here across open/close.
    int32 MenuNavPage = 0;
    int32 MenuSettingsPage = 0;
    int32 MenuPrefsPage = 0;
    TMap<FString, FBorn2FlapConditions> MenuLevelConditions;
    TMap<FString, int32> MenuSelectedPoi;
    bool bMenuInLevel = false;

    // Startup splash (image + loading bar), shown on real launches only.
    UPROPERTY(Transient)
    TObjectPtr<ABorn2FlapSplash> SplashWidget;
    float SplashElapsed = -1.f;

    // Main menu / level selector, opened once the splash fades on a plain boot
    // (no ?Level=, no ?SkipMenu=1). Selecting a level re-opens with SkipMenu.
    // F10 re-opens it from inside a level (bSkipMenu true, menu closed).
    UPROPERTY(Transient)
    TObjectPtr<ABorn2FlapMenu> MenuWidget;
    bool bSkipMenu = false;
    bool bMenuOpen = false;

    // True when a plain interactive boot lands on the minimal Entry map (the
    // home screen). No world geometry or bird is built — a real level only
    // loads when the player selects a world and clicks FLY (OpenLevel).
    bool bMenuMode = false;

    // Slate pre-processor: captures F10 while the menu is open (UIOnly input
    // mode routes the key away from PlayerInput). Registered in BeginPlay.
    TSharedPtr<IInputProcessor> MenuKeyProcessor;

    // Gate-race state (Training course).
    bool bRaceRunning = false;
    double RaceTime = 0;
    double BestLapTime = 0;
};