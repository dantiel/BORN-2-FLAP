#pragma once
// Born2FlapSplash.h — the startup splash, rebuilt on the view framework.
//
// Replaces the old hand-built UUserWidget with an actor that drives
// UBorn2FlapUIRenderer (full-bleed Image + a bottom-anchored loading readout),
// so every on-screen surface now uses the same react-native-umg grammar.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapSplash.generated.h"

class UBorn2FlapUIRenderer;

UCLASS()
class BORN2FLAP_API ABorn2FlapSplash : public AActor
{
    GENERATED_BODY()

public:
    ABorn2FlapSplash();

    virtual void BeginPlay() override;

    // Drive the loading bar 0..1 and refresh the percentage label.
    void SetProgress(float Fraction);

    // Fade the whole splash (root opacity).
    void SetOpacity(float Opacity);

    // Detach the UMG window immediately (Destroy() only schedules GC — the
    // full-screen splash would otherwise linger at ZOrder 100 and swallow every
    // mouse click on the menu/panels beneath it until the next collection).
    void Close();

private:
    void Build();

    UPROPERTY() TObjectPtr<UBorn2FlapUIRenderer> Renderer;
};