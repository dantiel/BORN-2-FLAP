#pragma once
// Born2FlapUIRenderer.h — the UMG host: the "native" half of react-native-umg.
//
// This class implements the FRenderer<Host> contract (Born2FlapUiTree.h) with
// real UWidgets. It consumes the Ruby Brain's JSON op stream (parsed by
// Born2FlapUiOps.h) and turns it into a live UMG tree on the game viewport:
//
//   Ruby Brain ── Wire JSON ──▶ ApplyOpsJson ──▶ FRenderer ──▶ UWidget tree
//
// The five discrete HostConfig ops map to UMG mutations, and set_material_params
// maps to UMaterialInstanceDynamic::SetScalarParameterValue (the physics-driven
// crown-jewel effects computed Ruby-side).

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/UniquePtr.h"
#include "UI/Born2FlapUiOps.h"
#include "UI/Born2FlapUiTree.h"
#include "Born2FlapUIRenderer.generated.h"

class UWidget;
class UPanelWidget;
class UUserWidget;
class UCanvasPanel;
class UMaterialInstanceDynamic;

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
    void EnsureViewport();
    UMaterialInstanceDynamic* GetOrCreateDynamicMaterial(UWidget* W);

    TUniquePtr<Renderer> RendererImpl;
    UPROPERTY() UUserWidget* RootHost = nullptr;
    UPROPERTY() UCanvasPanel* ViewportCanvas = nullptr;
    TMap<UWidget*, UMaterialInstanceDynamic*> MaterialCache;
};
