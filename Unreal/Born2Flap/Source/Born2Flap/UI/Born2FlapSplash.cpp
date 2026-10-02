#include "UI/Born2FlapSplash.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"
#include "Styling/SlateColor.h"
#include "Widgets/Layout/Anchors.h"
#include "UI/Born2FlapUiTheme.h"

UBorn2FlapSplash::UBorn2FlapSplash(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    static ConstructorHelpers::FObjectFinder<UTexture2D> SplashTex(
        TEXT("/Game/Splash/born2flap-splash-new.born2flap-splash-new"));
    SplashTexture = SplashTex.Succeeded() ? SplashTex.Object : nullptr;
}

TSharedRef<SWidget> UBorn2FlapSplash::RebuildWidget()
{
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, UWidgetTree::StaticClass());
    }

    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
        UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
    WidgetTree->RootWidget = Root;

    // Full-bleed background artwork.
    BackgroundImage = WidgetTree->ConstructWidget<UImage>(
        UImage::StaticClass(), TEXT("SplashBackground"));
    Root->AddChild(BackgroundImage);
    if (SplashTexture)
    {
        BackgroundImage->SetBrushFromTexture(SplashTexture);
    }
    if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(BackgroundImage->Slot))
    {
        CanvasSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
        CanvasSlot->SetOffsets(FMargin(0.f));
    }

    // "LOADING nn%" label just above the bar.
    StatusText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("StatusText"));
    Root->AddChild(StatusText);
    {
        // Website UI identity: Chakra Petch face + the site's text colour.
        StatusText->SetFont(born2flap::ui::theme::FontPx(18.f, false));
        StatusText->SetColorAndOpacity(FSlateColor(born2flap::ui::theme::FG()));
        StatusText->SetJustification(ETextJustify::Center);
        StatusText->SetText(FText::FromString(TEXT("LOADING")));
    }
    if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(StatusText->Slot))
    {
        CanvasSlot->SetAnchors(FAnchors(0.5f, 0.80f));
        CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        CanvasSlot->SetAutoSize(true);
    }

    // The loading bar.
    LoadingBar = WidgetTree->ConstructWidget<UProgressBar>(
        UProgressBar::StaticClass(), TEXT("LoadingBar"));
    Root->AddChild(LoadingBar);
    {
        FProgressBarStyle Style = LoadingBar->GetWidgetStyle();
        Style.BackgroundImage = FSlateColorBrush(born2flap::ui::theme::BG_SOLID());
        Style.FillImage = FSlateColorBrush(born2flap::ui::theme::ACCENT());
        Style.MarqueeImage = FSlateColorBrush(born2flap::ui::theme::ACCENT());
        LoadingBar->SetWidgetStyle(Style);
        LoadingBar->SetPercent(0.f);
    }
    if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(LoadingBar->Slot))
    {
        CanvasSlot->SetAnchors(FAnchors(0.5f, 0.86f));
        CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        CanvasSlot->SetSize(FVector2D(680.f, 24.f));
        CanvasSlot->SetAutoSize(false);
    }

    return Super::RebuildWidget();
}

void UBorn2FlapSplash::SetProgress(float Fraction)
{
    Fraction = FMath::Clamp(Fraction, 0.f, 1.f);
    if (LoadingBar)
    {
        LoadingBar->SetPercent(Fraction);
    }
    if (StatusText)
    {
        StatusText->SetText(FText::FromString(
            FString::Printf(TEXT("LOADING %d%%"), FMath::RoundToInt(Fraction * 100.f))));
    }
}