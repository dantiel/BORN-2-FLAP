#pragma once
// Born2FlapUiComposite.h — a semantic component: a styled UBorder shell whose
// inner parts (title/value/bar/… and the child-content panel) are reachable by
// name, so the Ruby Brain drives it with semantic props (tone/size/value/…)
// instead of pixel styling.

#include "CoreMinimal.h"
#include "Components/Border.h"
#include "Born2FlapUiComposite.generated.h"

class UPanelWidget;
class UWidget;

UCLASS()
class BORN2FLAP_API UBorn2FlapComposite : public UBorder
{
    GENERATED_BODY()

public:
    // Semantic component kind ("Panel", "Value", "Stat", "Gauge", "Banner",
    // "Button", "Slider", "Divider").
    UPROPERTY() FString SemanticType;

    // Named inner widgets the prop layer reaches into ("title", "label",
    // "value", "bar", "slider", "button", "text", …).
    UPROPERTY() TMap<FString, UWidget*> Parts;

    // Where the component's *declared children* are appended (null for leaves).
    UPROPERTY() UPanelWidget* Content = nullptr;

    // Small display-state cache (last value/unit/label string) so that a
    // partial prop update (e.g. only `unit` changing) can rebuild a readout
    // without re-sending the whole text.
    UPROPERTY() TMap<FString, FString> State;

    UWidget* Part(const FString& Key) const { return Parts.FindRef(Key); }
    UPanelWidget* ContentPanel() const { return Content; }
};
