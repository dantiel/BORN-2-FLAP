#include "Born2Flap.h"
#include "Modules/ModuleManager.h"
#include "MoviePlayer.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Rendering/DrawElements.h"
#include "ContentStreaming.h"
#include "HAL/PlatformProcess.h"
#include "UI/Born2FlapUiTheme.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#if PLATFORM_WINDOWS
namespace
{
    // The native engine splash (WindowsApplication.ShowSplash) is a separate
    // window ("SplashScreenClass" / "SplashScreen") that is not created
    // topmost, so the game's main window — created later while the map and its
    // shaders are still loading — can appear above it and cover it with a
    // black surface. Pin the splash topmost while it is up so it stays visible
    // until the engine hides it on the first rendered frame.
    void PinSplashWindowTopmost()
    {
        // The splash runs on a background thread; give it a moment to exist.
        for (int32 Attempt = 0; Attempt < 200; ++Attempt)
        {
            HWND Splash = FindWindowW(L"SplashScreenClass", L"SplashScreen");
            if (Splash)
            {
                SetWindowPos(Splash, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                return;
            }
            FPlatformProcess::Sleep(0.005f);
        }
    }
}
#endif

// A bare indeterminate loading bar: a subtle track with a segment that sweeps
// left→right based on wall-clock time. It animates in OnPaint from the wall
// clock because the movie player's slate thread repaints every frame but only
// advances Slate *time* (ESlateTickType::Time) — it never runs active timers,
// so a stock SProgressBar marquee would stay frozen during the level load.
class SBorn2FlapLoadingBar : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SBorn2FlapLoadingBar)
        : _FillColor(born2flap::ui::theme::ACCENT())
        , _TrackColor(FLinearColor(1.f, 1.f, 1.f, 0.14f))
        , _Height(5.f)
    {}
        SLATE_ARGUMENT(FLinearColor, FillColor)
        SLATE_ARGUMENT(FLinearColor, TrackColor)
        SLATE_ARGUMENT(float, Height)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        FillColor = InArgs._FillColor;
        TrackColor = InArgs._TrackColor;
        BarHeight = InArgs._Height;
    }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
    {
        const FVector2D Size = AllottedGeometry.GetLocalSize();
        const float H = FMath::Max(1.f, FMath::Min(BarHeight, Size.Y));
        const float Y = (Size.Y - H) * 0.5f;
        const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");

        // Track.
        FSlateDrawElement::MakeBox(
            OutDrawElements, LayerId,
            AllottedGeometry.ToPaintGeometry(FVector2f(Size.X, H), FSlateLayoutTransform(FVector2f(0.f, Y))),
            Brush, ESlateDrawEffect::None, TrackColor);

        // Sweeping segment (25% wide), offset driven by the fractional wall clock.
        const float T = FMath::Fractional((float)FPlatformTime::Seconds());
        const float SegW = Size.X * 0.25f;
        const float X = (T * (Size.X + SegW)) - SegW;
        FSlateDrawElement::MakeBox(
            OutDrawElements, LayerId + 1,
            AllottedGeometry.ToPaintGeometry(FVector2f(SegW, H), FSlateLayoutTransform(FVector2f(X, Y))),
            Brush, ESlateDrawEffect::None, FillColor);

        return LayerId + 2;
    }

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(240.f, BarHeight);
    }

private:
    FLinearColor FillColor;
    FLinearColor TrackColor;
    float BarHeight = 5.f;
};

// Primary game module with a full-screen level-loading screen. The menu calls
// Born2FlapShowLoadingScreen() just before OpenLevel; the movie player keeps the
// viewport covered (main window hidden) until the new map finishes loading.
class FBorn2FlapModule : public FDefaultGameModuleImpl
{
public:
    FBorn2FlapModule() { GModule = this; }

    virtual void StartupModule() override
    {
        FDefaultGameModuleImpl::StartupModule();
#if PLATFORM_WINDOWS
        PinSplashWindowTopmost();
#endif
        FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(this, &FBorn2FlapModule::OnPostLoadMap);
    }

    virtual void ShutdownModule() override
    {
        FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
        FDefaultGameModuleImpl::ShutdownModule();
    }

    static void PlayLoadingScreen()
    {
        if (GModule)
            GModule->ShowLoadingScreen();
    }

private:
    void ShowLoadingScreen()
    {
        IGameMoviePlayer* MoviePlayer = GetMoviePlayer();
        if (!MoviePlayer)
            return;
        if (!LoadingTexture.IsValid())
        {
            LoadingTexture = TStrongObjectPtr<UTexture2D>(LoadObject<UTexture2D>(nullptr,
                TEXT("/Game/Splash/born2flap-splash-new.born2flap-splash-new")));
            if (!LoadingTexture.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("Born2FlapModule: loading-screen texture missing"));
                return;
            }
            // Same pattern as UBorn2FlapUIRenderer::ArtBrush: root the texture
            // (TStrongObjectPtr) so GC can't collect it behind the Slate brush.
            LoadingBrush.SetResourceObject(LoadingTexture.Get());
            LoadingBrush.ImageSize = FVector2D(LoadingTexture->GetSizeX(), LoadingTexture->GetSizeY());
            LoadingBrush.DrawAs = ESlateBrushDrawType::Image;
            LoadingBrush.TintColor = FLinearColor::White;
        }

        FLoadingScreenAttributes Attr;
        Attr.bAutoCompleteWhenLoadingCompletes = true;
        Attr.bWaitForManualStop = false;
        Attr.bAllowEngineTick = false;
        Attr.MinimumLoadingScreenDisplayTime = 3.0f;
        // Full-bleed artwork + a text label + a bare indeterminate bar that
        // sweeps from wall-clock time (so it keeps moving on the movie player's
        // slate thread, which never runs Slate active timers). The screen
        // auto-completes when loading finishes.
        Attr.WidgetLoadingScreen =
            SNew(SConstraintCanvas)
            + SConstraintCanvas::Slot()
              .Anchors(FAnchors(0.f, 0.f, 1.f, 1.f))
              .Offset(FMargin(0.f))
              [
                  SNew(SImage).Image(&LoadingBrush)
              ]
            + SConstraintCanvas::Slot()
              .Anchors(FAnchors(0.30f, 0.82f, 0.70f, 0.88f))
              .Offset(FMargin(0.f))
              [
                  SNew(STextBlock)
                    .Text(FText::FromString(TEXT("LOADING...")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                    .Justification(ETextJustify::Center)
                    .ColorAndOpacity(FSlateColor(born2flap::ui::theme::FG()))
              ]
            + SConstraintCanvas::Slot()
              .Anchors(FAnchors(0.40f, 0.90f, 0.60f, 0.915f))
              .Offset(FMargin(0.f))
              [
                  SNew(SBorn2FlapLoadingBar).Height(3.f)
              ];
        MoviePlayer->SetupLoadingScreen(Attr);
        MoviePlayer->PlayMovie();
    }

    void OnPostLoadMap(UWorld*)
    {
        // Prime texture streaming before the viewport is revealed. The level has
        // finished loading and the actors (bird included) have begun play, so
        // every streaming surface — the 4k world textures especially — can now
        // be pulled fully resident. StreamAllResources blocks (bounded) while the
        // movie player's slate thread keeps the loading screen animating, so the
        // scene does not appear black right after the screen drops.
        IStreamingManager::Get().StreamAllResources(2.5f);

        // Belt-and-braces: auto-complete normally stops the movie, but make sure
        // the viewport is never left covered if a load path skips the callback.
        IGameMoviePlayer* MoviePlayer = GetMoviePlayer();
        if (MoviePlayer && MoviePlayer->IsMovieCurrentlyPlaying())
            MoviePlayer->StopMovie();
    }

    TStrongObjectPtr<UTexture2D> LoadingTexture;
    FSlateBrush LoadingBrush;
    static FBorn2FlapModule* GModule;
};

FBorn2FlapModule* FBorn2FlapModule::GModule = nullptr;

void Born2FlapShowLoadingScreen()
{
    FBorn2FlapModule::PlayLoadingScreen();
}

IMPLEMENT_PRIMARY_GAME_MODULE(FBorn2FlapModule, Born2Flap, "Born2Flap");