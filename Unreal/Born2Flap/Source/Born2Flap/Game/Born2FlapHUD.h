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
    virtual void BeginPlay() override;
    virtual void DrawHUD() override;

  private:
    // F9 cycles the control-input overlay OFF → STICKS → MOUSE → OFF so the
    // stick gimbals and the mouse-movement halo never share the screen. The
    // default is OFF — on touch the twin-stick joysticks already paint their
    // own on-screen sticks, so the overlay stays deactivated until asked for.
    enum class EInputOverlayMode : uint8 { Off, Sticks, Mouse };
    bool bShowChannels = true;
    EInputOverlayMode InputOverlay = EInputOverlayMode::Off;
    bool bShowFps = true;
    float SmoothedFps = 60.f;
    // Frame-rate independent display damping for the channel bars (THR/PIT/
    // YAW/ROLL) so the F2 window reads steadily instead of vibrating.
    float ChannelDisplay[4] = { 0.f, 0.f, 0.f, 0.f };
    void DrawChannels(const ABorn2FlapFlightPawn& Bird);
    void DrawMouseIndicator(const ABorn2FlapFlightPawn& Bird);
    void DrawInputSticks(const ABorn2FlapFlightPawn& Bird);
    void DrawRcPanel(const FBorn2FlapRcController &Rc);
    TWeakObjectPtr<ABorn2FlapRacingManager> CachedRacing;
};