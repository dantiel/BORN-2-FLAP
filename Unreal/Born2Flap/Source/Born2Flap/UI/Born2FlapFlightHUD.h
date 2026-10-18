#pragma once
// Born2FlapFlightHUD.h — the glass cockpit: the native driver of the semantic
// flight HUD (the "magnum opus" UI).
//
// Where the Ruby Brain *authors* screens (UMGHAML → reconciler → wire), this
// actor is the *in-game* host that builds the same semantic component tree
// (Panel / Stat / Gauge / Banner) once and re-drives it every tick from real
// FlightPawn telemetry. It talks to UBorn2FlapUIRenderer through the same
// FOp/FNode wire model the wire uses — so the native HUD and a remote Brain
// are interchangeable faces of one grammar.
//
// Spawned by ABorn2FlapGameMode::BeginPlay. Purely visual; no game state.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapFlightHUD.generated.h"

class UBorn2FlapUIRenderer;
class ABorn2FlapFlightPawn;

UCLASS()
class BORN2FLAP_API ABorn2FlapFlightHUD : public AActor
{
    GENERATED_BODY()

public:
    ABorn2FlapFlightHUD();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UFUNCTION()
    void HandleComponentAction(const FString& Action, float Value, const FString& Text);
    void BuildCockpit();
    void Refresh();

    UPROPERTY() TObjectPtr<UBorn2FlapUIRenderer> Renderer;
    ABorn2FlapFlightPawn* CachedBird = nullptr;

    // Last-emitted readouts, cached as the *displayed* strings: Refresh only
    // re-drives a stat when what the pilot actually reads changes — oscillating
    // telemetry (e.g. climb flickering ±0.1 m/s) no longer shakes the digits.
    FString LastAlt;
    FString LastClimb;
    FString LastSpeed;
    FString LastBattery;
    FString LastThrottle;
    FString LastWindSpeed;
    float LastWindRelAngle = -1e9f;
    FString LastWindDesc;
    FString LastStatus;
    FString LastCamera;
    // "HIDE" overlay button collapses the readout column (logo + panels +
    // banners) but keeps the THROW/MENU/HIDE button row so it can be re-opened.
    bool bReadoutsHidden = false;
    bool bLastHidden = true;
};