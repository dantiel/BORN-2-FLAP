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
// `Gauge`, `Banner`, `Button`, `Slider`, `Divider` are pre-styled composite
// widgets, so the Brain says `gauge label:"Höhe" value: h tone: :accent`
// and the host resolves the look — the Brain never styles.

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

// Minimal concrete UUserWidget subclass so the runtime HUD can build a widget
// tree without a Blueprint asset. UUserWidget itself is UCLASS(Abstract), so
// CreateWidget<UUserWidget> returns nullptr and NewObject<UCanvasPanel> on the
// null outer would assert.
UCLASS()
class BORN2FLAP_API UBorn2FlapRootWidget : public UUserWidget
{
    GENERATED_BODY()
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

    virtual void BeginDestroy() override;

private:
    // Route an already-parsed op stream: tree ops → FRenderer, audio ops →
    // FAudioEngine. Kept in one place so ApplyOpsJson and ApplyOps share it.
    void ApplyParsedOps(const std::vector<born2flap::ui::FOp>& Ops);
    void EnsureViewport();
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
    TMap<UWidget*, UMaterialInstanceDynamic*> MaterialCache;
    TMap<UPanelWidget*, FLayoutState> LayoutState;
};