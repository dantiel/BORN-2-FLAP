// Born2FlapPoiBeacon.h — a floating marker + label that makes a POI visible in
// the world, and brightens when it is the currently-selected reset point.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapPoiBeacon.generated.h"

class UPointLightComponent;
class UTextRenderComponent;

UCLASS()
class BORN2FLAP_API ABorn2FlapPoiBeacon : public AActor
{
    GENERATED_BODY()
  public:
    ABorn2FlapPoiBeacon();
    void Initialize(const FString& InLabel, const FVector& InPosition);
    // The selected reset point reads brighter/warmer so the player can see it.
    void SetSelected(bool bSelected);

  private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
};
