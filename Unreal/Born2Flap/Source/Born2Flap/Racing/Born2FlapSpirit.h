#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapRacing.h"
#include "Born2FlapSpirit.generated.h"

// A replay ghost: flies one recorded round alongside the player, rebuilding the
// original bird model (RavenCrow / Prototype / Kestrel). Normal ghosts render as
// a grayed-out, slightly translucent silhouette; the champion renders gold.
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
    void BuildPrototype();
    FB2FSpiritRecording Recording;
    int32 BirdModel = 0;
    bool bChampion = false;
    bool bReady = false;
    double SpiritTime = 0;
};