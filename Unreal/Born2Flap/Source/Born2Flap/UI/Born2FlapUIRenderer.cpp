// Born2FlapUIRenderer.cpp — the UMG host implementation.
//
// NOTE: this file compiles against the Unreal Engine (UE 5.7 UMG API). It is
// the sibling of Tools/umg_host_test.cpp, which proves the SAME FRenderer core
// headlessly with plain clang++ (no engine). Build this on the machine that has
// the engine installed.

#include "UI/Born2FlapUIRenderer.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Components/Spacer.h"
#include "Components/PanelWidget.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UBorn2FlapUIRenderer::UBorn2FlapUIRenderer()
    : RendererImpl(MakeUnique<Renderer>(*this))
{
}

void UBorn2FlapUIRenderer::ApplyOpsJson(const FString& Json)
{
    const std::string Bytes = TCHAR_TO_UTF8(*Json);
    std::vector<born2flap::ui::FOp> Ops;
    if (!born2flap::ui::ParseOps(Bytes, Ops))
    {
        UE_LOG(LogTemp, Warning, TEXT("Born2FlapUIRenderer: failed to parse op JSON"));
        return;
    }
    if (RendererImpl)
        RendererImpl->Apply(Ops);
}

void UBorn2FlapUIRenderer::ApplyOps(const TArray<born2flap::ui::FOp>& Ops)
{
    std::vector<born2flap::ui::FOp> Vec;
    Vec.reserve(Ops.Num());
    for (const auto& Op : Ops)
        Vec.push_back(Op);
    if (RendererImpl)
        RendererImpl->Apply(Vec);
}

// ---- FRenderer host contract ----------------------------------------------

UWidget* UBorn2FlapUIRenderer::CreateInstance(const std::string& Type)
{
    if (Type == "Overlay")       return NewObject<UOverlay>(this);
    if (Type == "VerticalBox")   return NewObject<UVerticalBox>(this);
    if (Type == "HorizontalBox") return NewObject<UHorizontalBox>(this);
    if (Type == "Border")        return NewObject<UBorder>(this);
    if (Type == "TextBlock")     return NewObject<UTextBlock>(this);
    if (Type == "Button")        return NewObject<UButton>(this);
    if (Type == "Slider")        return NewObject<USlider>(this);
    if (Type == "ProgressBar")   return NewObject<UProgressBar>(this);
    if (Type == "Image")         return NewObject<UImage>(this);
    if (Type == "Spacer")        return NewObject<USpacer>(this);
    UE_LOG(LogTemp, Warning, TEXT("Born2FlapUIRenderer: unknown widget type '%s'"), UTF8_TO_TCHAR(Type.c_str()));
    return NewObject<UBorder>(this);
}

void UBorn2FlapUIRenderer::RemoveInstance(UWidget* W)
{
    if (!W)
        return;
    W->RemoveFromParent();
    W->MarkAsGarbage();
}

void UBorn2FlapUIRenderer::SetRoot(UWidget* W)
{
    EnsureViewport();
    if (!ViewportCanvas)
        return;
    UCanvasPanelSlot* Slot = ViewportCanvas->AddChildToCanvas(W);
    Slot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    Slot->SetOffsets(FMargin(0.f));
}

void UBorn2FlapUIRenderer::AppendChild(UWidget* Parent, int32 Index, UWidget* Child)
{
    if (UPanelWidget* Panel = Cast<UPanelWidget>(Parent))
        Panel->InsertChildAt(Index, Child);
}

void UBorn2FlapUIRenderer::RemoveChild(UWidget* Parent, int32 Index)
{
    if (UPanelWidget* Panel = Cast<UPanelWidget>(Parent))
    {
        if (Panel->GetChildAt(Index))
        {
            UWidget* Child = Panel->GetChildAt(Index);
            Panel->RemoveChildAt(Index);
            Child->MarkAsGarbage();
        }
    }
}

void UBorn2FlapUIRenderer::UpdateProps(UWidget* W, const born2flap::ui::FProps& Props)
{
    for (const auto& KV : Props)
    {
        const std::string& Key = KV.first;
        const born2flap::ui::FValue& V = KV.second;

        if (UTextBlock* Text = Cast<UTextBlock>(W))
        {
            if (Key == "text" || Key == "value")
                Text->SetText(FText::FromString(UTF8_TO_TCHAR(V.AsString().c_str())));
        }
        else if (USlider* Slider = Cast<USlider>(W))
        {
            if (Key == "value") Slider->SetValue((float)V.AsNumber());
            else if (Key == "min") Slider->SetMinValue((float)V.AsNumber());
            else if (Key == "max") Slider->SetMaxValue((float)V.AsNumber());
            else if (Key == "step") Slider->SetStepSize((float)V.AsNumber());
        }
        else if (UProgressBar* Bar = Cast<UProgressBar>(W))
        {
            if (Key == "value") Bar->SetPercent((float)V.AsNumber());
        }
        // id / class / bind / onPress / onChange / effect are consumed by the
        // Brain (bind/effect) or wired to delegates (onPress) in a later pass.
    }
}

void UBorn2FlapUIRenderer::SetMaterialParams(UWidget* W, const born2flap::ui::FProps& Params)
{
    UMaterialInstanceDynamic* MID = GetOrCreateDynamicMaterial(W);
    if (!MID)
        return;
    for (const auto& KV : Params)
        MID->SetScalarParameterValue(FName(UTF8_TO_TCHAR(KV.first.c_str())), (float)KV.second.AsNumber());
}

void UBorn2FlapUIRenderer::BeginDestroy()
{
    MaterialCache.Empty();
    RendererImpl.Reset();
    if (RootHost)
        RootHost->RemoveFromParent();
    RootHost = nullptr;
    ViewportCanvas = nullptr;
    Super::BeginDestroy();
}

// ---- helpers ---------------------------------------------------------------

void UBorn2FlapUIRenderer::EnsureViewport()
{
    if (RootHost)
        return;
    UWorld* World = GetWorld();
    if (!World)
        return;
    RootHost = CreateWidget<UUserWidget>(World);
    ViewportCanvas = NewObject<UCanvasPanel>(RootHost);
    RootHost->WidgetTree->RootWidget = ViewportCanvas;
    RootHost->AddToViewport();
}

UMaterialInstanceDynamic* UBorn2FlapUIRenderer::GetOrCreateDynamicMaterial(UWidget* W)
{
    if (UMaterialInstanceDynamic* Cached = MaterialCache.FindRef(W))
        return Cached;

    UMaterialInstanceDynamic* MID = nullptr;
    if (UImage* Image = Cast<UImage>(W))
        MID = Image->GetDynamicMaterial();
    else if (UBorder* Border = Cast<UBorder>(W))
        MID = Border->GetDynamicMaterial();

    if (MID)
        MaterialCache.Add(W, MID);
    return MID;
}