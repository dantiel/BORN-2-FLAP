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
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

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

// ---- semantic value formatting (the "prowess" of numeric components) ------
// A component knows how to *read* its own value: decimals are inferred from
// the step size unless an explicit `format` token ("int", "f1"…"f4") is given.
// This is semantic formatting (how the value means something), not styling —
// no fonts or colors are involved.

int32 DecimalsForStep(double Step)
{
    Step = FMath::Abs(Step);
    if (Step <= 0.0) return 2;
    int32 D = 0;
    while (Step < 1.0 - 1e-9 && D < 6) { Step *= 10.0; ++D; }
    return D;
}

int32 DecimalsFor(const FProps& Props, double Step)
{
    auto it = Props.find("format");
    if (it != Props.end())
    {
        const FString F = Str(it->second);
        if (F == TEXT("int")) return 0;
        if (F.Len() == 2 && F[0] == 'f' && F[1] >= '0' && F[1] <= '9')
            return F[1] - '0';
    }
    return DecimalsForStep(Step);
}

FString Readout(double Value, int32 Decimals, const FString& Unit)
{
    FString S = FString::SanitizeFloat((float)Value, Decimals);
    if (!Unit.IsEmpty()) S += Unit;
    return S;
}

UBorn2FlapComposite* NewComposite(UObject* Outer, const FString& Type)
{
    UBorn2FlapComposite* C = NewObject<UBorn2FlapComposite>(Outer);
    C->SemanticType = Type;
    C->SetBrush(FSlateRoundedBoxBrush(theme::BG(), theme::Radius()));
    C->SetPadding(FMargin(10.f));
    return C;
}

// Textures referenced by the `texture` prop on Image widgets. SetBrushFromTexture
// stores the UObject in a plain (non-UPROPERTY) TObjectPtr — it does NOT root the
// texture (the same GC disease as the Slate-brush crash). A program-lifetime cache
// keeps each imported texture alive so a GC pass cannot collect it behind the brush.
TMap<FString, TStrongObjectPtr<UTexture2D>> GTextureCache;

UTexture2D* GetOrLoadTexture(const FString& Path)
{
    if (TStrongObjectPtr<UTexture2D>* Cached = GTextureCache.Find(Path))
        return Cached->Get();
    UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path);
    if (IsValid(Tex))
    {
        GTextureCache.Add(Path, TStrongObjectPtr<UTexture2D>(Tex));
        return Tex;
    }
    return nullptr;
}

}  // namespace

UBorn2FlapUIRenderer::UBorn2FlapUIRenderer()
    : RendererImpl(MakeUnique<Renderer>(*this))
    , AudioEngine(MakeUnique<born2flap::audio::FAudioEngine>())
{
}

// ---- action relay (UMG dynamic delegates → semantic component actions) ----

void UBorn2FlapActionRelay::OnSlider(float Value)
{
    if (Renderer.IsValid())
        Renderer->HandleSliderChanged(Composite, Value);
}

void UBorn2FlapActionRelay::OnClick()
{
    if (Renderer.IsValid())
        Renderer->HandleClicked(Composite, OptionIndex);
}

void UBorn2FlapActionRelay::OnNumber(const FText& Text, ETextCommit::Type CommitMethod)
{
    if (Renderer.IsValid())
        Renderer->HandleNumberCommitted(Composite, Text, CommitMethod);
}

// ---- semantic action handling (value semantics, zero styling) -------------

void UBorn2FlapUIRenderer::EmitAction(const FString& Action, double Value, const FString& Text)
{
    if (Action.IsEmpty())
        return;
    OnComponentAction.Broadcast(Action, (float)Value, Text);
}

void UBorn2FlapUIRenderer::HandleSliderChanged(UBorn2FlapComposite* C, float Value)
{
    if (!C)
        return;
    // Snap to step so a slider with step=1 lands on whole numbers (the readout
    // and the emitted value agree). Step/min/max are re-read from State.
    double Step = FCString::Atof(*C->State.FindRef(TEXT("step")));
    if (Step > 0.0)
        Value = (float)(FMath::RoundToDouble(Value / Step) * Step);

    const double Min = FCString::Atof(*C->State.FindRef(TEXT("min")));
    const double Max = FCString::Atof(*C->State.FindRef(TEXT("max")));
    if (C->State.Contains(TEXT("min")) && C->State.Contains(TEXT("max")))
        Value = (float)FMath::Clamp((double)Value, Min, Max);

    const int32 D = FCString::Atoi(*C->State.FindRef(TEXT("decimals")));
    const FString Unit = C->State.FindRef(TEXT("unit"));
    C->State.Add(TEXT("value"), Readout(Value, D, Unit));
    if (UTextBlock* V = Cast<UTextBlock>(C->Part(TEXT("value"))))
        V->SetText(FText::FromString(C->State.FindRef(TEXT("value"))));

    EmitAction(C->Action, Value, TEXT(""));
}

void UBorn2FlapUIRenderer::HandleClicked(UBorn2FlapComposite* C, int32 OptionIndex)
{
    if (!C)
        return;

    const FString Type = C->SemanticType;

    // Section folding is intrinsic behaviour — it works with or without an
    // action key (the fold state itself is the semantics).
    if (Type == TEXT("Section"))
    {
        const bool bOpen = !C->State.FindRef(TEXT("open")).Equals(TEXT("1"));
        C->State.Add(TEXT("open"), bOpen ? TEXT("1") : TEXT("0"));
        ApplySectionFold(C, bOpen);
        EmitAction(C->Action, bOpen ? 1.0 : 0.0, TEXT(""));
        return;
    }

    if (C->Action.IsEmpty())
        return;

    if (Type == TEXT("Toggle"))
    {
        const bool bNow = !C->State.FindRef(TEXT("value")).Equals(TEXT("1"));
        C->State.Add(TEXT("value"), bNow ? TEXT("1") : TEXT("0"));
        ApplyToggleState(C);
        EmitAction(C->Action, bNow ? 1.0 : 0.0, TEXT(""));
    }
    else if (Type == TEXT("Select") && OptionIndex != INDEX_NONE)
    {
        C->State.Add(TEXT("selected"), FString::FromInt(OptionIndex));
        ApplySelectSelection(C, OptionIndex);
        EmitAction(C->Action, (double)OptionIndex, TEXT(""));
    }
    else
    {
        EmitAction(C->Action, 1.0, TEXT(""));
    }
}

void UBorn2FlapUIRenderer::HandleNumberCommitted(UBorn2FlapComposite* C, const FText& Text, ETextCommit::Type Commit)
{
    if (!C || C->Action.IsEmpty())
        return;
    if (Commit != ETextCommit::OnEnter && Commit != ETextCommit::OnUserMovedFocus)
        return;

    double Value = FCString::Atof(*Text.ToString());
    const double Min = FCString::Atof(*C->State.FindRef(TEXT("min")));
    const double Max = FCString::Atof(*C->State.FindRef(TEXT("max")));
    if (C->State.Contains(TEXT("min")) && C->State.Contains(TEXT("max")))
        Value = FMath::Clamp(Value, Min, Max);

    const int32 D = FCString::Atoi(*C->State.FindRef(TEXT("decimals")));
    const FString Unit = C->State.FindRef(TEXT("unit"));
    C->State.Add(TEXT("value"), Readout(Value, D, Unit));
    if (UEditableTextBox* E = Cast<UEditableTextBox>(C->Part(TEXT("edit"))))
        E->SetText(FText::FromString(C->State.FindRef(TEXT("value"))));

    EmitAction(C->Action, Value, TEXT(""));
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
        T == TEXT("Slider") || T == TEXT("Toggle") || T == TEXT("Select") ||
        T == TEXT("NumberBox") || T == TEXT("Field") || T == TEXT("Section") ||
        T == TEXT("Divider"))
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
    if (Type == "ScrollBox")     return NewObject<UScrollBox>(this);
    if (Type == "SizeBox")       return NewObject<USizeBox>(this);

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
    RootWidget = W;
    AttachRoot();
}

void UBorn2FlapUIRenderer::AttachRoot()
{
    EnsureViewport();
    if (!ViewportCanvas || !RootWidget)
        return;
    if (RootWidget->Slot)
        return;  // already mounted in the canvas
    UCanvasPanelSlot* Slot = ViewportCanvas->AddChildToCanvas(RootWidget);
    Slot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    Slot->SetOffsets(FMargin(0.f));
}

void UBorn2FlapUIRenderer::EnsureAttached()
{
    AttachRoot();
}

void UBorn2FlapUIRenderer::AppendChild(UWidget* Parent, int32 Index, UWidget* Child)
{
    if (UBorn2FlapComposite* C = Cast<UBorn2FlapComposite>(Parent))
    {
        if (UPanelWidget* Panel = C->ContentPanel())
        {
            Panel->InsertChildAt(Index, Child);
            ApplyStoredLayoutToChild(Panel, Index);
        }
        return;
    }
    // Single-child content widgets (SizeBox / Border) host their child via
    // SetContent rather than an indexed child list.
    if (USizeBox* SB = Cast<USizeBox>(Parent)) { SB->SetContent(Child); return; }
    if (UBorder* B = Cast<UBorder>(Parent))    { B->SetContent(Child); return; }

    UPanelWidget* Panel = Cast<UPanelWidget>(Parent);
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

void UBorn2FlapUIRenderer::Close()
{
    if (RootHost)
        RootHost->RemoveFromParent();
    RootHost = nullptr;
    ViewportCanvas = nullptr;
    RootWidget = nullptr;
}

void UBorn2FlapUIRenderer::BeginDestroy()
{
    MaterialCache.Empty();
    LayoutState.Empty();
    Relays.Empty();
    RendererImpl.Reset();
    Close();
    Super::BeginDestroy();
}

// ---- helpers ---------------------------------------------------------------

void UBorn2FlapUIRenderer::EnsureViewport()
{
    if (RootHost)
        return;
    UWorld* World = GetWorld();
    // CreateWidget bakes the owning player in at creation time. The splash
    // spawns from GameMode::BeginPlay, before the PlayerController exists, so
    // creating the host too early leaves it without an owning player and it
    // never attaches to a real viewport. Defer until a local player is live;
    // EnsureAttached() re-drives this once per splash tick.
    if (!World || !World->GetFirstLocalPlayerFromController())
        return;
    RootHost = CreateWidget<UBorn2FlapRootWidget>(World);
    ViewportCanvas = NewObject<UCanvasPanel>(RootHost);
    RootHost->WidgetTree->RootWidget = ViewportCanvas;
    RootHost->AddToViewport(ViewportZOrder);
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
        // The content region must fill the panel's body (which fills the
        // border) so a ScrollBox inside it gets a bounded height and scrolls
        // instead of overflowing the card.
        if (UVerticalBoxSlot* ContentSlot = Body->AddChildToVerticalBox(Content))
        {
            ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            ContentSlot->SetVerticalAlignment(VAlign_Fill);
        }
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
        BindButton(Btn, C);
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
        BindSlider(Slider, C);
        return C;
    }
    if (Type == TEXT("Toggle"))
    {
        UBorn2FlapComposite* C = NewComposite(this, TEXT("Toggle"));
        C->SetPadding(FMargin(0.f));
        UButton* Btn = NewObject<UButton>(this);
        Btn->SetBackgroundColor(theme::BG_SOLID());
        C->SetContent(Btn);
        UHorizontalBox* Row = NewObject<UHorizontalBox>(this);
        Btn->SetContent(Row);
        UTextBlock* Label = NewObject<UTextBlock>(this);
        Label->SetFont(theme::Font(TEXT("s"), 12));
        Label->SetColorAndOpacity(FSlateColor(theme::FG_DIM()));
        Row->AddChildToHorizontalBox(Label);
        UTextBlock* Value = NewObject<UTextBlock>(this);
        Value->SetFont(theme::Font(TEXT("s"), 12, true));
        Row->AddChildToHorizontalBox(Value);
        C->Parts.Add(TEXT("label"), Label);
        C->Parts.Add(TEXT("value"), Value);
        C->Parts.Add(TEXT("button"), Btn);
        BindButton(Btn, C);
        return C;
    }
    if (Type == TEXT("Select"))
    {
        UBorn2FlapComposite* C = NewComposite(this, TEXT("Select"));
        UVerticalBox* Body = NewObject<UVerticalBox>(this);
        C->SetContent(Body);
        UTextBlock* Label = NewObject<UTextBlock>(this);
        Label->SetFont(theme::Font(TEXT("s"), 12));
        Label->SetColorAndOpacity(FSlateColor(theme::FG_DIM()));
        Label->SetVisibility(ESlateVisibility::Collapsed);
        Body->AddChildToVerticalBox(Label);
        UHorizontalBox* Options = NewObject<UHorizontalBox>(this);
        Body->AddChildToVerticalBox(Options);
        C->Parts.Add(TEXT("label"), Label);
        C->Parts.Add(TEXT("options"), Options);
        return C;
    }
    if (Type == TEXT("NumberBox"))
    {
        UBorn2FlapComposite* C = NewComposite(this, TEXT("NumberBox"));
        UVerticalBox* Body = NewObject<UVerticalBox>(this);
        C->SetContent(Body);
        UHorizontalBox* Head = NewObject<UHorizontalBox>(this);
        Body->AddChildToVerticalBox(Head);
        UTextBlock* Label = NewObject<UTextBlock>(this);
        Label->SetFont(theme::Font(TEXT("s"), 12));
        Label->SetColorAndOpacity(FSlateColor(theme::FG_DIM()));
        Head->AddChildToHorizontalBox(Label);
        UEditableTextBox* Edit = NewObject<UEditableTextBox>(this);
        FEditableTextBoxStyle Style = Edit->WidgetStyle;
        Style.TextStyle.Font = theme::Font(TEXT("m"), 14);
        Style.TextStyle.ColorAndOpacity = FSlateColor(theme::FG());
        Edit->SetWidgetStyle(Style);
        Head->AddChildToHorizontalBox(Edit);
        C->Parts.Add(TEXT("label"), Label);
        C->Parts.Add(TEXT("edit"), Edit);
        BindNumber(Edit, C);
        return C;
    }
    if (Type == TEXT("Field"))
    {
        UBorn2FlapComposite* C = NewComposite(this, TEXT("Field"));
        C->SetPadding(FMargin(0.f));
        UHorizontalBox* Body = NewObject<UHorizontalBox>(this);
        C->SetContent(Body);
        UTextBlock* Label = NewObject<UTextBlock>(this);
        Label->SetFont(theme::Font(TEXT("s"), 12));
        Label->SetColorAndOpacity(FSlateColor(theme::FG_DIM()));
        Body->AddChildToHorizontalBox(Label);
        UHorizontalBox* Control = NewObject<UHorizontalBox>(this);
        Body->AddChildToHorizontalBox(Control);
        C->Parts.Add(TEXT("label"), Label);
        C->Content = Control;
        return C;
    }
    if (Type == TEXT("Section"))
    {
        UBorn2FlapComposite* C = NewComposite(this, TEXT("Section"));
        UVerticalBox* Body = NewObject<UVerticalBox>(this);
        C->SetContent(Body);
        UButton* Header = NewObject<UButton>(this);
        Header->SetBackgroundColor(theme::BG_SOLID());
        Body->AddChildToVerticalBox(Header);
        UHorizontalBox* HeadRow = NewObject<UHorizontalBox>(this);
        Header->SetContent(HeadRow);
        UTextBlock* Arrow = NewObject<UTextBlock>(this);
        Arrow->SetFont(theme::Font(TEXT("m"), 14));
        Arrow->SetColorAndOpacity(FSlateColor(theme::ACCENT()));
        Arrow->SetText(FText::FromString(TEXT("▾")));
        HeadRow->AddChildToHorizontalBox(Arrow);
        UTextBlock* Title = NewObject<UTextBlock>(this);
        Title->SetFont(theme::Font(TEXT("m"), 14, true));
        HeadRow->AddChildToHorizontalBox(Title);
        UVerticalBox* Content = NewObject<UVerticalBox>(this);
        Body->AddChildToVerticalBox(Content);
        C->Parts.Add(TEXT("header"), Header);
        C->Parts.Add(TEXT("arrow"), Arrow);
        C->Parts.Add(TEXT("title"), Title);
        C->Content = Content;
        BindButton(Header, C);
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
    const bool bInteractive = (Type == TEXT("Button") || Type == TEXT("Slider") ||
                               Type == TEXT("Toggle") || Type == TEXT("Select") ||
                               Type == TEXT("NumberBox") || Type == TEXT("Section"));

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
    if (Props.count("action"))
        C->Action = Str(Props.at("action"));

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

        double Min  = Slider ? (double)Slider->GetMinValue()  : 0.0;
        double Max  = Slider ? (double)Slider->GetMaxValue()  : 1.0;
        double Step = Slider ? (double)Slider->GetStepSize()  : 0.01;
        if (Props.count("min"))  { Min  = Props.at("min").AsNumber();  if (Slider) Slider->SetMinValue((float)Min);  }
        if (Props.count("max"))  { Max  = Props.at("max").AsNumber();  if (Slider) Slider->SetMaxValue((float)Max);  }
        if (Props.count("step")) { Step = Props.at("step").AsNumber(); if (Slider) Slider->SetStepSize((float)Step); }

        // Semantic cache: the relay snaps/clamps/formats from these on move.
        C->State.Add(TEXT("min"), FString::SanitizeFloat((float)Min));
        C->State.Add(TEXT("max"), FString::SanitizeFloat((float)Max));
        C->State.Add(TEXT("step"), FString::SanitizeFloat((float)Step));
        C->State.Add(TEXT("decimals"), FString::FromInt(DecimalsFor(Props, Step)));
        if (Props.count("unit")) C->State.Add(TEXT("unit"), Str(Props.at("unit")));

        double Value = Props.count("value") ? Props.at("value").AsNumber()
                                            : (Slider ? (double)Slider->GetValue() : 0.0);
        if (Props.count("value") && Slider) Slider->SetValue((float)Value);
        const int32 D = FCString::Atoi(*C->State.FindRef(TEXT("decimals")));
        const FString Unit = C->State.FindRef(TEXT("unit"));
        C->State.Add(TEXT("value"), Readout(Value, D, Unit));
        if (ValueTxt) ValueTxt->SetText(FText::FromString(C->State.FindRef(TEXT("value"))));

        if (!Tone.IsEmpty() && Slider) Slider->SetSliderHandleColor(ToneColor);
        if (!Tone.IsEmpty() && ValueTxt) ValueTxt->SetColorAndOpacity(FSlateColor(ToneColor));
        if (Props.count("disabled") && Slider) Slider->SetIsEnabled(!Props.at("disabled").AsBool(false));
        StyleText(ValueTxt, Props);
    }
    else if (Type == TEXT("Toggle"))
    {
        UTextBlock* Label = Cast<UTextBlock>(C->Part(TEXT("label")));
        if (Props.count("label") && Label) SetText(Label, Props.at("label"));
        if (Props.count("on"))  C->State.Add(TEXT("on"),  Str(Props.at("on")));
        if (Props.count("off")) C->State.Add(TEXT("off"), Str(Props.at("off")));
        if (Props.count("value"))
        {
            C->State.Add(TEXT("value"), Props.at("value").AsBool(false) ? TEXT("1") : TEXT("0"));
            ApplyToggleState(C);
        }
        if (Props.count("disabled"))
            if (UButton* B = Cast<UButton>(C->Part(TEXT("button"))))
                B->SetIsEnabled(!Props.at("disabled").AsBool(false));
    }
    else if (Type == TEXT("Select"))
    {
        UTextBlock* Label = Cast<UTextBlock>(C->Part(TEXT("label")));
        if (Props.count("label") && Label) SetText(Label, Props.at("label"));
        if (Props.count("options"))
            RebuildSelectOptions(C, Props.at("options"));
        if (Props.count("value"))
        {
            const int32 Index = (int32)Props.at("value").AsNumber(0);
            C->State.Add(TEXT("selected"), FString::FromInt(Index));
            ApplySelectSelection(C, Index);
        }
    }
    else if (Type == TEXT("NumberBox"))
    {
        UTextBlock* Label = Cast<UTextBlock>(C->Part(TEXT("label")));
        UEditableTextBox* Edit = Cast<UEditableTextBox>(C->Part(TEXT("edit")));
        if (Props.count("label") && Label) SetText(Label, Props.at("label"));

        double Min = 0.0, Max = 0.0, Step = 0.0;
        if (Props.count("min"))  Min  = Props.at("min").AsNumber();
        if (Props.count("max"))  Max  = Props.at("max").AsNumber();
        if (Props.count("step")) Step = Props.at("step").AsNumber();
        C->State.Add(TEXT("min"), FString::SanitizeFloat((float)Min));
        C->State.Add(TEXT("max"), FString::SanitizeFloat((float)Max));
        if (Props.count("unit")) C->State.Add(TEXT("unit"), Str(Props.at("unit")));
        C->State.Add(TEXT("decimals"), FString::FromInt(DecimalsFor(Props, Step)));

        double Value = Props.count("value") ? Props.at("value").AsNumber() : 0.0;
        const int32 D = FCString::Atoi(*C->State.FindRef(TEXT("decimals")));
        const FString Unit = C->State.FindRef(TEXT("unit"));
        C->State.Add(TEXT("value"), Readout(Value, D, Unit));
        if (Props.count("value") && Edit)
            Edit->SetText(FText::FromString(C->State.FindRef(TEXT("value"))));
        if (Props.count("placeholder") && Edit)
            Edit->SetHintText(FText::FromString(Str(Props.at("placeholder"))));
        if (Props.count("disabled") && Edit)
            Edit->SetIsReadOnly(Props.at("disabled").AsBool(false));
    }
    else if (Type == TEXT("Field"))
    {
        UTextBlock* Label = Cast<UTextBlock>(C->Part(TEXT("label")));
        if (Props.count("label") && Label) SetText(Label, Props.at("label"));
        if (C->ContentPanel())
            StoreContainerLayout(C->ContentPanel(), Props);
    }
    else if (Type == TEXT("Section"))
    {
        UTextBlock* Title = Cast<UTextBlock>(C->Part(TEXT("title")));
        if (Props.count("title") && Title) SetText(Title, Props.at("title"));
        if (Props.count("open"))
        {
            const bool bOpen = Props.at("open").AsBool(true);
            C->State.Add(TEXT("open"), bOpen ? TEXT("1") : TEXT("0"));
            ApplySectionFold(C, bOpen);
        }
        if (Props.count("disabled"))
            if (UButton* H = Cast<UButton>(C->Part(TEXT("header"))))
                H->SetIsEnabled(!Props.at("disabled").AsBool(false));
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
        if (Props.count("texture"))
            if (UTexture2D* Tex = GetOrLoadTexture(Str(Props.at("texture"))))
                Im->SetBrushFromTexture(Tex);
        if (!ToneOf(Props).IsEmpty())
            Im->SetColorAndOpacity(theme::Tone(ToneOf(Props), theme::FG()));
    }
    else if (USizeBox* SB = Cast<USizeBox>(W))
    {
        if (Props.count("width"))  SB->SetWidthOverride(Num(Props.at("width"), 0.0));
        if (Props.count("height")) SB->SetHeightOverride(Num(Props.at("height"), 0.0));
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

// ---- interactivity wiring (thin, event-only — no styling) ------------------

void UBorn2FlapUIRenderer::BindSlider(USlider* S, UBorn2FlapComposite* C)
{
    if (!S || !C)
        return;
    UBorn2FlapActionRelay* R = NewObject<UBorn2FlapActionRelay>(this);
    R->Renderer = this;
    R->Composite = C;
    Relays.Add(R);
    S->OnValueChanged.AddDynamic(R, &UBorn2FlapActionRelay::OnSlider);
}

void UBorn2FlapUIRenderer::BindButton(UButton* B, UBorn2FlapComposite* C, int32 OptionIndex)
{
    if (!B || !C)
        return;
    UBorn2FlapActionRelay* R = NewObject<UBorn2FlapActionRelay>(this);
    R->Renderer = this;
    R->Composite = C;
    R->OptionIndex = OptionIndex;
    Relays.Add(R);
    B->OnClicked.AddDynamic(R, &UBorn2FlapActionRelay::OnClick);
}

void UBorn2FlapUIRenderer::BindNumber(UEditableTextBox* E, UBorn2FlapComposite* C)
{
    if (!E || !C)
        return;
    UBorn2FlapActionRelay* R = NewObject<UBorn2FlapActionRelay>(this);
    R->Renderer = this;
    R->Composite = C;
    Relays.Add(R);
    E->OnTextCommitted.AddDynamic(R, &UBorn2FlapActionRelay::OnNumber);
}

// Rebuild a Select's option buttons from a semantic `options` array of labels.
// The selected option carries the accent tone; others are muted. Each option
// is a self-describing button: it reports its own index via the shared relay.
void UBorn2FlapUIRenderer::RebuildSelectOptions(UBorn2FlapComposite* C, const FValue& Options)
{
    if (!C)
        return;
    UHorizontalBox* Box = Cast<UHorizontalBox>(C->Part(TEXT("options")));
    if (!Box)
        return;

    Box->ClearChildren();

    TArray<FString> Labels;
    if (Options.kind == FValue::Kind::Array)
        for (const FValue& O : Options.arr)
            Labels.Add(Str(O));
    else if (Options.kind == FValue::Kind::String)
        Labels.Add(Str(Options));

    FString Csv;
    for (int32 i = 0; i < Labels.Num(); ++i)
    {
        UButton* B = NewObject<UButton>(this);
        B->SetBackgroundColor(theme::BG_SOLID());
        UTextBlock* L = NewObject<UTextBlock>(this);
        L->SetFont(theme::Font(TEXT("s"), 12));
        L->SetColorAndOpacity(FSlateColor(theme::FG_DIM()));
        L->SetText(FText::FromString(Labels[i]));
        B->SetContent(L);
        Box->AddChildToHorizontalBox(B);
        BindButton(B, C, i);
        Csv += (i ? TEXT(",") : TEXT("")) + Labels[i];
    }
    C->State.Add(TEXT("options"), Csv);
}

void UBorn2FlapUIRenderer::ApplySelectSelection(UBorn2FlapComposite* C, int32 Index)
{
    if (!C)
        return;
    UHorizontalBox* Box = Cast<UHorizontalBox>(C->Part(TEXT("options")));
    if (!Box)
        return;
    const int32 N = Box->GetChildrenCount();
    for (int32 i = 0; i < N; ++i)
    {
        UWidget* Child = Box->GetChildAt(i);
        UButton* B = Cast<UButton>(Child);
        UTextBlock* L = B ? Cast<UTextBlock>(B->GetContent()) : nullptr;
        if (B) B->SetBackgroundColor(i == Index ? theme::ACCENT() : theme::BG_SOLID());
        if (L) L->SetColorAndOpacity(i == Index
            ? FSlateColor(FLinearColor(0.06f, 0.06f, 0.08f, 1.0f))
            : FSlateColor(theme::FG_DIM()));
    }
}

void UBorn2FlapUIRenderer::ApplyToggleState(UBorn2FlapComposite* C)
{
    if (!C)
        return;
    const bool bOn = C->State.FindRef(TEXT("value")).Equals(TEXT("1"));
    UButton* B = Cast<UButton>(C->Part(TEXT("button")));
    if (B) B->SetBackgroundColor(bOn ? theme::GOOD() : theme::BG_SOLID());
    if (UTextBlock* V = Cast<UTextBlock>(C->Part(TEXT("value"))))
    {
        const FString On  = C->State.FindRef(TEXT("on")).IsEmpty()  ? TEXT("ON")  : C->State.FindRef(TEXT("on"));
        const FString Off = C->State.FindRef(TEXT("off")).IsEmpty() ? TEXT("OFF") : C->State.FindRef(TEXT("off"));
        V->SetText(FText::FromString(bOn ? On : Off));
        V->SetColorAndOpacity(FSlateColor(bOn ? FLinearColor(0.06f, 0.06f, 0.08f, 1.0f) : theme::FG_DIM()));
    }
}

void UBorn2FlapUIRenderer::ApplySectionFold(UBorn2FlapComposite* C, bool bOpen)
{
    if (!C)
        return;
    if (UPanelWidget* Body = C->ContentPanel())
        Body->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (UTextBlock* Arrow = Cast<UTextBlock>(C->Part(TEXT("arrow"))))
        Arrow->SetText(FText::FromString(bOpen ? TEXT("▾") : TEXT("▸")));
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
    UWidget* Child = Panel->GetChildAt(Index);
    if (!Child || !Child->Slot)
        return;
    const int32 N = Panel->GetChildrenCount();

    // A ScrollBox placed in a linear box slot defaults to "Automatic" sizing,
    // so it reports its full content height and overflows its panel instead of
    // scrolling. Pin it to the remaining space so it clips and scrolls.
    if (Cast<UScrollBox>(Child))
    {
        if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Child->Slot))
        {
            VSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            VSlot->SetVerticalAlignment(VAlign_Fill);
        }
        else if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Child->Slot))
        {
            HSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            HSlot->SetHorizontalAlignment(HAlign_Fill);
        }
    }

    if (!S)
        return;

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