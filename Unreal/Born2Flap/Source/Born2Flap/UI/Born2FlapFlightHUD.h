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
    void BuildCockpit();
    void Refresh();

    UPROPERTY() TObjectPtr<UBorn2FlapUIRenderer> Renderer;
    ABorn2FlapFlightPawn* CachedBird = nullptr;

    // Last-emitted readout values — lets Refresh skip no-op update_props ops
    // (the reconciler stays minimal, matching the Brain's live-propagation ideal).
    float LastAlt = -1e9f;
    float LastClimb = -1e9f;
    float LastSpeed = -1e9f;
    float LastBattery = -1e9f;
    float LastThrottle = -1e9f;
    float LastWindSpeed = -1e9f;
    float LastWindRelAngle = -1e9f;
    FString LastWindDesc;
    FString LastStatus;
    FString LastCamera;
    bool bLastHidden = true;
};