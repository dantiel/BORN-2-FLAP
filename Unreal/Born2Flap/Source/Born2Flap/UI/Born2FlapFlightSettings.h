#pragma once
// Born2FlapFlightSettings.h — the semantic "flight desk" settings panel.
//
// Replaces the old raw-Slate SFlightSettings with the react-native-umg view
// framework (UBorn2FlapUIRenderer + semantic components). The tree is authored
// once (Panel / Button / Toggle / Slider / ScrollBox) and every interactive
// control reports back through OnComponentAction with a semantic action key
// ("bird.model.0", "mouse.gain.1", "tuning.ServoSpeed", …). This actor routes
// those keys into the FlightPawn — it never styles pixels.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapFlightSettings.generated.h"

class UBorn2FlapUIRenderer;
class ABorn2FlapFlightPawn;

UCLASS()
class BORN2FLAP_API ABorn2FlapFlightSettings : public AActor
{
    GENERATED_BODY()

public:
    ABorn2FlapFlightSettings();

    // Spawn-time wiring: bind input (Esc/F8 close) and build the panel tree.
    void Open(ABorn2FlapFlightPawn* InBird);

    // Detach the panel's UMG window from the viewport immediately. Called by the
    // FlightPawn before Destroy() so the window disappears this frame instead of
    // lingering on screen until the next garbage collection.
    void Close();

    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void BuildTree();
    void RefreshModelButtons();
    void RefreshMouseGains();

    UFUNCTION()
    void HandleAction(const FString& Action, float Value, const FString& Text);

    UFUNCTION()
    void HandleClose();

    UPROPERTY() TObjectPtr<UBorn2FlapUIRenderer> Renderer;
    TWeakObjectPtr<ABorn2FlapFlightPawn> Bird;

    // Re-drive targets (paths into the tree) for controls whose displayed state
    // can change without a user drag on that exact widget (model selection,
    // "reset mouse response").
    TArray<TArray<int32>> ModelButtons;
    TArray<TArray<int32>> MouseGainSliders;
};