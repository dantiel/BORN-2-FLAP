#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapRacing.h"
#include "Born2FlapSpirit.generated.h"

// A replay "shadow doppelgänger": flies one recorded round alongside the player.
// Normal spirits render as an ink-dark silhouette; the champion renders gold.
UCLASS()
class BORN2FLAP_API ABorn2FlapSpirit : public AActor
{
    GENERATED_BODY()
  public:
    ABorn2FlapSpirit();
    void SetupSpirit(const FB2FSpiritRecording &InRecording, bool bIsChampion);
    void Advance(double DeltaSeconds);

  private:
    UPROPERTY() TObjectPtr<USceneComponent> RavenRoot;
    UPROPERTY() TObjectPtr<USceneComponent> LeftShoulder;
    UPROPERTY() TObjectPtr<USceneComponent> RightShoulder;
    UPROPERTY() TObjectPtr<USceneComponent> TailPivot;
    FB2FSpiritRecording Recording;
    bool bChampion = false;
    bool bReady = false;
    double SpiritTime = 0;
};