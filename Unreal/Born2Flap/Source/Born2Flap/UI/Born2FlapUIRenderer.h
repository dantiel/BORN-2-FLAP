#pragma once
// Born2FlapUIRenderer.h — the UMG host: the "native" half of react-native-umg.
//
// This class implements the FRenderer<Host> contract (Born2FlapUiTree.h) with
// real UWidgets. It consumes the Ruby Brain's JSON op stream (parsed by
// Born2FlapUiOps.h) and turns it into a live UMG tree on the game viewport:
//
//   Ruby Brain ── Wire JSON ──▶ ApplyOpsJson ──▶ FRenderer ──▶ UWidget tree
//
// On top of the raw primitives it adds a *semantic component layer* (see
// Born2FlapUiTheme.h / Born2FlapUiComposite.h): `Panel`, `Value`, `Stat`,
// `Gauge`, `Banner`, `Button`, `Slider`, `Toggle`, `Select`, `NumberBox`,
// `Field`, `Section`, `Divider` are composite widgets, so the Brain says
// `slider label:"Höhe" value: h min:0 max:100 unit:" m"` and the host
// resolves the look — the Brain never styles.

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Blueprint/UserWidget.h"
#include "Templates/UniquePtr.h"
#include "Types/SlateEnums.h"
#include "UI/Born2FlapUiOps.h"
#include "UI/Born2FlapUiTree.h"
#include "UI/Born2FlapUiTheme.h"
#include "UI/Born2FlapUiComposite.h"
#include "UI/Born2FlapAudioEngine.h"
#include "Born2FlapUIRenderer.generated.h"

class UWidget;
class UPanelWidget;
class UUserWidget;
class UCanvasPanel;
class UMaterialInstanceDynamic;
class UBorn2FlapComposite;
class UBorn2FlapUIRenderer;
class USlider;
class UButton;
class UEditableTextBox;

// Semantic component action: an interactive component (Slider/Toggle/Select/
// NumberBox/Button) reports `{Action, Value, Text}` when the user manipulates
// it. `Action` is the semantic key ("tuning.tail_elevator_angle"), `Value` is the
// numeric result (slider value, select index, toggle 1/0), `Text` is a
// rendered label for string-typed actions.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FBorn2FlapComponentAction, const FString&, Action, float, Value, const FString&, Text);

// Minimal concrete UUserWidget subclass so the runtime HUD can build a widget
// tree without a Blueprint asset. UUserWidget itself is UCLASS(Abstract), so
// CreateWidget<UUserWidget> returns nullptr and NewObject<UCanvasPanel> on the
// null outer would assert.
UCLASS()
class BORN2FLAP_API UBorn2FlapRootWidget : public UUserWidget
{
    GENERATED_BODY()
};

// UMG dynamic delegates (USlider::OnValueChanged, UButton::OnClicked,
// UEditableTextBox::OnTextCommitted) only accept UFUNCTION handlers, so a
// tiny relay object forwards each event to the owning renderer along with the
// composite (and, for Select options, the option index) that fired it. Kept
// alive by UBorn2FlapUIRenderer::Relays.
UCLASS()
class BORN2FLAP_API UBorn2FlapActionRelay : public UObject
{
    GENERATED_BODY()

public:
    TWeakObjectPtr<UBorn2FlapUIRenderer> Renderer;
    UBorn2FlapComposite* Composite = nullptr;
    int32 OptionIndex = INDEX_NONE;

    UFUNCTION()
    void OnSlider(float Value);

    UFUNCTION()
    void OnClick();

    UFUNCTION()
    void OnNumber(const FText& Text, ETextCommit::Type CommitMethod);
};

// Per-panel layout intent. The reconciler mounts children *after* the parent's
// UpdateProps, so we cache spacing/alignment here and re-apply on every insert.
struct FLayoutState
{
    float Spacing = 0.f;
    bool bSpacing = false;
    EHorizontalAlignment H = HAlign_Fill;
    EVerticalAlignment V = VAlign_Top;
    bool bH = false;
    bool bV = false;
};

UCLASS()
class BORN2FLAP_API UBorn2FlapUIRenderer : public UObject
{
    GENERATED_BODY()

public:
    using Widget = UWidget*;
    using Renderer = born2flap::ui::FRenderer<UBorn2FlapUIRenderer>;

    UBorn2FlapUIRenderer();

    // Z-order of the viewport root (AddToViewport layering). The splash uses
    // 100 (above the cockpit), the settings panel 10, the cockpit/radio 0.
    UPROPERTY() int32 ViewportZOrder = 0;

    // Parse a Ruby `Wire.dump_ops` JSON string and apply it. The single entry
    // point the Brain bridge (pipe/file/socket/mruby) needs to call.
    void ApplyOpsJson(const FString& Json);

    // Apply an already-parsed op stream (shared with the headless
    // Tools/umg_host_test.cpp via the identical FRenderer core).
    void ApplyOps(const TArray<born2flap::ui::FOp>& Ops);

    // --- FRenderer host contract -------------------------------------------
    UWidget* CreateInstance(const std::string& Type);
    void RemoveInstance(UWidget* W);
    void SetRoot(UWidget* W);
    void AppendChild(UWidget* Parent, int32 Index, UWidget* Child);
    void RemoveChild(UWidget* Parent, int32 Index);
    void UpdateProps(UWidget* W, const born2flap::ui::FProps& Props);
    void SetMaterialParams(UWidget* W, const born2flap::ui::FProps& Params);

    // Re-attempt viewport attachment if it was deferred (see EnsureViewport).
    // The splash calls this each tick until its root is actually on screen.
    void EnsureAttached();

    // Detach the root widget from the game viewport immediately. Destroy() on
    // the owning actor only schedules it for GC — the UMG window would otherwise
    // linger on screen until the next garbage collection. Call this before
    // destroying a panel so its window disappears in the same frame.
    void Close();

    // --- semantic component actions (the interactivity "prowess") ----------
    // Broadcast when an interactive component is manipulated. The native
    // editor / gameplay code subscribes and routes the value into the bird's
    // firmware. Nothing here touches pixels — it is pure semantics.
    UPROPERTY() FBorn2FlapComponentAction OnComponentAction;

    // Relay entry points (called by UBorn2FlapActionRelay).
    void HandleSliderChanged(UBorn2FlapComposite* C, float Value);
    void HandleClicked(UBorn2FlapComposite* C, int32 OptionIndex);
    void HandleNumberCommitted(UBorn2FlapComposite* C, const FText& Text, ETextCommit::Type Commit);
    void EmitAction(const FString& Action, double Value, const FString& Text);

    virtual void BeginDestroy() override;

private:
    void BindSlider(USlider* S, UBorn2FlapComposite* C);
    void BindButton(UButton* B, UBorn2FlapComposite* C, int32 OptionIndex = INDEX_NONE);
    void BindNumber(UEditableTextBox* E, UBorn2FlapComposite* C);
    void RebuildSelectOptions(UBorn2FlapComposite* C, const born2flap::ui::FValue& Options);
    void ApplySelectSelection(UBorn2FlapComposite* C, int32 Index);
    void ApplyToggleState(UBorn2FlapComposite* C);
    void ApplySectionFold(UBorn2FlapComposite* C, bool bOpen);

    // Route an already-parsed op stream: tree ops → FRenderer, audio ops →
    // FAudioEngine. Kept in one place so ApplyOpsJson and ApplyOps share it.
    void ApplyParsedOps(const std::vector<born2flap::ui::FOp>& Ops);
    void EnsureViewport();
    void AttachRoot();
    UMaterialInstanceDynamic* GetOrCreateDynamicMaterial(UWidget* W);

    // ---- semantic component layer -----------------------------------------
    UBorn2FlapComposite* BuildComponent(const FString& Type);
    void ApplyCompositeProps(UBorn2FlapComposite* C, const born2flap::ui::FProps& Props);
    void ApplyPrimitiveProps(UWidget* W, const born2flap::ui::FProps& Props);

    // ---- layout -----------------------------------------------------------
    void StoreContainerLayout(UPanelWidget* Panel, const born2flap::ui::FProps& Props);
    void ApplyStoredLayout(UPanelWidget* Panel);
    void ApplyStoredLayoutToChild(UPanelWidget* Panel, int32 Index);

    TUniquePtr<Renderer> RendererImpl;
    TUniquePtr<born2flap::audio::FAudioEngine> AudioEngine;
    UPROPERTY() UUserWidget* RootHost = nullptr;
    UPROPERTY() UCanvasPanel* ViewportCanvas = nullptr;
    UWidget* RootWidget = nullptr;
    TMap<UWidget*, UMaterialInstanceDynamic*> MaterialCache;
    TMap<UPanelWidget*, FLayoutState> LayoutState;

    // Keeps UBorn2FlapActionRelay instances alive (they are plain UObjects
    // created with this renderer as Outer, otherwise GC would collect them).
    UPROPERTY() TArray<TObjectPtr<UBorn2FlapActionRelay>> Relays;
};