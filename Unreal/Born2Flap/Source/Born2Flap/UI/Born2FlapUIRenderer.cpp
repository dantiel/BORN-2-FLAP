// Born2FlapUIRenderer.cpp — the UMG host implementation with a semantic
// component layer. Raw primitives (Overlay/VBox/HBox/TextBlock/…) plus a set
// of pre-styled composite widgets (Panel/Value/Stat/Gauge/Banner/Button/
// Slider/Divider) so the Ruby Brain speaks semantically and never styles.

#include "UI/Born2FlapUIRenderer.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
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
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateColor.h"

using namespace born2flap::ui;

// ---- file-local prop helpers ----------------------------------------------
namespace {

FString Str(const FValue& V) { return FString(UTF8_TO_TCHAR(V.AsString().c_str())); }
float Num(const FValue& V, float D) { return (float)V.AsNumber(D); }
FString FormatNum(const FValue& V) { return FString::SanitizeFloat((float)V.AsNumber(0.0)); }

// size prop: a numeric px value wins, otherwise a size token (xs..xxl).
float SizeOrNum(const FValue& V, float D)
{
    if (V.kind == FValue::Kind::Number) return (float)V.AsNumber(D);
    return theme::SizePx(Str(V), D);
}

FMargin ParseMargin(const FValue& V)
{
    if (V.kind == FValue::Kind::Array && V.arr.size() == 4)
        return FMargin(Num(V.arr[0], 0), Num(V.arr[1], 0), Num(V.arr[2], 0), Num(V.arr[3], 0));
    if (V.kind == FValue::Kind::Array && V.arr.size() == 2)
        return FMargin(Num(V.arr[0], 0), Num(V.arr[1], 0));
    return FMargin(Num(V, 10.0));
}

EHorizontalAlignment ParseHAlign(const FValue& V)
{
    const FString S = Str(V);
    if (S == TEXT("left"))   return HAlign_Left;
    if (S == TEXT("center")) return HAlign_Center;
    if (S == TEXT("right"))  return HAlign_Right;
    return HAlign_Fill;
}

EVerticalAlignment ParseVAlign(const FValue& V)
{
    const FString S = Str(V);
    if (S == TEXT("top"))    return VAlign_Top;
    if (S == TEXT("center")) return VAlign_Center;
    if (S == TEXT("bottom")) return VAlign_Bottom;
    return VAlign_Fill;
}

ETextJustify::Type ParseJustify(const FValue& V)
{
    const FString S = Str(V);
    if (S == TEXT("center")) return ETextJustify::Center;
    if (S == TEXT("right"))  return ETextJustify::Right;
    return ETextJustify::Left;
}

// Set text + collapse-when-empty (a readout with no value disappears).
void SetText(UTextBlock* T, const FValue& V)
{
    if (!T) return;
    const FString S = Str(V);
    T->SetText(FText::FromString(S));
    T->SetVisibility(S.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}

// Typography (size/weight/align/wrap) — tone color is applied by the caller.
void StyleText(UTextBlock* T, const FProps& Props)
{
    if (!T) return;
    FSlateFontInfo F = T->GetFont();
    float Size = F.Size;
    bool bBold = (F.TypefaceFontName == FName("Bold"));
    if (Props.count("size"))   Size  = SizeOrNum(Props.at("size"), Size);
    if (Props.count("weight")) bBold = (Str(Props.at("weight")) == TEXT("bold"));
    T->SetFont(theme::FontPx(Size, bBold));
    if (Props.count("align")) T->SetJustification(ParseJustify(Props.at("align")));
    if (Props.count("wrap"))  T->SetAutoWrapText(Props.at("wrap").AsBool(false));
}

FString ToneOf(const FProps& Props)
{
    auto it = Props.find("tone");
    return it == Props.end() ? FString() : Str(it->second);
}

UBorn2FlapComposite* NewComposite(UObject* Outer, const FString& Type)
{
    UBorn2FlapComposite* C = NewObject<UBorn2FlapComposite>(Outer);
    C->SemanticType = Type;
    C->SetBrush(FSlateRoundedBoxBrush(theme::BG(), theme::Radius()));
    C->SetPadding(FMargin(10.f));
    return C;
}

}  // namespace

UBorn2FlapUIRenderer::UBorn2FlapUIRenderer()
    : RendererImpl(MakeUnique<Renderer>(*this))
    , AudioEngine(MakeUnique<born2flap::audio::FAudioEngine>())
{
}

void UBorn2FlapUIRenderer::ApplyOpsJson(const FString& Json)
{
    const std::string Bytes = TCHAR_TO_UTF8(*Json);
    std::vector<FOp> Ops;
    if (!ParseOps(Bytes, Ops))
    {
        UE_LOG(LogTemp, Warning, TEXT("Born2FlapUIRenderer: failed to parse op JSON"));
        return;
    }
    ApplyParsedOps(Ops);
}

void UBorn2FlapUIRenderer::ApplyOps(const TArray<FOp>& Ops)
{
    std::vector<FOp> Vec;
    Vec.reserve(Ops.Num());
    for (const auto& Op : Ops)
        Vec.push_back(Op);
    ApplyParsedOps(Vec);
}

void UBorn2FlapUIRenderer::ApplyParsedOps(const std::vector<FOp>& Ops)
{
    std::vector<FOp> TreeOps;
    TreeOps.reserve(Ops.size());
    for (const auto& Op : Ops)
    {
        if (Op.op == EOp::SetAudioParams)
        {
            if (AudioEngine)
                AudioEngine->SetParams(Op.voice, Op.props);
        }
        else
        {
            TreeOps.push_back(Op);
        }
    }
    if (RendererImpl && !TreeOps.empty())
        RendererImpl->Apply(TreeOps);
}

// ---- FRenderer host contract ----------------------------------------------

UWidget* UBorn2FlapUIRenderer::CreateInstance(const std::string& Type)
{
    const FString T(UTF8_TO_TCHAR(Type.c_str()));

    // semantic composite components (pre-styled)
    if (T == TEXT("Panel") || T == TEXT("Value") || T == TEXT("Stat") ||
        T == TEXT("Gauge") || T == TEXT("Banner") || T == TEXT("Button") ||
        T == TEXT("Slider") || T == TEXT("Divider"))
    {
        return BuildComponent(T);
    }

    // raw layout/leaf primitives
    if (Type == "Overlay")       return NewObject<UOverlay>(this);
    if (Type == "VerticalBox")   return NewObject<UVerticalBox>(this);
    if (Type == "HorizontalBox") return NewObject<UHorizontalBox>(this);
    if (Type == "Border")        return NewObject<UBorder>(this);
    if (Type == "TextBlock")     return NewObject<UTextBlock>(this);
    if (Type == "ProgressBar")   return NewObject<UProgressBar>(this);
    if (Type == "Image")         return NewObject<UImage>(this);
    if (Type == "Spacer")        return NewObject<USpacer>(this);

    UE_LOG(LogTemp, Warning, TEXT("Born2FlapUIRenderer: unknown widget type '%s'"), *T);
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
    UPanelWidget* Panel = Cast<UPanelWidget>(Parent);
    if (UBorn2FlapComposite* C = Cast<UBorn2FlapComposite>(Parent))
        Panel = C->ContentPanel();
    if (!Panel)
        return;
    Panel->InsertChildAt(Index, Child);
    ApplyStoredLayoutToChild(Panel, Index);
}

void UBorn2FlapUIRenderer::RemoveChild(UWidget* Parent, int32 Index)
{
    UPanelWidget* Panel = Cast<UPanelWidget>(Parent);
    if (UBorn2FlapComposite* C = Cast<UBorn2FlapComposite>(Parent))
        Panel = C->ContentPanel();
    if (!Panel)
        return;
    if (Panel->GetChildAt(Index))
    {
        UWidget* Child = Panel->GetChildAt(Index);
        Panel->RemoveChildAt(Index);
        Child->MarkAsGarbage();
    }
}

void UBorn2FlapUIRenderer::UpdateProps(UWidget* W, const FProps& Props)
{
    if (UBorn2FlapComposite* C = Cast<UBorn2FlapComposite>(W))
        ApplyCompositeProps(C, Props);
    else
        ApplyPrimitiveProps(W, Props);
}

void UBorn2FlapUIRenderer::SetMaterialParams(UWidget* W, const FProps& Params)
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
    LayoutState.Empty();
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

// ---- semantic component factory -------------------------------------------

UBorn2FlapComposite* UBorn2FlapUIRenderer::BuildComponent(const FString& Type)
{
    if (Type == TEXT("Panel"))
    {
        UBorn2FlapComposite* C = NewComposite(this, TEXT("Panel"));
        UVerticalBox* Body = NewObject<UVerticalBox>(this);
        C->SetContent(Body);
        UTextBlock* Title = NewObject<UTextBlock>(this);
        Title->SetFont(theme::Font(TEXT("s"), 12, true));
        Title->SetColorAndOpacity(FSlateColor(theme::FG_DIM()));
        Title->SetVisibility(ESlateVisibility::Collapsed);
        Body->AddChildToVerticalBox(Title);
        UVerticalBox* Content = NewObject<UVerticalBox>(this);
        Body->AddChildToVerticalBox(Content);
        C->Parts.Add(TEXT("title"), Title);
        C->Content = Content;
        return C;
    }
    if (Type == TEXT("Value"))
    {
        UBorn2FlapComposite* C = NewComposite(this, TEXT("Value"));
        C->SetPadding(FMargin(8.f, 4.f));
        UHorizontalBox* Row = NewObject<UHorizontalBox>(this);
        C->SetContent(Row);
        UTextBlock* Label = NewObject<UTextBlock>(this);
        Label->SetFont(theme::Font(TEXT("s"), 12));
        Label->SetColorAndOpacity(FSlateColor(theme::FG_DIM()));
        Row->AddChildToHorizontalBox(Label);
        UTextBlock* Value = NewObject<UTextBlock>(this);
        Value->SetFont(theme::Font(TEXT("m"), 15));
        Row->AddChildToHorizontalBox(Value);
        C->Parts.Add(TEXT("label"), Label);
        C->Parts.Add(TEXT("value"), Value);
        return C;
    }
    if (Type == TEXT("Stat"))
    {
        UBorn2FlapComposite* C = NewComposite(this, TEXT("Stat"));
        C->SetPadding(FMargin(8.f, 6.f));
        UVerticalBox* Body = NewObject<UVerticalBox>(this);
        C->SetContent(Body);
        UTextBlock* Value = NewObject<UTextBlock>(this);
        Value->SetFont(theme::Font(TEXT("l"), 20));
        Body->AddChildToVerticalBox(Value);
        UTextBlock* Label = NewObject<UTextBlock>(this);
        Label->SetFont(theme::Font(TEXT("s"), 12));
        Label->SetColorAndOpacity(FSlateColor(theme::FG_DIM()));
        Body->AddChildToVerticalBox(Label);
        C->Parts.Add(TEXT("value"), Value);
        C->Parts.Add(TEXT("label"), Label);
        return C;
    }
    if (Type == TEXT("Gauge"))
    {
        UBorn2FlapComposite* C = NewComposite(this, TEXT("Gauge"));
        UVerticalBox* Body = NewObject<UVerticalBox>(this);
        C->SetContent(Body);
        UHorizontalBox* Head = NewObject<UHorizontalBox>(this);
        Body->AddChildToVerticalBox(Head);
        UTextBlock* Label = NewObject<UTextBlock>(this);
        Label->SetFont(theme::Font(TEXT("s"), 12));
        Label->SetColorAndOpacity(FSlateColor(theme::FG_DIM()));
        Head->AddChildToHorizontalBox(Label);
        UTextBlock* ValueText = NewObject<UTextBlock>(this);
        ValueText->SetFont(theme::Font(TEXT("s"), 12, true));
        Head->AddChildToHorizontalBox(ValueText);
        UProgressBar* Bar = NewObject<UProgressBar>(this);
        Bar->SetFillColorAndOpacity(theme::ACCENT());
        Body->AddChildToVerticalBox(Bar);
        C->Parts.Add(TEXT("label"), Label);
        C->Parts.Add(TEXT("value"), ValueText);
        C->Parts.Add(TEXT("bar"), Bar);
        return C;
    }
    if (Type == TEXT("Banner"))
    {
        UBorn2FlapComposite* C = NewComposite(this, TEXT("Banner"));
        UHorizontalBox* Row = NewObject<UHorizontalBox>(this);
        C->SetContent(Row);
        UTextBlock* Text = NewObject<UTextBlock>(this);
        Text->SetFont(theme::Font(TEXT("m"), 15));
        Row->AddChildToHorizontalBox(Text);
        C->Parts.Add(TEXT("text"), Text);
        return C;
    }
    if (Type == TEXT("Button"))
    {
        UBorn2FlapComposite* C = NewComposite(this, TEXT("Button"));
        C->SetPadding(FMargin(0.f));
        UButton* Btn = NewObject<UButton>(this);
        Btn->SetBackgroundColor(theme::ACCENT());
        C->SetContent(Btn);
        UTextBlock* Label = NewObject<UTextBlock>(this);
        Label->SetFont(theme::Font(TEXT("m"), 15, true));
        Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.06f, 0.06f, 0.08f, 1.0f)));
        Btn->SetContent(Label);
        C->Parts.Add(TEXT("button"), Btn);
        C->Parts.Add(TEXT("label"), Label);
        return C;
    }
    if (Type == TEXT("Slider"))
    {
        UBorn2FlapComposite* C = NewComposite(this, TEXT("Slider"));
        UVerticalBox* Body = NewObject<UVerticalBox>(this);
        C->SetContent(Body);
        UHorizontalBox* Head = NewObject<UHorizontalBox>(this);
        Body->AddChildToVerticalBox(Head);
        UTextBlock* Label = NewObject<UTextBlock>(this);
        Label->SetFont(theme::Font(TEXT("s"), 12));
        Label->SetColorAndOpacity(FSlateColor(theme::FG_DIM()));
        Head->AddChildToHorizontalBox(Label);
        UTextBlock* ValueText = NewObject<UTextBlock>(this);
        ValueText->SetFont(theme::Font(TEXT("s"), 12, true));
        Head->AddChildToHorizontalBox(ValueText);
        USlider* Slider = NewObject<USlider>(this);
        Slider->SetSliderBarColor(theme::FG_DIM());
        Slider->SetSliderHandleColor(theme::ACCENT());
        Body->AddChildToVerticalBox(Slider);
        C->Parts.Add(TEXT("label"), Label);
        C->Parts.Add(TEXT("value"), ValueText);
        C->Parts.Add(TEXT("slider"), Slider);
        return C;
    }
    if (Type == TEXT("Divider"))
    {
        UBorn2FlapComposite* C = NewComposite(this, TEXT("Divider"));
        C->SetPadding(FMargin(0.f));
        C->SetBrush(FSlateRoundedBoxBrush(
            FLinearColor(theme::FG_DIM().R, theme::FG_DIM().G, theme::FG_DIM().B, 0.25f),
            theme::Radius(0.5f)));
        USpacer* Sp = NewObject<USpacer>(this);
        Sp->SetSize(FVector2D(1.f, 1.f));
        C->SetContent(Sp);
        C->Parts.Add(TEXT("spacer"), Sp);
        return C;
    }

    return NewComposite(this, TEXT("Panel"));
}

// ---- semantic prop application --------------------------------------------

void UBorn2FlapUIRenderer::ApplyCompositeProps(UBorn2FlapComposite* C, const FProps& Props)
{
    const FString Type = C->SemanticType;
    const FString Tone = ToneOf(Props);
    const FLinearColor ToneColor = theme::Tone(Tone, theme::FG());
    const bool bInteractive = (Type == TEXT("Button") || Type == TEXT("Slider"));

    // common
    if (Props.count("visible"))
    {
        const bool v = Props.at("visible").AsBool(true);
        C->SetVisibility(v ? (bInteractive ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible)
                           : ESlateVisibility::Collapsed);
    }
    if (Props.count("opacity"))
        C->SetRenderOpacity(Num(Props.at("opacity"), 1.0));
    if (Props.count("padding"))
        C->SetPadding(ParseMargin(Props.at("padding")));

    if (Type == TEXT("Panel"))
    {
        UTextBlock* Title = Cast<UTextBlock>(C->Part(TEXT("title")));
        if (Props.count("title") && Title) SetText(Title, Props.at("title"));
        if (!Tone.IsEmpty() && Title) Title->SetColorAndOpacity(FSlateColor(ToneColor));
        if (Props.count("bg"))
        {
            const FString Bg = Str(Props.at("bg"));
            if (Bg == TEXT("clear") || Bg == TEXT("none"))
                C->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.f, 0.f, 0.f, 0.f), theme::Radius()));
            else if (Bg == TEXT("solid"))
                C->SetBrush(FSlateRoundedBoxBrush(theme::BG_SOLID(), theme::Radius()));
            else
                C->SetBrush(FSlateRoundedBoxBrush(theme::BG(), theme::Radius()));
        }
        else if (!Tone.IsEmpty())
        {
            C->SetBrush(FSlateRoundedBoxBrush(theme::ToneAt(Tone, theme::BG(), 0.30f), theme::Radius()));
        }
        if (C->ContentPanel())
            StoreContainerLayout(C->ContentPanel(), Props);
    }
    else if (Type == TEXT("Value") || Type == TEXT("Stat"))
    {
        UTextBlock* Label = Cast<UTextBlock>(C->Part(TEXT("label")));
        UTextBlock* Value = Cast<UTextBlock>(C->Part(TEXT("value")));
        if (Props.count("label") && Label) SetText(Label, Props.at("label"));

        const bool bV = Props.count("value") != 0;
        const bool bU = Props.count("unit") != 0;
        if (bV) C->State.Add(TEXT("value"), FormatNum(Props.at("value")));
        if (bU) C->State.Add(TEXT("unit"), Str(Props.at("unit")));
        if (bV || bU)
        {
            FString D = C->State.FindRef(TEXT("value"));
            D += C->State.FindRef(TEXT("unit"));
            if (Value) Value->SetText(FText::FromString(D));
        }
        if (!Tone.IsEmpty() && Value) Value->SetColorAndOpacity(FSlateColor(ToneColor));
        StyleText(Value, Props);
    }
    else if (Type == TEXT("Gauge"))
    {
        UProgressBar* Bar = Cast<UProgressBar>(C->Part(TEXT("bar")));
        UTextBlock* ValueTxt = Cast<UTextBlock>(C->Part(TEXT("value")));
        UTextBlock* LabelTxt = Cast<UTextBlock>(C->Part(TEXT("label")));
        if (Props.count("label") && LabelTxt) SetText(LabelTxt, Props.at("label"));
        if (!Tone.IsEmpty() && Bar) Bar->SetFillColorAndOpacity(ToneColor);
        if (!Tone.IsEmpty() && ValueTxt) ValueTxt->SetColorAndOpacity(FSlateColor(ToneColor));

        const bool bV = Props.count("value") != 0;
        const bool bU = Props.count("unit") != 0;
        const bool bLo = Props.count("min") != 0;
        const bool bHi = Props.count("max") != 0;
        if (bV) C->State.Add(TEXT("value"), FormatNum(Props.at("value")));
        if (bU) C->State.Add(TEXT("unit"), Str(Props.at("unit")));
        if (bV || bU)
        {
            FString D = C->State.FindRef(TEXT("value"));
            D += C->State.FindRef(TEXT("unit"));
            if (ValueTxt) ValueTxt->SetText(FText::FromString(D));
        }
        if (bV || bLo || bHi)
        {
            const float Min = bLo ? Num(Props.at("min"), 0.0) : 0.0f;
            const float Max = bHi ? Num(Props.at("max"), 1.0) : 1.0f;
            const float Vv  = bV  ? Num(Props.at("value"), 0.0) : 0.0f;
            const float Pct = (Max > Min) ? FMath::Clamp((Vv - Min) / (Max - Min), 0.f, 1.f)
                                          : FMath::Clamp(Vv, 0.f, 1.f);
            if (Bar) Bar->SetPercent(Pct);
        }
        StyleText(ValueTxt, Props);
    }
    else if (Type == TEXT("Banner"))
    {
        UTextBlock* Text = Cast<UTextBlock>(C->Part(TEXT("text")));
        if (Props.count("text") && Text) SetText(Text, Props.at("text"));
        if (!Tone.IsEmpty())
        {
            C->SetBrush(FSlateRoundedBoxBrush(theme::ToneAt(Tone, theme::ACCENT(), 0.18f), theme::Radius()));
            if (Text) Text->SetColorAndOpacity(FSlateColor(ToneColor));
        }
        StyleText(Text, Props);
    }
    else if (Type == TEXT("Button"))
    {
        UButton* Btn = Cast<UButton>(C->Part(TEXT("button")));
        UTextBlock* Label = Cast<UTextBlock>(C->Part(TEXT("label")));
        if (Props.count("label") && Label) SetText(Label, Props.at("label"));
        if (!Tone.IsEmpty() && Btn) Btn->SetBackgroundColor(ToneColor);
        if (Props.count("disabled") && Btn) Btn->SetIsEnabled(!Props.at("disabled").AsBool(false));
        if (!Tone.IsEmpty() && Label)
            Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.06f, 0.06f, 0.08f, 1.0f)));
        StyleText(Label, Props);
    }
    else if (Type == TEXT("Slider"))
    {
        USlider* Slider = Cast<USlider>(C->Part(TEXT("slider")));
        UTextBlock* Label = Cast<UTextBlock>(C->Part(TEXT("label")));
        UTextBlock* ValueTxt = Cast<UTextBlock>(C->Part(TEXT("value")));
        if (Props.count("label") && Label) SetText(Label, Props.at("label"));
        if (Props.count("value") && Slider) Slider->SetValue(Num(Props.at("value"), 0.0));
        if (Props.count("min") && Slider) Slider->SetMinValue(Num(Props.at("min"), 0.0));
        if (Props.count("max") && Slider) Slider->SetMaxValue(Num(Props.at("max"), 1.0));
        if (Props.count("step") && Slider) Slider->SetStepSize(Num(Props.at("step"), 0.01));
        if (!Tone.IsEmpty() && Slider) Slider->SetSliderHandleColor(ToneColor);

        const bool bV = Props.count("value") != 0;
        const bool bU = Props.count("unit") != 0;
        if (bV) C->State.Add(TEXT("value"), FormatNum(Props.at("value")));
        if (bU) C->State.Add(TEXT("unit"), Str(Props.at("unit")));
        if (bV || bU)
        {
            FString D = C->State.FindRef(TEXT("value"));
            D += C->State.FindRef(TEXT("unit"));
            if (ValueTxt) ValueTxt->SetText(FText::FromString(D));
        }
        if (!Tone.IsEmpty() && ValueTxt) ValueTxt->SetColorAndOpacity(FSlateColor(ToneColor));
        StyleText(ValueTxt, Props);
    }
    else if (Type == TEXT("Divider"))
    {
        if (Props.count("height"))
        {
            if (USpacer* Sp = Cast<USpacer>(C->Part(TEXT("spacer"))))
                Sp->SetSize(FVector2D(1.f, Num(Props.at("height"), 1.0)));
        }
        if (!Tone.IsEmpty())
            C->SetBrush(FSlateRoundedBoxBrush(theme::ToneAt(Tone, theme::FG_DIM(), 0.35f), theme::Radius(0.5f)));
    }
}

void UBorn2FlapUIRenderer::ApplyPrimitiveProps(UWidget* W, const FProps& Props)
{
    if (Props.count("visible"))
    {
        const bool v = Props.at("visible").AsBool(true);
        W->SetVisibility(v ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    }
    if (Props.count("opacity"))
        W->SetRenderOpacity(Num(Props.at("opacity"), 1.0));

    if (UTextBlock* T = Cast<UTextBlock>(W))
    {
        if (Props.count("text") || Props.count("value"))
        {
            const FValue& V = Props.count("text") ? Props.at("text") : Props.at("value");
            SetText(T, V);
        }
        if (!ToneOf(Props).IsEmpty())
            T->SetColorAndOpacity(FSlateColor(theme::Tone(ToneOf(Props), theme::FG())));
        StyleText(T, Props);
    }
    else if (USlider* S = Cast<USlider>(W))
    {
        if (Props.count("value")) S->SetValue(Num(Props.at("value"), 0.0));
        if (Props.count("min")) S->SetMinValue(Num(Props.at("min"), 0.0));
        if (Props.count("max")) S->SetMaxValue(Num(Props.at("max"), 1.0));
        if (Props.count("step")) S->SetStepSize(Num(Props.at("step"), 0.01));
    }
    else if (UProgressBar* B = Cast<UProgressBar>(W))
    {
        if (Props.count("value")) B->SetPercent(Num(Props.at("value"), 0.0));
        if (!ToneOf(Props).IsEmpty())
            B->SetFillColorAndOpacity(theme::Tone(ToneOf(Props), theme::ACCENT()));
    }
    else if (UImage* Im = Cast<UImage>(W))
    {
        if (!ToneOf(Props).IsEmpty())
            Im->SetColorAndOpacity(theme::Tone(ToneOf(Props), theme::FG()));
    }
    else if (USpacer* Sp = Cast<USpacer>(W))
    {
        if (Props.count("size") || Props.count("width") || Props.count("height"))
        {
            float W2 = Props.count("width") ? Num(Props.at("width"), 0.0) : 0.0f;
            float H2 = Props.count("height") ? Num(Props.at("height"), 0.0) : 0.0f;
            if (Props.count("size")) { W2 = H2 = SizeOrNum(Props.at("size"), 0.0); }
            Sp->SetSize(FVector2D(W2, H2));
        }
    }

    if (UPanelWidget* P = Cast<UPanelWidget>(W))
        StoreContainerLayout(P, Props);
}

// ---- layout ----------------------------------------------------------------

void UBorn2FlapUIRenderer::StoreContainerLayout(UPanelWidget* Panel, const FProps& Props)
{
    if (!Panel)
        return;

    FLayoutState S;
    if (FLayoutState* Existing = LayoutState.Find(Panel))
        S = *Existing;

    if (Props.count("spacing") || Props.count("gap"))
    {
        const FValue& V = Props.count("spacing") ? Props.at("spacing") : Props.at("gap");
        S.Spacing = Num(V, 0.0);
        S.bSpacing = true;
    }
    if (Props.count("align"))  { S.H = ParseHAlign(Props.at("align"));  S.bH = true; }
    if (Props.count("valign")) { S.V = ParseVAlign(Props.at("valign")); S.bV = true; }

    LayoutState.Add(Panel, S);
    ApplyStoredLayout(Panel);
}

void UBorn2FlapUIRenderer::ApplyStoredLayout(UPanelWidget* Panel)
{
    if (!Panel)
        return;
    const int32 N = Panel->GetChildrenCount();
    for (int32 i = 0; i < N; ++i)
        ApplyStoredLayoutToChild(Panel, i);
}

void UBorn2FlapUIRenderer::ApplyStoredLayoutToChild(UPanelWidget* Panel, int32 Index)
{
    const FLayoutState* S = LayoutState.Find(Panel);
    if (!S)
        return;
    UWidget* Child = Panel->GetChildAt(Index);
    if (!Child || !Child->Slot)
        return;
    const int32 N = Panel->GetChildrenCount();

    if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Child->Slot))
    {
        if (S->bH) VSlot->SetHorizontalAlignment(S->H);
        if (S->bV) VSlot->SetVerticalAlignment(S->V);
        if (S->bSpacing) VSlot->SetPadding(FMargin(0.f, 0.f, 0.f, (Index == N - 1) ? 0.f : S->Spacing));
    }
    else if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Child->Slot))
    {
        if (S->bH) HSlot->SetHorizontalAlignment(S->H);
        if (S->bV) HSlot->SetVerticalAlignment(S->V);
        if (S->bSpacing) HSlot->SetPadding(FMargin(0.f, 0.f, (Index == N - 1) ? 0.f : S->Spacing, 0.f));
    }
    else if (UOverlaySlot* OSlot = Cast<UOverlaySlot>(Child->Slot))
    {
        if (S->bH) OSlot->SetHorizontalAlignment(S->H);
        if (S->bV) OSlot->SetVerticalAlignment(S->V);
    }
}