#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Born2FlapHUD.generated.h"
class FBorn2FlapRcController;
class ABorn2FlapFlightPawn;
UCLASS()
class BORN2FLAP_API ABorn2FlapHUD : public AHUD
{
    GENERATED_BODY()
  public:
    virtual void DrawHUD() override;

  private:
    bool bHideRavenHUD = false;
    bool bShowChannels = true;
    void DrawChannels(const ABorn2FlapFlightPawn& Bird);
    void DrawRcPanel(const FBorn2FlapRcController &Rc);
};
