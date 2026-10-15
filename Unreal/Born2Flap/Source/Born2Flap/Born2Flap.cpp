#include "Born2Flap.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/Text/STextBlock.h"
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
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "UI/Born2FlapUiTheme.h"
#include "UI/Born2FlapLoading.h"

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

// The loading panel: a status line + a progress bar. It reads the shared
// Born2Flap::Loading state on every OnPaint, so it keeps advancing while the
// movie player's slate thread repaints the loading screen (active timers never
// run there). When progress is known (>= 0) the bar fills determinately,
// easing from 0 toward the reported target and creeping forward while the
// game thread is blocked in a synchronous build — so it never snaps or freezes;
// otherwise a 25%-wide segment sweeps left→right, driven by the monotonic
// cycle counter so it never freezes — and never loses precision to a float
// wall-clock cast, which is why the old marquee looked stuck.
class SBorn2FlapLoadingPanel : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SBorn2FlapLoadingPanel)
        : _FillColor(born2flap::ui::theme::ACCENT())
        , _TrackColor(FLinearColor(1.f, 1.f, 1.f, 0.14f))
        , _TextColor(born2flap::ui::theme::FG())
        , _BarHeight(4.f)
        , _FontSize(14)
    {}
        SLATE_ARGUMENT(FLinearColor, FillColor)
        SLATE_ARGUMENT(FLinearColor, TrackColor)
        SLATE_ARGUMENT(FLinearColor, TextColor)
        SLATE_ARGUMENT(float, BarHeight)
        SLATE_ARGUMENT(int32, FontSize)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        FillColor = InArgs._FillColor;
        TrackColor = InArgs._TrackColor;
        TextColor = InArgs._TextColor;
        BarHeight = InArgs._BarHeight;
        Font = FCoreStyle::GetDefaultFontStyle("Regular", InArgs._FontSize);
        SweepStartCycles = FPlatformTime::Cycles64();
        if (FSlateApplication::IsInitialized())
        {
            if (FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
                FontMeasure = Renderer->GetFontMeasureService();
        }
    }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
    {
        const FVector2D Size = AllottedGeometry.GetLocalSize();
        const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");

        // Current load state (game thread writes, this thread reads).
        float Progress = -1.f;
        FString Status = TEXT("LOADING");
        Born2Flap::Loading::State().Get(Progress, Status);

        const float LineHeight = FMath::Max(16.f, Font.Size * 1.6f);
        const float Gap = 6.f;

        // Track, anchored to the bottom of the panel.
        const float H = FMath::Max(2.f, FMath::Min(BarHeight, Size.Y - LineHeight - Gap));
        const float Y = Size.Y - H;

        // Status text, sitting above the track.
        if (FontMeasure.IsValid())
        {
            const FVector2f TextSize = FontMeasure->Measure(FStringView(Status), Font);
            const float TextX = FMath::Max(0.f, (Size.X - TextSize.X) * 0.5f);
            const float TextY = FMath::Max(Gap, Y - LineHeight - Gap);
            FSlateDrawElement::MakeText(
                OutDrawElements, LayerId,
                AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(FVector2f(TextX, TextY))),
                Status, Font, ESlateDrawEffect::None, TextColor);
        }
        FSlateDrawElement::MakeBox(
            OutDrawElements, LayerId + 1,
            AllottedGeometry.ToPaintGeometry(FVector2f(Size.X, H), FSlateLayoutTransform(FVector2f(0.f, Y))),
            Brush, ESlateDrawEffect::None, TrackColor);

        if (Progress >= 0.f)
        {
            // Animated determinate fill. The game thread reports monotonic
            // targets while the slate thread paints (often several times per
            // second), so keep a displayed value that:
            //  1. starts at 0 (never snaps to the first target), and
            //  2. eases toward the target when a new phase is reported, and
            //  3. creeps asymptotically toward a ceiling just under 100% when
            //     the game thread is blocked inside a long synchronous build
            //     (terrain / valley / streaming) with no new report — so the
            //     bar never freezes mid-phase and never claims "done" early.
            const uint64 NowCycles = FPlatformTime::Cycles64();
            const float Dt = (LastPaintCycles == 0)
                ? 0.f
                : (float)FPlatformTime::ToSeconds64(NowCycles - LastPaintCycles);
            LastPaintCycles = NowCycles;

            if (DisplayedProgress < 0.f)
                DisplayedProgress = 0.f;

            const float Target = FMath::Clamp(Progress, 0.f, 1.f);
            const float RiseRate = 0.8f;   // fraction/sec while catching up
            const float CreepRate = 0.12f; // headroom fraction/sec while blocked
            const float Ceiling = 0.96f;   // never show "done" until READY reports

            if (DisplayedProgress < Target)
                DisplayedProgress = FMath::Min(Target, DisplayedProgress + RiseRate * Dt);
            else
                DisplayedProgress += (Ceiling - DisplayedProgress) * CreepRate * Dt;

            const float FillW = Size.X * DisplayedProgress;
            FSlateDrawElement::MakeBox(
                OutDrawElements, LayerId + 2,
                AllottedGeometry.ToPaintGeometry(FVector2f(FillW, H), FSlateLayoutTransform(FVector2f(0.f, Y))),
                Brush, ESlateDrawEffect::None, FillColor);
        }
        else
        {
            // Indeterminate sweep (25% wide), clipped to the track.
            const double Elapsed = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - SweepStartCycles);
            const float T = FMath::Fractional((float)Elapsed);
            const float SegW = Size.X * 0.25f;
            const float X = (T * (Size.X + SegW)) - SegW;
            const FGeometry BarGeometry = AllottedGeometry.MakeChild(
                FVector2D(Size.X, H), FSlateLayoutTransform(FVector2f(0.f, Y)));
            OutDrawElements.PushClip(FSlateClippingZone(BarGeometry));
            FSlateDrawElement::MakeBox(
                OutDrawElements, LayerId + 2,
                AllottedGeometry.ToPaintGeometry(FVector2f(SegW, H), FSlateLayoutTransform(FVector2f(X, Y))),
                Brush, ESlateDrawEffect::None, FillColor);
            OutDrawElements.PopClip();
        }

        return LayerId + 3;
    }

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(320.f, BarHeight + 26.f);
    }

private:
    FLinearColor FillColor;
    FLinearColor TrackColor;
    FLinearColor TextColor;
    FSlateFontInfo Font;
    TSharedPtr<FSlateFontMeasure> FontMeasure;
    float BarHeight = 4.f;
    uint64 SweepStartCycles = 0;

    // Animated determinate fill state. OnPaint is const, but these are purely
    // cosmetic paint-thread state (single slate render thread), safe to mutate.
    mutable float DisplayedProgress = -1.f;
    mutable uint64 LastPaintCycles = 0;
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
        FString ViewPath=FPaths::ProjectContentDir()/TEXT("UI/LoadingView.json");
        // Authoring happens in Ruby before the blocking load. The movie thread
        // consumes its immutable tree and reads only atomic progress state.
        FString Script=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../../Brain/bin/export_loading"));
        FString Ruby=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../../ThirdParty/ruby/bin/ruby.exe"));
        if(!FPaths::FileExists(Script)) Script=FPaths::ProjectDir()/TEXT("Brain/bin/export_loading");
        if(!FPaths::FileExists(Ruby)) Ruby=FPaths::ProjectDir()/TEXT("ThirdParty/ruby/bin/ruby.exe");
        if(FPaths::FileExists(Script) && FPaths::FileExists(Ruby)) {
            const FString Generated=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("LoadingView.json"));
            int32 Code=-1; FString Errors;
            FPlatformProcess::ExecProcess(*Ruby,*FString::Printf(TEXT("\"%s\" \"%s\""),*Script,*Generated),&Code,nullptr,&Errors);
            if(Code==0) ViewPath=Generated;
            else UE_LOG(LogTemp,Warning,TEXT("Ruby loading view export failed: %s"),*Errors);
        }
        FString Json; TSharedPtr<FJsonObject> View;
        if(!FFileHelper::LoadFileToString(Json,*ViewPath) ||
           !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),View)) {
            UE_LOG(LogTemp,Error,TEXT("Loading UMGHAML view missing or invalid: %s"),*ViewPath); return;
        }
        LoadingAssets.Empty(); LoadingBrushes.Empty();
        FLoadingScreenAttributes Attr;
        Attr.bAutoCompleteWhenLoadingCompletes = true;
        Attr.bWaitForManualStop = false;
        Attr.bAllowEngineTick = false;
        Attr.MinimumLoadingScreenDisplayTime = 3.0f;
        // Full-bleed artwork + a status line + a progress bar. The panel reads
        // the shared Born2Flap::Loading state every paint, so it shows the
        // assembly phases (reported from BeginPlay) and advances the bar even on
        // the movie player's slate thread, which never runs Slate active timers.
        // Reset the progress state for this load before the movie starts.
        Born2Flap::Loading::State().Begin(TEXT("LOADING WORLD"));
        Attr.WidgetLoadingScreen = BuildLoadingNode(View.ToSharedRef());
        UE_LOG(LogTemp,Display,TEXT("UMGHAML loading view compiled and mounted: %s"),*ViewPath);
        MoviePlayer->SetupLoadingScreen(Attr);
        MoviePlayer->PlayMovie();
    }

    void OnPostLoadMap(UWorld*)
    {
        // The level package has loaded and the actors (bird included) have begun
        // play, so the whole world is assembled. Now prime every streaming
        // surface — the 4k world textures especially — fully resident before the
        // viewport is revealed. StreamAllResources blocks while the movie
        // player's slate thread keeps the loading screen animating, so the scene
        // no longer appears black (or stutters) right after the screen drops.
        // Loop until nothing is left wanting, with a hard cap so a stuck stream
        // can never leave the loader hanging forever.
        Born2Flap::Loading::State().Report(0.90f, TEXT("STREAMING WORLD"));
        // Prime every streaming surface fully resident before the viewport is
        // revealed — the 4k world textures, Nanite meshes and audio banks in
        // particular. StreamAllResources blocks (with the movie player's slate
        // thread still animating), so loop until nothing is left wanting. The
        // hard cap only guards against a genuinely stuck stream; it is long
        // enough that any real level finishes well inside it.
        IStreamingManager& Streaming = IStreamingManager::Get();
        const double StreamStart = FPlatformTime::Seconds();
        const double StreamCap = 60.0;
        int32 Pending = 0;
        int32 Wanting = 0;
        do
        {
            Pending = Streaming.StreamAllResources(1.0f);
            Wanting = Streaming.GetNumWantingResources();
        }
        while ((Pending > 0 || Wanting > 0) && (FPlatformTime::Seconds() - StreamStart) < StreamCap);
        // Final flush: force every outstanding streaming request (geometry LODs
        // + texture mips) to finish before the curtain drops, so the revealed
        // scene never pops in black or low-res after the loader disappears.
        Streaming.BlockTillAllRequestsFinished(10.0f, false);

        Born2Flap::Loading::State().Report(1.f, TEXT("READY"));
        // Hold the READY frame briefly so the bar visibly eases from ~90% to a
        // full 100% before the viewport is revealed — otherwise the last catch-up
        // is skipped and the loader appears to end early. The movie player's slate
        // thread keeps painting while the game thread pauses, so the fill animates.
        const double ReadyHoldStart = FPlatformTime::Seconds();
        while ((FPlatformTime::Seconds() - ReadyHoldStart) < 0.5)
        {
            FPlatformProcess::Sleep(0.016f);
        }
        Born2Flap::Loading::State().Finish();

        // Belt-and-braces: auto-complete normally stops the movie, but make sure
        // the viewport is never left covered if a load path skips the callback.
        IGameMoviePlayer* MoviePlayer = GetMoviePlayer();
        if (MoviePlayer && MoviePlayer->IsMovieCurrentlyPlaying())
            MoviePlayer->StopMovie();
    }

    // Thread-safe host for the loading-view subset of the shared wire schema.
    // No screen composition is authored here: anchors/assets/style come from UMGHAML/UMGSS.
    TArray<TStrongObjectPtr<UTexture2D>> LoadingAssets;
    TArray<TSharedPtr<FSlateBrush>> LoadingBrushes;
    TSharedRef<SWidget> BuildLoadingNode(const TSharedRef<FJsonObject>& Node)
    {
        const FString Type=Node->GetStringField(TEXT("type"));
        const auto Props=Node->GetObjectField(TEXT("props"));
        if(Type==TEXT("Overlay")) {
            auto Canvas=SNew(SConstraintCanvas);
            for(const auto& Value:Node->GetArrayField(TEXT("children"))) {
                const auto Child=Value->AsObject(); const auto P=Child->GetObjectField(TEXT("props"));
                FAnchors Anchors(0,0,1,1);
                const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
                if(P->TryGetArrayField(TEXT("anchors"),A) && A->Num()==4)
                    Anchors=FAnchors((*A)[0]->AsNumber(),(*A)[1]->AsNumber(),(*A)[2]->AsNumber(),(*A)[3]->AsNumber());
                Canvas->AddSlot().Anchors(Anchors).Offset(FMargin(0))[BuildLoadingNode(Child.ToSharedRef())];
            }
            return Canvas;
        }
        if(Type==TEXT("Image")) {
            const FString Asset=Props->GetStringField(TEXT("texture"));
            auto* Texture=LoadObject<UTexture2D>(nullptr,*Asset);
            auto Brush=MakeShared<FSlateBrush>();
            Brush->SetResourceObject(Texture); Brush->DrawAs=ESlateBrushDrawType::Image;
            if(Texture) { Brush->ImageSize=FVector2D(Texture->GetSizeX(),Texture->GetSizeY()); LoadingAssets.Emplace(Texture); }
            LoadingBrushes.Add(Brush);
            return SNew(SImage).Image(&Brush.Get());
        }
        if(Type==TEXT("LoadingProgress")) {
            double Height=4,Font=14;
            Props->TryGetNumberField(TEXT("bar_height"),Height); Props->TryGetNumberField(TEXT("font_size"),Font);
            return SNew(SBorn2FlapLoadingPanel).BarHeight(Height).FontSize(Font);
        }
        FString Text; Props->TryGetStringField(TEXT("text"),Text);
        return SNew(STextBlock).Text(FText::FromString(Text));
    }

    static FBorn2FlapModule* GModule;
};

FBorn2FlapModule* FBorn2FlapModule::GModule = nullptr;

void Born2FlapShowLoadingScreen()
{
    FBorn2FlapModule::PlayLoadingScreen();
}

IMPLEMENT_PRIMARY_GAME_MODULE(FBorn2FlapModule, Born2Flap, "Born2Flap");