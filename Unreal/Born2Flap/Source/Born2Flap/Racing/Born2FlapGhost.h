#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapRacing.h"
#include "Born2FlapGhost.generated.h"

// A replay "shadow doppelgänger": flies one recorded round alongside the player.
// Normal ghosts render as an ink-dark silhouette; the champion renders gold.
UCLASS()
class BORN2FLAP_API ABorn2FlapGhost : public AActor
{
    GENERATED_BODY()
  public:
    ABorn2FlapGhost();
    void SetupGhost(const FB2FGhostRecording &InRecording, bool bIsChampion);
    void Advance(double DeltaSeconds);

  private:
    UPROPERTY() TObjectPtr<USceneComponent> LeftShoulder;
    UPROPERTY() TObjectPtr<USceneComponent> RightShoulder;
    FB2FGhostRecording Recording;
    bool bChampion = false;
    bool bReady = false;
    double GhostTime = 0;
};
