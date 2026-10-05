#pragma once
// Born2FlapPoiOverlay.h — the lightweight F8 "points of interest" overlay.
//
// Lists the current level's points of interest (already populated by the
// GameMode and copied onto the FlightPawn at spawn) as a small centred card.
// Selecting one teleports the bird there (SelectPoi -> ResetFlight) and closes
// the overlay. It is the in-flight companion to the level-select menu's
// "TRAVEL TO" list, but it re-places the bird in the live world instead of
// loading a map.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapPoiOverlay.generated.h"

class UBorn2FlapUIRenderer;
class ABorn2FlapFlightPawn;

UCLASS()
class BORN2FLAP_API ABorn2FlapPoiOverlay : public AActor
{
    GENERATED_BODY()

public:
    ABorn2FlapPoiOverlay();

    // Spawn-time wiring: build the POI list and bind the action callback.
    void Open(ABorn2FlapFlightPawn* InBird);

    // Detach the UMG window immediately (Destroy() only schedules GC).
    void Close();

private:
    void BuildTree();

    UFUNCTION()
    void HandleAction(const FString& Action, float Value, const FString& Text);

    UPROPERTY() TObjectPtr<UBorn2FlapUIRenderer> Renderer;
    TWeakObjectPtr<ABorn2FlapFlightPawn> Bird;
};
