// Born2FlapFlightHUD.cpp — the glass cockpit implementation.
//
// Builds the semantic component tree as a single create_instance op (nested
// FNode), then re-drives the readouts each tick with update_props ops. See the
// header for the architecture note.

#include "UI/Born2FlapFlightHUD.h"

#include "UI/Born2FlapUIRenderer.h"
#include "UI/Born2FlapI18n.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Game/Born2FlapGameMode.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"

using namespace born2flap::ui;

// ---- compact wire-model builders ------------------------------------------
namespace {

FValue S(const char* v) { FValue x; x.kind = FValue::Kind::String; x.str = v; return x; }
FValue N(double v) { FValue x; x.kind = FValue::Kind::Number; x.num = v; return x; }
FValue B(bool v) { FValue x; x.kind = FValue::Kind::Bool; x.b = v; return x; }
FValue A(std::initializer_list<double> Vals) { FValue x; x.kind = FValue::Kind::Array; for (double v : Vals) { FValue e; e.kind = FValue::Kind::Number; e.num = v; x.arr.push_back(e); } return x; }

// A localized string value: look up a semantic key and emit it as UTF-8.
FValue L(const char* Key) { return S(TCHAR_TO_UTF8(*Born2Flap::I18n::T(Key))); }

FProps P(std::initializer_list<std::pair<const char*, FValue>> Items)
{
    FProps M;
    for (const auto& KV : Items) M[KV.first] = KV.second;
    return M;
}

FNode Nd(const char* Type, FProps Props = {}, std::vector<FNode> Children = {})
{
    FNode N_;
    N_.type = Type;
    N_.props = std::move(Props);
    N_.children = std::move(Children);
    return N_;
}

FOp UpdateProps(FPath Path, FProps Props)
{
    FOp O;
    O.op = EOp::UpdateProps;
    O.path = std::move(Path);
    O.props = std::move(Props);
    return O;
}

FPath Pth(std::initializer_list<int> I) { FPath Path{0}; Path.insert(Path.end(),I.begin(),I.end()); return Path; }

// Wind classification for the cockpit: simple, intuitive Beaufort-like words.
FString WindWord(float SpeedMs)
{
    if (SpeedMs < 0.5f)  return Born2Flap::I18n::T("wind.calm");
    if (SpeedMs < 4.0f)  return Born2Flap::I18n::T("wind.breeze");
    if (SpeedMs < 8.0f)  return Born2Flap::I18n::T("wind.windy");
    if (SpeedMs < 11.0f) return Born2Flap::I18n::T("wind.strong");
    return Born2Flap::I18n::T("wind.storm");
}

const char* WindTone(float SpeedMs)
{
    if (SpeedMs < 0.5f)  return "good";
    if (SpeedMs < 4.0f)  return "info";
    if (SpeedMs < 8.0f)  return "normal";
    if (SpeedMs < 11.0f) return "warn";
    return "danger";
}

// The relative wind direction is conveyed by the HUD "WindArrow" custom widget:
// a small vector arrow that rotates in screen space (0° = ahead, clockwise
// positive), so the pilot reads the relative wind source at a glance. No ASCII
// glyphs (the ChakraPetch font cannot render arrow codepoints), no world actor.

}  // namespace

ABorn2FlapFlightHUD::ABorn2FlapFlightHUD()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ABorn2FlapFlightHUD::BeginPlay()
{
    Super::BeginPlay();
    Renderer = NewObject<UBorn2FlapUIRenderer>(this);
    Renderer->OnComponentAction.AddDynamic(this, &ABorn2FlapFlightHUD::HandleComponentAction);
    // The engine's DefaultVirtualJoysticks are added to the viewport at Z-order
    // 0. Keep the cockpit *below* that (negative) so the on-screen sticks always
    // render on top of the glass cockpit and stay touchable over the HUD.
    Renderer->ViewportZOrder = -10;
    BuildCockpit();

    // The action buttons float above the sticks (z-order 10 > 0) at the top-right
    // corner, clear of both on-screen joysticks (bottom corners) and of the
    // readout column (top-left). Under the modal menu (90) and POI overlay (12).
    ButtonRenderer = NewObject<UBorn2FlapUIRenderer>(this);
    ButtonRenderer->OnComponentAction.AddDynamic(this, &ABorn2FlapFlightHUD::HandleComponentAction);
    ButtonRenderer->ViewportZOrder = 10;
    BuildButtons();
}

// The semantic tree. Paths are child indices from the Overlay root ([]):
//   [0]      the width-constrained SizeBox
//   [0,0]    the outer VBox
//   [0,0,0]  brand banner
//   [0,0,1]  "INSTRUMENTE" panel  → [0,0,1,0] alt, [0,0,1,1] climb, [0,0,1,2] speed
//   [0,0,2]  "ENERGIE" panel       → [0,0,2,0] battery, [0,0,2,1] throttle
//   [0,0,3]  "WIND" panel          → [0,0,3,0] speed, [0,0,3,1] arrow, [0,0,3,2] desc
//   [0,0,4]  status banner · [0,0,5] camera banner · [0,0,6] hint banner
// The THROW / MENU / HIDE buttons are NOT part of this tree — they live in a
// separate top-right overlay (see BuildButtons) so they are never covered by
// the on-screen joysticks.
void ABorn2FlapFlightHUD::BuildCockpit()
{
    if (!Renderer)
        return;

    FNode Root = Nd("Overlay", P({ {"align", S("left")}, {"valign", S("top")} }),
    {
        Nd("SizeBox",P({{"width",N(350)}}),{
        Nd("VerticalBox", P({ {"spacing", N(8)} }),
        {
            Nd("SizeBox", P({ {"width", N(300)}, {"height", N(92)} }), {
                Nd("Image", P({ {"texture", S("/Game/Splash/born2flap-logo.born2flap-logo")} }))
            }),

            Nd("Panel", P({ {"title", L("hud.instruments")}, {"spacing", N(2)} }),
            {
                Nd("Stat", P({ {"label", L("hud.altitude")}, {"value", N(0)}, {"unit", S("m")},   {"format", S("int")}, {"tone", S("good")} })),
                Nd("Stat", P({ {"label", L("hud.climb")},    {"value", N(0)}, {"unit", S("m/s")}, {"format", S("f1")}, {"signed", B(true)}, {"tone", S("normal")} })),
                Nd("Stat", P({ {"label", L("hud.speed")},    {"value", N(0)}, {"unit", S("m/s")}, {"format", S("f1")}, {"tone", S("info")} })),
            }),

            Nd("Panel", P({ {"title", L("hud.energy")}, {"spacing", N(2)} }),
            {
                Nd("Gauge", P({ {"label", L("hud.battery")}, {"value", N(100)}, {"min", N(0)}, {"max", N(100)}, {"unit", S("%")}, {"format", S("int")}, {"tone", S("good")} })),
                Nd("Gauge", P({ {"label", L("hud.throttle")}, {"value", N(0)},   {"min", N(0)}, {"max", N(100)}, {"unit", S("%")}, {"format", S("int")}, {"tone", S("accent")} })),
            }),

            Nd("Panel", P({ {"title", L("hud.wind")}, {"spacing", N(2)} }),
            {
                Nd("Stat", P({ {"label", L("hud.speed")},    {"value", N(0)}, {"unit", S("m/s")}, {"format", S("f1")}, {"tone", S("info")} })),
                Nd("WindArrow", P({ {"angle", N(0)}, {"size", N(30)} })),
                Nd("Banner", P({ {"text", L("wind.calm")}, {"tone", S("good")} })),
            }),

            Nd("Banner", P({ {"text", L("hud.ready")}, {"tone", S("normal")} })),
            Nd("Banner", P({ {"text", S("")}, {"tone", S("info")}, {"size", S("s")} })),
            Nd("Banner", P({ {"text", S("")}, {"tone", S("normal")}, {"size", S("s")} })),
        })}),
    });

    FOp Mount;
    Mount.op = EOp::CreateInstance;
    Mount.path = FPath();  // root
    Mount.node = std::move(Root);

    TArray<FOp> Ops;
    Ops.Add(MoveTemp(Mount));
    Renderer->ApplyOps(MoveTemp(Ops));
}

// Top-right floating action bar: THROW (hand-launch), MENU (main menu) and
// HIDE (collapse the readout column). Kept in its own overlay so it renders
// above the virtual joysticks and can never be covered by them.
void ABorn2FlapFlightHUD::BuildButtons()
{
    if (!ButtonRenderer)
        return;

    // "visible" also marks the full-bleed Overlay SelfHitTestInvisible so its
    // empty screen-spanning canvas slot never swallows touches meant for the
    // virtual joysticks below — only the buttons themselves stay hit-testable.
    // The top inset clears the notch (safe-area title-safe top inset).
    float TopInset = 16.f;
    if (FSlateApplication::IsInitialized())
    {
        FDisplayMetrics Metrics;
        FSlateApplication::Get().GetDisplayMetrics(Metrics);
        TopInset = FMath::Max(16.f, Metrics.TitleSafePaddingSize.Y + 8.f);
    }
    FNode Root = Nd("Overlay", P({ {"align", S("right")}, {"valign", S("top")}, {"margin", A({0.0, (double)TopInset, 12.0, 0.0})}, {"visible", N(1)} }),
    {
        Nd("HorizontalBox", P({ {"spacing", N(8)} }),
        {
            Nd("IconButton", P({ {"icon", S("glyph:launch")}, {"label", S("THROW")}, {"action", S("flight.throw")}, {"tone", S("good")} })),
            Nd("IconButton", P({ {"icon", S("glyph:pin")},    {"label", S("POI")},   {"action", S("ui.poi")},        {"tone", S("info")} })),
            Nd("IconButton", P({ {"icon", S("glyph:camera")}, {"label", S("CAM")},   {"action", S("flight.perspective")}, {"tone", S("info")} })),
            Nd("IconButton", P({ {"icon", S("glyph:menu")},   {"label", S("MENU")},  {"action", S("ui.menu")},       {"tone", S("ghost")} })),
            Nd("IconButton", P({ {"icon", S("glyph:eye")},    {"action", S("ui.hide")}, {"tone", S("ghost")} })),
        }),
    });

    FOp Mount;
    Mount.op = EOp::CreateInstance;
    Mount.path = FPath();  // root
    Mount.node = std::move(Root);

    TArray<FOp> Ops;
    Ops.Add(MoveTemp(Mount));
    ButtonRenderer->ApplyOps(MoveTemp(Ops));
}

void ABorn2FlapFlightHUD::HandleComponentAction(const FString& Action, float Value, const FString& Text)
{
    // Overlay touch buttons (THROW / MENU). These are the mobile affordances the
    // desktop keyboard shortcuts (space for hand-launch, F10 for menu) map onto.
    if (Action == TEXT("flight.throw"))
    {
        if (!CachedBird)
        {
            if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
                CachedBird = Cast<ABorn2FlapFlightPawn>(PC->GetPawn());
        }
        if (CachedBird)
            CachedBird->RequestHandLaunch();
    }
    else if (Action == TEXT("ui.menu"))
    {
        if (ABorn2FlapGameMode* GM = GetWorld() ? Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()) : nullptr)
            GM->ShowMainMenu(true);
    }
    else if (Action == TEXT("ui.poi"))
    {
        if (!CachedBird)
        {
            if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
                CachedBird = Cast<ABorn2FlapFlightPawn>(PC->GetPawn());
        }
        if (CachedBird)
        {
            if (CachedBird->IsPoiOverlayOpen()) CachedBird->ClosePoiOverlay();
            else CachedBird->OpenPoiOverlay();
        }
    }
    else if (Action == TEXT("flight.perspective"))
    {
        if (!CachedBird)
        {
            if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
                CachedBird = Cast<ABorn2FlapFlightPawn>(PC->GetPawn());
        }
        if (CachedBird)
            CachedBird->CycleCamera();
    }
    else if (Action == TEXT("ui.hide"))
    {
        // Collapse the whole readout column. The action bar lives in a separate
        // overlay, so HIDE always stays reachable to toggle it back open.
        bReadoutsHidden = !bReadoutsHidden;
        if (Renderer)
        {
            TArray<FOp> Ops;
            Ops.Add(UpdateProps(FPath(), P({ {"visible", N(bReadoutsHidden ? 0.0 : 1.0)} })));
            Renderer->ApplyOps(MoveTemp(Ops));
        }
    }
}

void ABorn2FlapFlightHUD::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Refresh();
}

void ABorn2FlapFlightHUD::Refresh()
{
    if (!Renderer)
        return;

    if (!CachedBird)
    {
        if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
            CachedBird = Cast<ABorn2FlapFlightPawn>(PC->GetPawn());
        if (!CachedBird)
            return;
    }

    // Blind flight is ear-only: collapse the whole cockpit (no banner — just
    // silence and sound). Settings panel owns the screen while open.
    const bool bCockpitHidden = bReadoutsHidden || CachedBird->IsBlindFlight() || CachedBird->IsFlightSettingsOpen() || CachedBird->IsUIHidden();
    if (bCockpitHidden != bLastHidden)
    {
        TArray<FOp> Ops;
        Ops.Add(UpdateProps(FPath(), P({ {"visible", N(bCockpitHidden ? 0.0 : 1.0)} })));
        Renderer->ApplyOps(MoveTemp(Ops));
        bLastHidden = bCockpitHidden;
    }
    if (bCockpitHidden)
        return;

    TArray<FOp> Ops;

    const float Alt = CachedBird->GetAltitude();
    const FString AltTxt = FString::Printf(TEXT("%.0f"), Alt);
    if (AltTxt != LastAlt)
    {
        Ops.Add(UpdateProps(Pth({0, 1, 0}), P({ {"value", N(Alt)}, {"format", S("int")} })));
        LastAlt = AltTxt;
    }

    const float Climb = CachedBird->GetClimbRate();
    const FString ClimbTxt = FString::Printf(TEXT("%+.1f"), Climb);
    if (ClimbTxt != LastClimb)
    {
        Ops.Add(UpdateProps(Pth({0, 1, 1}), P({ {"value", N(Climb)}, {"signed", B(true)},
            {"tone", S(Climb > 0.4f ? "good" : Climb < -0.4f ? "warn" : "normal")} })));
        LastClimb = ClimbTxt;
    }

    const float Speed = CachedBird->GetSpeed();
    const FString SpeedTxt = FString::Printf(TEXT("%.1f"), Speed);
    if (SpeedTxt != LastSpeed)
    {
        Ops.Add(UpdateProps(Pth({0, 1, 2}), P({ {"value", N(Speed)} })));
        LastSpeed = SpeedTxt;
    }

    // Battery: the bar stays a 0..100 % gauge, the readout shows the LiPo
    // voltages — per cell first, then the pack total (cell count derived from
    // the tuned nominal pack voltage, e.g. 11.1 V → 3S; 3.0 V empty → 4.2 V
    // full per cell).
    const float Soc = CachedBird->GetBattery();
    const float NominalV = CachedBird->GetTuning(ETuningField::BatteryVoltage);
    const int32 Cells = FMath::Max(1, FMath::RoundToInt(NominalV / 3.7f));
    const float CellV = 3.0f + Soc * 1.2f;
    const FString BattTxt = FString::Printf(TEXT("%.2fV × %d = %.1fV"), CellV, Cells, CellV * Cells);
    const FString BattKey = BattTxt + FString::Printf(TEXT("#%.0f"), Soc * 100.f);
    if (BattKey != LastBattery)
    {
        Ops.Add(UpdateProps(Pth({0, 2, 0}), P({ {"value", N(Soc * 100.f)},
            {"readout", S(TCHAR_TO_UTF8(*BattTxt))},
            {"tone", S(Soc > 0.5f ? "good" : Soc > 0.2f ? "warn" : "danger")} })));
        LastBattery = BattKey;
    }

    const float Throttle = CachedBird->GetEffort() * 100.f;
    const FString ThrottleTxt = FString::Printf(TEXT("%.0f"), Throttle);
    if (ThrottleTxt != LastThrottle)
    {
        Ops.Add(UpdateProps(Pth({0, 2, 1}), P({ {"value", N(Throttle)}, {"format", S("int")} })));
        LastThrottle = ThrottleTxt;
    }

    const FVector Wind = CachedBird->GetWind();
    const float WindSpeed = FVector2D(Wind.X, Wind.Y).Size();
    // Bearing the wind blows toward; the source (where it comes FROM) is opposite.
    const float BlowBearing = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(Wind.X, Wind.Y)) + 360.0f, 360.0f);
    const float SourceBearing = FMath::Fmod(BlowBearing + 180.0f, 360.0f);

    const FString WindTxt = FString::Printf(TEXT("%.1f"), WindSpeed);
    if (WindTxt != LastWindSpeed)
    {
        Ops.Add(UpdateProps(Pth({0, 3, 0}), P({ {"value", N(WindSpeed)} })));
        LastWindSpeed = WindTxt;
    }

    // Relative wind-source bearing: rotate the HUD arrow against the current
    // camera view (0° = ahead on screen, clockwise positive = to the right) so
    // the source direction reads directly no matter where the player is looking.
    // The arrow lives in screen space, so its reference is the view yaw, not the
    // bird's nose. RelAngle = viewBearing - sourceBearing (the source's bearing
    // measured clockwise from the view direction).
    float ViewYaw = 0.f;
    if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
    {
        if (APlayerCameraManager* Cam = PC->PlayerCameraManager)
        {
            ViewYaw = Cam->GetCameraRotation().Yaw;
        }
    }
    const float ViewBearing = FMath::Fmod(90.0f - ViewYaw + 360.0f, 360.0f);
    const float RelAngle = FMath::Fmod(ViewBearing - SourceBearing + 360.0f, 360.0f);
    if (!FMath::IsNearlyEqual(RelAngle, LastWindRelAngle, 0.5f))
    {
        Ops.Add(UpdateProps(Pth({0, 3, 1}), P({ {"angle", N(RelAngle)}, {"tone", S(WindTone(WindSpeed))} })));
        LastWindRelAngle = RelAngle;
    }

    const FString WindDesc = WindWord(WindSpeed);
    if (WindDesc != LastWindDesc)
    {
        Ops.Add(UpdateProps(Pth({0, 3, 2}), P({ {"text", S(TCHAR_TO_UTF8(*WindDesc))}, {"tone", S(WindTone(WindSpeed))} })));
        LastWindDesc = WindDesc;
    }

    const FString Status = CachedBird->GetFlightStatus();
    if (Status != LastStatus)
    {
        Ops.Add(UpdateProps(Pth({0, 4}), P({ {"text", S(TCHAR_TO_UTF8(*Status))} })));
        LastStatus = Status;
    }

    const FString Camera = CachedBird->GetCameraLabel();
    if (Camera != LastCamera)
    {
        Ops.Add(UpdateProps(Pth({0, 5}), P({ {"text", S(TCHAR_TO_UTF8(*Camera))} })));
        LastCamera = Camera;
    }

    if (Ops.Num() > 0)
        Renderer->ApplyOps(MoveTemp(Ops));
}