// Born2FlapFlightHUD.cpp — the glass cockpit implementation.
//
// Builds the semantic component tree as a single create_instance op (nested
// FNode), then re-drives the readouts each tick with update_props ops. See the
// header for the architecture note.

#include "UI/Born2FlapFlightHUD.h"

#include "UI/Born2FlapUIRenderer.h"
#include "UI/Born2FlapI18n.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

using namespace born2flap::ui;

// ---- compact wire-model builders ------------------------------------------
namespace {

FValue S(const char* v) { FValue x; x.kind = FValue::Kind::String; x.str = v; return x; }
FValue N(double v) { FValue x; x.kind = FValue::Kind::Number; x.num = v; return x; }

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

FPath Pth(std::initializer_list<int> I) { return FPath(I); }

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

FString Cardinal(float BearingDeg)
{
    static const char* Keys[] = { "dir.n", "dir.ne", "dir.e", "dir.se", "dir.s", "dir.sw", "dir.w", "dir.nw" };
    int32 I = FMath::FloorToInt(FMath::Fmod(BearingDeg + 22.5f, 360.0f) / 45.0f) % 8;
    return Born2Flap::I18n::T(Keys[I]);
}

}  // namespace

ABorn2FlapFlightHUD::ABorn2FlapFlightHUD()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ABorn2FlapFlightHUD::BeginPlay()
{
    Super::BeginPlay();
    Renderer = NewObject<UBorn2FlapUIRenderer>(this);
    BuildCockpit();
}

// The semantic tree. Paths are child indices from the Overlay root ([]):
//   [0]      the outer VBox
//   [0,0]    brand banner
//   [0,1]    "INSTRUMENTE" panel  → [0,1,0] alt, [0,1,1] climb, [0,1,2] speed
//   [0,2]    "ENERGIE" panel       → [0,2,0] battery, [0,2,1] throttle
//   [0,3]    "WIND" panel          → [0,3,0] speed, [0,3,1] direction, [0,3,2] desc
//   [0,4]    status banner
void ABorn2FlapFlightHUD::BuildCockpit()
{
    if (!Renderer)
        return;

    FNode Root = Nd("Overlay", P({ {"align", S("left")}, {"valign", S("top")} }),
    {
        Nd("VerticalBox", P({ {"spacing", N(6)} }),
        {
            Nd("Banner", P({ {"text", L("brand")}, {"tone", S("accent")} })),

            Nd("Panel", P({ {"title", L("hud.instruments")}, {"spacing", N(2)} }),
            {
                Nd("Stat", P({ {"label", L("hud.altitude")}, {"value", N(0)}, {"unit", S(" m")},   {"tone", S("good")} })),
                Nd("Stat", P({ {"label", L("hud.climb")},    {"value", N(0)}, {"unit", S(" m/s")} })),
                Nd("Stat", P({ {"label", L("hud.speed")},    {"value", N(0)}, {"unit", S(" m/s")}, {"tone", S("info")} })),
            }),

            Nd("Panel", P({ {"title", L("hud.energy")}, {"spacing", N(2)} }),
            {
                Nd("Gauge", P({ {"label", L("hud.battery")}, {"value", N(100)}, {"min", N(0)}, {"max", N(100)}, {"unit", S("%")}, {"tone", S("good")} })),
                Nd("Gauge", P({ {"label", L("hud.throttle")}, {"value", N(0)},   {"min", N(0)}, {"max", N(1)},                     {"tone", S("accent")} })),
            }),

            Nd("Panel", P({ {"title", L("hud.wind")}, {"spacing", N(2)} }),
            {
                Nd("Stat", P({ {"label", L("hud.speed")},    {"value", N(0)}, {"unit", S(" m/s")}, {"tone", S("info")} })),
                Nd("Stat", P({ {"label", L("hud.direction")}, {"value", N(0)}, {"unit", S("°")} })),
                Nd("Banner", P({ {"text", L("wind.calm")}, {"tone", S("good")} })),
            }),

            Nd("Banner", P({ {"text", L("hud.ready")}, {"tone", S("normal")} })),
        }),
    });

    FOp Mount;
    Mount.op = EOp::CreateInstance;
    Mount.path = FPath();  // root
    Mount.node = std::move(Root);

    TArray<FOp> Ops;
    Ops.Add(MoveTemp(Mount));
    Renderer->ApplyOps(Ops);
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
    const bool bCockpitHidden = CachedBird->IsBlindFlight() || CachedBird->IsFlightSettingsOpen();
    if (bCockpitHidden != bLastHidden)
    {
        TArray<FOp> Ops;
        Ops.Add(UpdateProps(FPath(), P({ {"visible", N(bCockpitHidden ? 0.0 : 1.0)} })));
        Renderer->ApplyOps(Ops);
        bLastHidden = bCockpitHidden;
    }
    if (bCockpitHidden)
        return;

    TArray<FOp> Ops;

    const float Alt = CachedBird->GetAltitude();
    if (!FMath::IsNearlyEqual(Alt, LastAlt))
    {
        Ops.Add(UpdateProps(Pth({0, 1, 0}), P({ {"value", N(Alt)} })));
        LastAlt = Alt;
    }

    const float Climb = CachedBird->GetClimbRate();
    if (!FMath::IsNearlyEqual(Climb, LastClimb))
    {
        Ops.Add(UpdateProps(Pth({0, 1, 1}), P({ {"value", N(Climb)} })));
        LastClimb = Climb;
    }

    const float Speed = CachedBird->GetSpeed();
    if (!FMath::IsNearlyEqual(Speed, LastSpeed))
    {
        Ops.Add(UpdateProps(Pth({0, 1, 2}), P({ {"value", N(Speed)} })));
        LastSpeed = Speed;
    }

    const float Battery = CachedBird->GetBattery() * 100.f;  // 0..100 %
    if (!FMath::IsNearlyEqual(Battery, LastBattery))
    {
        Ops.Add(UpdateProps(Pth({0, 2, 0}), P({ {"value", N(Battery)} })));
        LastBattery = Battery;
    }

    const float Throttle = CachedBird->GetEffort();
    if (!FMath::IsNearlyEqual(Throttle, LastThrottle))
    {
        Ops.Add(UpdateProps(Pth({0, 2, 1}), P({ {"value", N(Throttle)} })));
        LastThrottle = Throttle;
    }

    const FVector Wind = CachedBird->GetWind();
    const float WindSpeed = FVector2D(Wind.X, Wind.Y).Size();
    const float WindBearing = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(Wind.X, Wind.Y)) + 360.0f, 360.0f);

    if (!FMath::IsNearlyEqual(WindSpeed, LastWindSpeed))
    {
        Ops.Add(UpdateProps(Pth({0, 3, 0}), P({ {"value", N(WindSpeed)} })));
        LastWindSpeed = WindSpeed;
    }
    if (!FMath::IsNearlyEqual(WindBearing, LastWindDir))
    {
        Ops.Add(UpdateProps(Pth({0, 3, 1}), P({ {"value", N(WindBearing)} })));
        LastWindDir = WindBearing;
    }

    const FString WindDesc = WindWord(WindSpeed) + FString(TEXT(" · ")) + Cardinal(WindBearing);
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

    if (Ops.Num() > 0)
        Renderer->ApplyOps(Ops);
}