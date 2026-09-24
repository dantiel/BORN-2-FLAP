#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Born2FlapHUD.generated.h"
class FBorn2FlapRcController;
UCLASS()
class BORN2FLAP_API ABorn2FlapHUD : public AHUD
{
    GENERATED_BODY()
  public:
    virtual void DrawHUD() override;

  private:
    void DrawRcPanel(const FBorn2FlapRcController &Rc);
};
