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

UBorn2FlapSplash::UBorn2FlapSplash(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    static ConstructorHelpers::FObjectFinder<UTexture2D> SplashTex(
        TEXT("/Game/Splash/born2flap-splash.born2flap-splash"));
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
        FSlateFontInfo Font = StatusText->GetFont();
        Font.Size = 18;
        StatusText->SetFont(Font);
        StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.92f)));
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
        Style.BackgroundImage = FSlateColorBrush(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f));
        Style.FillImage = FSlateColorBrush(FLinearColor(0.10f, 0.56f, 0.95f, 1.0f));
        Style.MarqueeImage = FSlateColorBrush(FLinearColor(0.10f, 0.56f, 0.95f, 1.0f));
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