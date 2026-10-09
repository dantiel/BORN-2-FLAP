#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Born2FlapHUD.generated.h"
class FBorn2FlapRcController;
class ABorn2FlapFlightPawn;
class ABorn2FlapRacingManager;
UCLASS()
class BORN2FLAP_API ABorn2FlapHUD : public AHUD
{
    GENERATED_BODY()
  public:
    virtual void DrawHUD() override;

  private:
    bool bShowChannels = true;
    bool bShowMouseIndicator = false;
    // Frame-rate independent display damping for the channel bars (THR/PIT/
    // YAW/ROLL) so the F2 window reads steadily instead of vibrating.
    float ChannelDisplay[4] = { 0.f, 0.f, 0.f, 0.f };
    void DrawChannels(const ABorn2FlapFlightPawn& Bird);
    void DrawMouseIndicator(const ABorn2FlapFlightPawn& Bird);
    void DrawRcPanel(const FBorn2FlapRcController &Rc);
    TWeakObjectPtr<ABorn2FlapRacingManager> CachedRacing;
};