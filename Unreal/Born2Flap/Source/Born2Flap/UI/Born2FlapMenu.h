#pragma once
// Born2FlapMenu.h — the post-splash main menu, with a separate level-selection
// screen.
//
// Shown after the startup splash fades. It drives the same semantic
// UBorn2FlapUIRenderer grammar as the cockpit and splash, painting the big
// born2flap-background artwork full-bleed. The main menu offers FLY (open the
// level selector) and QUIT; selecting a level loads that map with
// ?Level=<name>&SkipMenu=1 so the menu does not re-open on the destination map.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Born2FlapWeather.h"
#include "Born2FlapMenu.generated.h"

class UBorn2FlapUIRenderer;

UCLASS()
class BORN2FLAP_API ABorn2FlapMenu : public AActor
{
    GENERATED_BODY()

public:
    ABorn2FlapMenu();

    virtual void BeginPlay() override;

    // Fade the whole menu (root opacity).
    void SetOpacity(float Opacity);

    // true = opened from inside a level via Escape: show a RESUME button so the
    // player can return to flight without picking a level.
    // Rebuilds the card if the flag actually changed (e.g. opening the menu from
    // inside a level must add the RESUME button, which Build() gates on bInLevel).
    void SetInLevel(bool b) { if (bInLevel != b) { bInLevel = b; Build(); } }

    // Detach the UMG window immediately (Destroy() only schedules GC).
    void Close();

private:
    void Build();

    // Semantic action from a button ("menu.play" / "menu.back" / "menu.quit" /
    // "menu.ravenstonefield" / ...).
    UFUNCTION()
    void OnAction(const FString& Action, float Value, const FString& Text);

    UPROPERTY() TObjectPtr<UBorn2FlapUIRenderer> Renderer;

    // false = main menu (title + OPEN WORLDS/QUIT), true = the world browser
    // (the level selector). Defaults to the world browser so the level selector
    // is the first screen the player sees; BACK returns to the title screen.
    bool bLevelSelect = true;
    // Which open world the browser is currently showing (0..WorldCount-1).
    int32 SelectedWorld = 0;
    int32 SelectedPage = 0;
    TMap<FString,FBorn2FlapConditions> LevelConditions;
    // Per-world chosen travel destination (index into Born2FlapPoi::Catalog).
    TMap<FString,int32> SelectedPoi;

    // true = opened from inside a level (Escape): offer RESUME on the main menu.
    bool bInLevel = false;
};
