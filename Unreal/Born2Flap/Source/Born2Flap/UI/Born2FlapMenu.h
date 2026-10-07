#pragma once
// Born2FlapMenu.h — the post-splash menu, laid out full-screen: a persistent
// left navigation rail plus a single content pane with two pages.
//
// Shown after the startup splash fades. It drives the same semantic
// UBorn2FlapUIRenderer grammar as the cockpit and splash, painting the big
// born2flap-background artwork full-bleed. The left rail always offers OPEN
// WORLDS, FLIGHT DESK/RESUME (in-level) and QUIT. The content pane shows either
// the OPEN WORLDS level selector or — from inside a level — the integrated
// FLIGHT DESK tuning panel (same full-screen view, different page, no separate
// floating settings window). Choosing a level loads that map with
// ?Level=<name>&SkipMenu=1 so the menu does not re-open on the destination map.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Born2FlapWeather.h"
#include "UI/Born2FlapUiOps.h"
#include "Born2FlapMenu.generated.h"

class UBorn2FlapUIRenderer;
class ABorn2FlapFlightPawn;

UCLASS()
class BORN2FLAP_API ABorn2FlapMenu : public AActor
{
    GENERATED_BODY()

public:
    ABorn2FlapMenu();

    virtual void BeginPlay() override;

    // Fade the whole menu (root opacity).
    void SetOpacity(float Opacity);

    // true = opened from inside a level via F10: show FLIGHT DESK + RESUME so the
    // player can tune or return to flight without picking a level.
    // Rebuilds if the flag actually changed (opening in-level must add the
    // FLIGHT DESK/RESUME buttons, which Build() gates on bInLevel).
    void SetInLevel(bool b) { if (bInLevel != b) { bInLevel = b; Build(); } }

    // Detach the UMG window immediately (Destroy() only schedules GC).
    void Close();

    // Re-open on the page (and FLIGHT DESK sub-tab) the player last saw, so F10
    // always returns to the view that was open when the menu closed.
    void RestoreView(int32 Nav, int32 SettingsSub);
    int32 GetNavPage() const { return NavPage; }
    int32 GetSettingsPage() const { return SettingsPage; }

private:
    void Build();

    // Build the FLIGHT DESK page (the tuning panel, previously a separate
    // floating actor) into the shared full-screen content pane. `Out` receives
    // the Panel children: [0] tabs Select, [1] ScrollBox→VBox rows, [2] footer.
    void BuildFlightDesk(std::vector<born2flap::ui::FNode>& Out);

    // Re-drive targets whose displayed state can change without a drag on the
    // exact widget (model selection, "reset mouse response", servo CUSTOM).
    void RefreshModelButtons();
    void RefreshMouseGains();
    void MarkServoCustom();

    // Semantic action from a button ("menu.play" / "menu.settings" / "menu.quit"
    // / "settings.page" / "bird.model.0" / "tuning.ServoSpeed" / ...).
    UFUNCTION()
    void OnAction(const FString& Action, float Value, const FString& Text);

    UPROPERTY() TObjectPtr<UBorn2FlapUIRenderer> Renderer;

    // Top-level page: 0 = OPEN WORLDS (level selector), 1 = FLIGHT DESK
    // (integrated tuning panel — reachable only from inside a level).
    int32 NavPage = 0;
    // FLIGHT DESK sub-tab (0 CRAFT / 1 BIRD / 2 CONTROLS / 3 ASSIST / 4 TUNING).
    int32 SettingsPage = 0;
    // Which open world the browser is currently showing (0..WorldCount-1).
    int32 SelectedWorld = 0;
    // Which world-browser sub-page is shown (0 LOCATION / 1 WEATHER / 2 ROUTE).
    int32 SelectedPage = 0;
    TMap<FString,FBorn2FlapConditions> LevelConditions;
    // Per-world chosen travel destination (index into Born2FlapPoi::Catalog).
    TMap<FString,int32> SelectedPoi;

    // The live bird (only valid when opened from inside a level via F10).
    TWeakObjectPtr<ABorn2FlapFlightPawn> Bird;
    // Paths (child indices from the root) for controls whose displayed state can
    // change without a direct drag on that exact widget.
    TArray<TArray<int32>> ModelButtons;
    TArray<TArray<int32>> MouseGainSliders;
    TArray<int32> ServoSelectPath;
    TArray<int32> ServoStatusPath;
    int32 ServoPresetIndex = -1;

    // true = opened from inside a level (F10): offer FLIGHT DESK + RESUME.
    bool bInLevel = false;
};