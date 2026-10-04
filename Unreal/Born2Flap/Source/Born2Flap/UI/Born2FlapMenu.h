#pragma once
// Born2FlapMenu.h — the post-splash main menu / level selector.
//
// Shown after the startup splash fades. It drives the same semantic
// UBorn2FlapUIRenderer grammar as the cockpit and splash, painting the big
// born2flap-background artwork full-bleed and offering one button per level.
// Selecting a level loads that map with ?Level=<name>&SkipMenu=1 so the menu
// does not re-open on the destination map.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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

private:
    void Build();

    // Semantic action from a level button ("menu.ravenstonefield" / ...).
    UFUNCTION()
    void OnAction(const FString& Action, float Value, const FString& Text);

    UPROPERTY() TObjectPtr<UBorn2FlapUIRenderer> Renderer;
};
