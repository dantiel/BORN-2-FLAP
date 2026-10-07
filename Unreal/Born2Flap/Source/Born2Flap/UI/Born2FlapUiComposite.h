#pragma once
// Born2FlapUiComposite.h — a semantic component: a styled UBorder shell whose
// inner parts (title/value/bar/… and the child-content panel) are reachable by
// name, so the Ruby Brain drives it with semantic props (tone/value/action/…)
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
    // "Button", "Slider", "Toggle", "Select", "Segment", "NumberBox", "Field",
    // "Section", "Divider").
    UPROPERTY() FString SemanticType;

    // Semantic event key. When a component is interactive (Slider/Toggle/
    // Select/NumberBox/Button), user manipulation emits `{action, value}` to
    // the host's OnComponentAction delegate. Empty = read-only. This is the
    // semantic *identity* of the control — e.g. "tuning.tail_elevator_angle" — and has
    // nothing to do with how it is drawn.
    UPROPERTY() FString Action;

    // Named inner widgets the prop layer reaches into ("title", "label",
    // "value", "bar", "slider", "button", "edit", "options", "header", …).
    UPROPERTY() TMap<FString, UWidget*> Parts;

    // Where the component's *declared children* are appended (null for leaves).
    UPROPERTY() UPanelWidget* Content = nullptr;

    // Small display-state cache (last value/unit/label/options string) so that
    // a partial prop update (e.g. only `unit` changing) can rebuild a readout
    // without re-sending the whole text. Keys: value, unit, label, options,
    // selected, open, on, off, min, max, step, decimals.
    UPROPERTY() TMap<FString, FString> State;

    UWidget* Part(const FString& Key) const { return Parts.FindRef(Key); }
    UPanelWidget* ContentPanel() const { return Content; }
    bool IsInteractive() const { return !Action.IsEmpty(); }
};