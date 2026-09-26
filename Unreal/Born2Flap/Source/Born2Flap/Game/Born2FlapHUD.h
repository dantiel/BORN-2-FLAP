#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Born2FlapHUD.generated.h"
class FBorn2FlapRcController;
class ABorn2FlapRacingManager;
UCLASS()
class BORN2FLAP_API ABorn2FlapHUD : public AHUD
{
    GENERATED_BODY()
  public:
    virtual void DrawHUD() override;

  private:
    void DrawRcPanel(const FBorn2FlapRcController &Rc);
    TWeakObjectPtr<ABorn2FlapRacingManager> CachedRacing;
};