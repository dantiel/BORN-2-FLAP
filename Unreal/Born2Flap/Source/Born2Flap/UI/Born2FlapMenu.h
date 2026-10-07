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
#include "InputCoreTypes.h"
#include "World/Born2FlapWeather.h"
#include "UI/Born2FlapUiOps.h"
#include "UI/Born2FlapMenuSettings.h"
#include "Born2FlapMenu.generated.h"

class UBorn2FlapUIRenderer;
class ABorn2FlapFlightPawn;
class FBorn2FlapRcController;
class IInputProcessor;

UCLASS()
class BORN2FLAP_API ABorn2FlapMenu : public AActor
{
    GENERATED_BODY()

public:
    ABorn2FlapMenu();
    virtual ~ABorn2FlapMenu() override;

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

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
    void RestoreView(int32 Nav, int32 SettingsSub, int32 PrefsSub = 0);
    int32 GetNavPage() const { return NavPage; }
    int32 GetSettingsPage() const { return SettingsPage; }
    int32 GetPrefsPage() const { return PrefsPage; }

    // KEY BINDINGS capture: while ListeningKeybind is non-empty, the menu's
    // Slate pre-processor routes the next key-down here (Escape cancels).
    bool IsListeningForKeybind() const { return !ListeningKeybind.IsEmpty(); }
    void CancelKeybindListen();
    void ApplyCapturedKeybind(FKey Key);

private:
    void Build();

    // Build the FLIGHT DESK page (the tuning panel, previously a separate
    // floating actor) into the shared full-screen content pane. `Out` receives
    // the Panel children: [0] tabs Select, [1] ScrollBox→VBox rows, [2] footer.
    void BuildFlightDesk(std::vector<born2flap::ui::FNode>& Out);

    // Build the SETTINGS page: sub-tabs CONTROL SETTINGS (embedded RC panel) and
    // GENERAL SETTINGS (global options placeholder). Mirrors BuildFlightDesk's
    // Panel-children contract.
    void BuildPreferences(std::vector<born2flap::ui::FNode>& Out);

    // Re-drive targets whose displayed state can change without a drag on the
    // exact widget (model selection, "reset mouse response", servo CUSTOM).
    void RefreshMouseGains();
    void MarkServoCustom();

    // Settings-store helpers (see Born2FlapMenuSettings.h). The menu edits the
    // store directly and mirrors edits into the live bird when one exists.
    void SyncSettingsFromBird();
    void ApplySettingsToBird();
    void PersistSettings();
    float TuningValue(ETuningField Field) const;
    void SetTuningValue(ETuningField Field, float Value);
    int32 CurBirdModel() const;
    FBorn2FlapRcController* ActiveRc();
    // Craft length + CG travel (mirrors ABorn2FlapFlightPawn's model-dependent
    // values) so the FLIGHT DESK CG slider has the right range on the home screen.
    float CraftLengthCm() const { return CurBirdModel() == 2 ? 106.f : CurBirdModel() == 1 ? 144.f : 182.f; }
    float CgRangeCm() const { return FMath::Max(1.f, FMath::RoundToFloat(CraftLengthCm() * 0.10f)); }
    float CgRangeMm() const { return CgRangeCm() * 10.f; }

    // Semantic action from a button ("menu.play" / "menu.settings" / "menu.quit"
    // / "settings.page" / "bird.model.0" / "tuning.ServoSpeed" / ...).
    UFUNCTION()
    void OnAction(const FString& Action, float Value, const FString& Text);

    UPROPERTY() TObjectPtr<UBorn2FlapUIRenderer> Renderer;

    // Top-level page: 0 = OPEN WORLDS (level selector), 1 = FLIGHT DESK
    // (integrated tuning panel — reachable only from inside a level), 2 =
    // SETTINGS (CONTROL SETTINGS / GENERAL SETTINGS sub-tabs).
    int32 NavPage = 0;
    // FLIGHT DESK sub-tab (0 CRAFT / 1 BIRD / 2 SERVO / 3 FLIGHT).
    int32 SettingsPage = 0;
    // SETTINGS sub-tab (0 CONTROL SETTINGS / 1 GENERAL SETTINGS).
    int32 PrefsPage = 0;
    // Which open world the browser is currently showing (0..WorldCount-1).
    int32 SelectedWorld = 0;
    // Which world-browser sub-page is shown (0 LOCATION / 1 WEATHER / 2 ROUTE).
    int32 SelectedPage = 0;
    TMap<FString,FBorn2FlapConditions> LevelConditions;
    // Per-world chosen travel destination (index into Born2FlapPoi::Catalog).
    TMap<FString,int32> SelectedPoi;

    // The live bird (only valid when opened from inside a level via F10).
    TWeakObjectPtr<ABorn2FlapFlightPawn> Bird;
    // Pawn-independent settings mirror (persists to FlightPreferences.ini).
    born2flap::FMenuSettings Settings;
    // RC transmitter fallback for the home screen (the bird owns one in-level).
    TUniquePtr<FBorn2FlapRcController> RcFallback;
    // Paths (child indices from the root) for controls whose displayed state can
    // change without a direct drag on that exact widget.
    TArray<TArray<int32>> MouseGainSliders;
    TArray<int32> ServoSelectPath;
    TArray<int32> ServoStatusPath;
    int32 ServoPresetIndex = -1;

    // true = opened from inside a level (F10): offer FLIGHT DESK + RESUME.
    bool bInLevel = false;

    // KEY BINDINGS capture state: the action id awaiting a new key (empty = idle).
    FString ListeningKeybind;
    // Set when the key-capture pre-processor applied/cancelled a rebind during
    // Slate input processing; Tick defers the Build() to stay out of that path.
    bool bKeybindRebuildPending = false;
    // Slate pre-processor that captures the next key while ListeningKeybind is
    // armed (registered in BeginPlay, removed in Close/EndPlay).
    TSharedPtr<IInputProcessor> KeyCaptureProcessor;
};