// Born2FlapMenu.cpp — the semantic main-menu / level-selector implementation.

#include "UI/Born2FlapMenu.h"

#include "Born2Flap.h"
#include "UI/Born2FlapUIRenderer.h"
#include "UI/Born2FlapUiOps.h"
#include "UI/Born2FlapI18n.h"
#include "Game/Born2FlapGameMode.h"
#include "Game/Born2FlapPoi.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Flight/Born2FlapTuning.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"

using namespace born2flap::ui;

namespace {

FValue S(const char* v) { FValue x; x.kind = FValue::Kind::String; x.str = v; return x; }
FValue S(const FString& v) { FValue x; x.kind = FValue::Kind::String; x.str = TCHAR_TO_UTF8(*v); return x; }
FValue N(double v) { FValue x; x.kind = FValue::Kind::Number; x.num = v; return x; }
FValue B(bool v) { FValue x; x.kind = FValue::Kind::Bool; x.b = v; return x; }

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

FPath ToPath(const TArray<int32>& Indices)
{
    return FPath(Indices.GetData(), Indices.GetData() + Indices.Num());
}

void SendUpdate(UBorn2FlapUIRenderer* R, const TArray<int32>& Path, FProps Props)
{
    if (!R)
        return;
    TArray<FOp> Ops;
    FOp O;
    O.op = EOp::UpdateProps;
    O.path = ToPath(Path);
    O.props = std::move(Props);
    Ops.Add(O);
    R->ApplyOps(Ops);
}

// Servos tested for flapping (dantiel.github.io/OrniFlight servo-efficiency
// table). Speed = 60 / s-per-60°, torque = kg·cm × 0.0980665 → N·m. Backdrive
// is not published on the table, so presets keep the simulator default (20).
struct FServoPreset { const TCHAR* Name; float SpeedDegS; float StallTorqueNm; float Backdrive; float Voltage; };
static const FServoPreset ServoPresets[] = {
    { TEXT("Blue Arrow AF D43S-6.0-MG"), 1500.f, 0.17f, 20.f, 6.0f },
    { TEXT("VOTIK PTK 7465 / 7465W MG"),   667.f, 0.56f, 20.f, 8.4f },
    { TEXT("Blue Arrow D0576HT-MG-HV"),   1000.f, 0.41f, 20.f, 7.4f },
    { TEXT("Inservos D0474HT-MG-HV"),      750.f, 0.35f, 20.f, 7.4f },
    { TEXT("KST MR320"),                   750.f, 0.54f, 20.f, 7.4f },
    { TEXT("SAVOX SV-1270TG"),             545.f, 3.43f, 20.f, 7.4f },
};
constexpr int32 ServoPresetCount = (int32)(sizeof(ServoPresets) / sizeof(ServoPresets[0]));

// Open worlds: shown one at a time in the chooser. Each carries a short fictional
// vignette so the player can read about the place before flying there.
struct FWorldDef { const TCHAR* Id; const TCHAR* Title; const TCHAR* Story; };
const FWorldDef Worlds[] = {
    { TEXT("Ravenstonefield"), TEXT("RAVENSTONEFIELD"),
      TEXT("A highland valley of black basalt columns rising from amber bracken. Ravens ride the thermals between the spires, and the wind carries heather and cold stone. An old ornithopter field, flown by generations of wing-builders.") },
    { TEXT("Shiomori"), TEXT("SHIOMORI BAY"),
      TEXT("A broad pale beach arcing around a sheltered bay of dark, glassy water. Salt-cured rock, wind-bent grass and a low tide-walk of basalt stretch inland toward hazy, sunlit hills. Quiet enough that you notice what the sky is doing.") },
    { TEXT("Training"), TEXT("TRAINING"),
      TEXT("A flat grass course marked with floating sky-gates. The field where every pilot learns to fold the wing, hold the line and thread the gates against the clock.") },
};
constexpr int32 WorldCount = 3;

}  // namespace

ABorn2FlapMenu::ABorn2FlapMenu()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ABorn2FlapMenu::BeginPlay()
{
    Super::BeginPlay();
    Renderer = NewObject<UBorn2FlapUIRenderer>(this);
    Renderer->ViewportZOrder = 90; // under the splash (100), above the cockpit (0)
    Renderer->OnComponentAction.AddDynamic(this, &ABorn2FlapMenu::OnAction);
    for(const FString Level:{FString(TEXT("Ravenstonefield")),FString(TEXT("Shiomori")),FString(TEXT("Training"))})
        LevelConditions.Add(Level,Born2FlapWeather::Load(Level));
    Build();
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FWeatherMenuTest")))
    {
        Renderer->OnComponentAction.Broadcast(TEXT("menu.play"),1.f,TEXT(""));
        SelectedWorld=1;
        OnAction(TEXT("menu.page"),1,TEXT(""));
        Renderer->OnComponentAction.Broadcast(TEXT("weather.Shiomori"),2.f,TEXT(""));
        Renderer->OnComponentAction.Broadcast(TEXT("daytime.Shiomori"),3.f,TEXT(""));
        const auto C=LevelConditions.FindChecked(TEXT("Shiomori"));
        const bool Pass=C.Weather==TEXT("rain") && C.Time==TEXT("sunset") &&
            !Born2FlapWeather::WeatherOptions(TEXT("Training")).Contains(TEXT("snow"));
        UE_LOG(LogTemp,Display,TEXT("WeatherMenuTest %s: selection relays and per-level options"),Pass ? TEXT("PASS") : TEXT("FAIL"));
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[](float){
            FScreenshotRequest::RequestScreenshot(TEXT("WEATHER_MENU.png"),true,false);return false;
        }),2.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float){OnAction(TEXT("menu.page"),0,TEXT(""));return false;}),2.3f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[](float){FScreenshotRequest::RequestScreenshot(TEXT("GLASS_LOCATION.png"),true,false);return false;}),2.6f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float){OnAction(TEXT("menu.page"),2,TEXT(""));return false;}),3.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[](float){FScreenshotRequest::RequestScreenshot(TEXT("GLASS_ROUTE.png"),true,false);return false;}),3.4f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float){
            OnAction(TEXT("menu.shiomori"),1.f,TEXT(""));return false;
        }),4.f);
    }
}

void ABorn2FlapMenu::Build()
{
    if (!Renderer)
        return;

    // Full-bleed menu artwork shared by both pages: a persistent left navigation
    // rail plus a single content pane. OPEN WORLDS and (in-level) FLIGHT DESK are
    // two pages of the same full-screen view — no back-and-forth, no separate
    // floating settings window.
    //   []                 Overlay (root, children fill)
    //   [0]                Image (born2flap-background, full-bleed)
    //   [1]                HorizontalBox
    //   [1,0]              SizeBox(380) → Panel "FLIGHT MENU" (nav rail)
    //   [1,1]              SizeBox → Panel (the active page)
    if (bInLevel)
        Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0));

    FString PaneTitle = TEXT("OPEN WORLDS");
    std::vector<FNode> PaneChildren;
    FNode ContentPane;

    if (NavPage == 1)
    {
        PaneTitle = TEXT("FLIGHT DESK");
        if (bInLevel && Bird.IsValid())
        {
            // ---- FLIGHT DESK page: the integrated tuning panel ----
            BuildFlightDesk(PaneChildren);
            // Fixed size so the inner ScrollBox gets a bounded height and scrolls
            // instead of overflowing the card.
            ContentPane = Nd("SizeBox", P({ {"width", N(1040)}, {"height", N(860)} }),
            {
                Nd("Panel", P({ {"title", S(PaneTitle)}, {"bg", S("solid")}, {"padding", N(26)}, {"spacing", N(12)} }), std::move(PaneChildren))
            });
        }
        else
        {
            // Home screen: no live bird to tune yet — show the desk exists and
            // point the pilot at an open world.
            PaneChildren.push_back(Nd("TextBlock", P({ {"text", S("The FLIGHT DESK tunes the live ornithopter — servos, weight, balance, vertical mount angle and flight response.")}, {"size", S("m")}, {"wrap", B(true)} })));
            PaneChildren.push_back(Nd("Spacer", P({})));
            PaneChildren.push_back(Nd("TextBlock", P({ {"text", S("Enter a world, then press F10 and choose FLIGHT DESK to tune the bird in flight.")}, {"size", S("m")}, {"tone", S("accent")}, {"wrap", B(true)} })));
            ContentPane = Nd("SizeBox", P({ {"width", N(1040)}, {"height", N(860)} }),
            {
                Nd("Panel", P({ {"title", S(PaneTitle)}, {"bg", S("solid")}, {"padding", N(26)}, {"spacing", N(12)} }), std::move(PaneChildren))
            });
        }
    }
    else
    {
        // ---- OPEN WORLDS page: one open world at a time ----
        const FWorldDef& WD = Worlds[SelectedWorld];
        const FString Level = WD.Id;
        const FBorn2FlapConditions C = LevelConditions.FindChecked(Level);
        const auto Weathers = Born2FlapWeather::WeatherOptions(Level);
        const auto Times = Born2FlapWeather::TimeOptions(Level, C.Weather);
        FValue WeatherLabels, TimeLabels;
        WeatherLabels.kind = TimeLabels.kind = FValue::Kind::Array;
        for (const auto& Id : Weathers) WeatherLabels.arr.push_back(S(Born2FlapWeather::Profile(Id).Label));
        for (const auto& Id : Times) TimeLabels.arr.push_back(S(Born2FlapWeather::DayTime(Id).Label));
        const auto& W = Born2FlapWeather::Profile(C.Weather);
        const float Speed = Born2FlapWeather::Wind(Level, C, FVector::ZeroVector, 0).Size2D();

        // The selected world's name lives here, in context, as a heading — not
        // as the game's title (that role belongs to the sidebar logo).
        PaneChildren.push_back(Nd("TextBlock", P({ {"text", S(WD.Title)}, {"size", S("xl")}, {"tone", S("accent")} })));
        FValue Pages;Pages.kind=FValue::Kind::Array;for(const char* Page:{"LOCATION","WEATHER & TIME","ROUTE"}) Pages.arr.push_back(S(Page));
        // The three workbench pages are a single connected segmented control —
        // one selection at a time, equal-width cells on a shared track.
        PaneChildren.push_back(Nd("Segment",P({{"options",Pages},{"value",N(SelectedPage)},{"action",S("menu.page")},{"tooltip",S("Choose a location, set flight conditions, then select your starting point.")}})));
        const int32 LocationStart=PaneChildren.size();
        const FString Preview = Level == TEXT("Shiomori") ? TEXT("Coast") : Level == TEXT("Ravenstonefield") ? TEXT("Valley") : TEXT("Islands");
        PaneChildren.push_back(Nd("HorizontalBox", P({{"spacing",N(18)}}), {
            Nd("SizeBox",P({{"width",N(234)},{"height",N(128)}}),{
                Nd("Image",P({{"texture",S(TEXT("/Game/UI/Elements/")+Preview+TEXT(".")+Preview)}}))}),
            Nd("SizeBox",P({{"width",N(600)}}),{
                Nd("TextBlock", P({ {"text", S(WD.Story)}, {"size", S("m")}, {"wrap", B(true)} }))})
        }));
        PaneChildren.push_back(Nd("Spacer", P({})));
        const int32 WeatherStart=PaneChildren.size();
        PaneChildren.push_back(Nd("Select", P({ {"label", S("Weather")}, {"options", WeatherLabels}, {"value", N(Weathers.IndexOfByKey(C.Weather))}, {"action", S(TEXT("weather.") + Level)} })));
        PaneChildren.push_back(Nd("Select", P({ {"label", S("Time of day")}, {"options", TimeLabels}, {"value", N(Times.IndexOfByKey(C.Time))}, {"action", S(TEXT("daytime.") + Level)} })));
        PaneChildren.push_back(Nd("TextBlock", P({ {"text", S(FString::Printf(TEXT("Wind %.1f m/s | %s"), Speed, W.Gust < .5f ? TEXT("gentle gusts") : TEXT("variable breeze")))}, {"size", S("s")} })));
        // Travel destinations: a scrollable list of this world's points of
        // interest. Choosing one makes it the reset start point the FLY button
        // travels to. It replaces the old in-world beacon/cycling UI.
        const int32 RouteStart=PaneChildren.size();
        const TArray<FBorn2FlapPoi> PoiCat = Born2FlapPoi::Catalog(Level);
        const int32 PoiSel = SelectedPoi.FindRef(Level);
        std::vector<FNode> PoiButtons;
        for (int32 I = 0; I < PoiCat.Num(); ++I)
        {
            FString Name = Born2Flap::I18n::T(PoiCat[I].Key);
            if (PoiCat[I].Number > 0)
                Name += TEXT(" ") + FString::FromInt(PoiCat[I].Number);
            PoiButtons.push_back(Nd("Button", P({
                {"label", S(Name)},
                {"action", S(FString::Printf(TEXT("poi.%d"), I))},
                {"tone", S(I == PoiSel ? "good" : "accent")} })));
        }
        PaneChildren.push_back(Nd("TextBlock", P({ {"text", S("TRAVEL TO")}, {"size", S("xs")}, {"tone", S("dim")} })));
        PaneChildren.push_back(Nd("SizeBox", P({ {"height", N(150)} }),
        {
            Nd("ScrollBox", P({}),
            {
                Nd("VerticalBox", P({}), std::move(PoiButtons))
            })
        }));
        for(int32 I=LocationStart;I<(int32)PaneChildren.size();++I)
            PaneChildren[I].props["visible"]=B(SelectedPage==(I<WeatherStart ? 0 : I<RouteStart ? 1 : 2));
        PaneChildren.push_back(Nd("Button", P({ {"label", S(FString(TEXT("FLY  ")) + WD.Title)}, {"action", S(TEXT("menu.") + Level.ToLower())}, {"tone", S("good")} })));

        PaneChildren.push_back(Nd("Spacer", P({})));
        PaneChildren.push_back(Nd("TextBlock", P({ {"text", S(FString::Printf(TEXT("WORLD %d OF %d"), SelectedWorld + 1, WorldCount))}, {"size", S("xs")}, {"align", S("center")}, {"tone", S("dim")} })));
        PaneChildren.push_back(Nd("HorizontalBox", P({}),
        {
            Nd("Button", P({ {"label", S("PREV")}, {"action", S("menu.prevWorld")} })),
            Nd("Button", P({ {"label", S("NEXT")}, {"action", S("menu.nextWorld")} })),
        }));

        ContentPane = Nd("SizeBox", P({}), {
            Nd("Panel", P({ {"title", S(PaneTitle)}, {"bg", S("solid")}, {"padding", N(30)}, {"spacing", N(14)} }), std::move(PaneChildren))
        });
    }

    // ---- Left navigation rail (persistent across both pages) ----
    // Framed by the vertical-main-menu sprites: a vertical gold rail with an
    // ornate top fan and bottom taper, plus horizontal dividers between the
    // text-only buttons. Buttons rest transparent (ghost tone) and overlap the
    // rail a little; only on hover does the sprite's gold fill in behind them.
    auto Divider = []() {
        return Nd("SizeBox", P({ {"width", N(368)}, {"height", N(20)} }),
        {
            Nd("Image", P({ {"texture", S("/Game/UI/Elements/B2FMenuDivider.B2FMenuDivider")} }))
        });
    };
    auto Ghost = [](const FString& Label, const char* Action, bool Active) {
        return Nd("Button", P({
            {"label", S(Label)},
            {"action", S(Action)},
            {"tone", S(Active ? "ghost-active" : "ghost")} }));
    };

    std::vector<FNode> Sidebar;
    // Brand logo (imported from ~/Downloads/born2flap-logo.png). The game title
    // is the artwork, not a text banner — previously the i18n "menu.title"
    // string leaked a world name ("SHIOMORI BAY") into the title slot.
    Sidebar.push_back(Nd("SizeBox", P({ {"width", N(300)}, {"height", N(92)} }), {
        Nd("Image", P({ {"texture", S("/Game/Splash/born2flap-logo.born2flap-logo")} }))
    }));
    Sidebar.push_back(Nd("TextBlock", P({ {"text", S(Born2Flap::I18n::T("menu.subtitle"))}, {"size", S("s")}, {"wrap", B(true)}, {"tone", S("dim")} })));
    Sidebar.push_back(Nd("Spacer", P({})));
    Sidebar.push_back(Divider());
    Sidebar.push_back(Ghost(TEXT("OPEN WORLDS"), "menu.play", NavPage == 0));
    Sidebar.push_back(Divider());
    Sidebar.push_back(Ghost(TEXT("FLIGHT DESK"), "menu.settings", NavPage == 1));
    if (bInLevel)
    {
        Sidebar.push_back(Divider());
        Sidebar.push_back(Ghost(Born2Flap::I18n::T("menu.resume"), "menu.resume", false));
    }
    Sidebar.push_back(Divider());
    Sidebar.push_back(Ghost(Born2Flap::I18n::T("menu.quit"), "menu.quit", false));
    Sidebar.push_back(Nd("Spacer", P({})));

    // A fixed panel (NOT stretched to the full screen height): the rail spans
    // only this panel, the ornate fan sits at its top and the taper at its bottom.
    FNode SidebarPane = Nd("SizeBox", P({ {"width", N(400)}, {"height", N(520)} }),
    {
        Nd("Overlay", P({ {"align", S("left")} }),
        {
            Nd("SizeBox", P({ {"width", N(62)}, {"height", N(520)} }), {
                Nd("Image", P({ {"texture", S("/Game/UI/Elements/B2FMenuLeft.B2FMenuLeft")} }))
            }),
            Nd("VerticalBox", P({}), {
                Nd("SizeBox", P({ {"width", N(48)}, {"height", N(48)} }), {
                    Nd("Image", P({ {"texture", S("/Game/UI/Elements/B2FMenuTop.B2FMenuTop")} }))
                }),
                Nd("HorizontalBox", P({}), {
                    Nd("SizeBox", P({ {"width", N(30)} }), {}),
                    Nd("VerticalBox", P({}), std::move(Sidebar))
                }),
                Nd("Spacer", P({})),
                Nd("SizeBox", P({ {"width", N(70)}, {"height", N(80)} }), {
                    Nd("Image", P({ {"texture", S("/Game/UI/Elements/B2FMenuBottom.B2FMenuBottom")} }))
                })
            })
        })
    });

    FNode Root = Nd("Overlay", P({}),
    {
        Nd("Image", P({ {"texture", S("/Game/Splash/born2flap-background.born2flap-background")} })),
        Nd("HorizontalBox", P({ {"spacing", N(24)} }),
        {
            std::move(SidebarPane),
            std::move(ContentPane)
        })
    });

    TArray<FOp> Ops;
    FOp Op;
    Op.op = EOp::CreateInstance;
    Op.path = FPath();
    Op.node = std::move(Root);
    Ops.Add(Op);
    Renderer->ApplyOps(Ops);
}

void ABorn2FlapMenu::BuildFlightDesk(std::vector<FNode>& Out)
{
    // The hosting Panel sits at root path {1,1,0}; its children are [0]=tabs
    // Select, [1]=ScrollBox, [2]=footer. Rows hang off the VBox at {1,1,0,1,0}.
    static const TArray<int32> VBox{ 1, 1, 0, 1, 0 };
    auto RowPath = [&](int32 Row) { TArray<int32> P = VBox; P.Add(Row); return P; };

    const int32 CurModel = Bird->GetBirdModel();
    static const TCHAR* ModelNames[] =
    {
        TEXT("RAVENCROW  /  folded obsidian & comb pinions"),
        TEXT("PROTOTYPE  /  elliptical feathers"),
        TEXT("COMMON KESTREL  /  falcon"),
    };
    static const TCHAR* MouseNames[] =
    {
        TEXT("ROLL  /  mouse X"),
        TEXT("PITCH  /  mouse Y"),
        TEXT("YAW  /  mouse X"),
    };

    ModelButtons.Reset();
    MouseGainSliders.Reset();

    std::vector<FNode> Rows;

    FValue Pages;Pages.kind=FValue::Kind::Array;for(const char* Page:{"CRAFT","BIRD","CONTROLS","ASSIST","TUNING"}) Pages.arr.push_back(S(Page));
    Rows.push_back(Nd("Select",P({{"options",Pages},{"value",N(SettingsPage)},{"action",S("settings.page")},{"tooltip",S("Choose your aircraft, tune the bird, adjust controls, or manage assists.")}})));
    // Header.
    Rows.push_back(Nd("TextBlock", P({ {"text", S(FString(TEXT("Flight desk  •  choose your silhouette")))}, {"tone", S("dim")}, {"size", S("s")} })));

    const int32 CraftStart=Rows.size();
    // Bird model — three full-width buttons with a ●/○ selection marker.
    for (int32 M = 0; M < 3; ++M)
    {
        FString Label;
        Label += ModelNames[M];
        FProps Props;
        Props["label"] = S(Label);
        Props["action"] = S(FString::Printf(TEXT("bird.model.%d"), M));
        Props["tone"] = S(M==CurModel ? "good" : "normal");
        Props["tooltip"] = S("Select this aircraft for the current flight.");
        ModelButtons.Add(RowPath((int32)Rows.size()));
        Rows.push_back(Nd("Button", std::move(Props)));
    }

    // Camera mode toggle (FPV / chase).
    {
        FProps Props;
        Props["action"] = S("camera.fpv");
        Props["label"] = S("AIRBORNE CAMERA");
        Props["on"] = S("FPV  /  click for chase");
        Props["off"] = S("CHASE  /  click for FPV");
        Props["value"] = B(Bird->IsFpvAirView());
        Rows.push_back(Nd("Toggle", std::move(Props)));
    }

    const int32 BirdStart=Rows.size();
    // Further bird tuning (BIRD tab).
    Rows.push_back(Nd("TextBlock", P({ {"text", S("BIRD TUNING")}, {"tone", S("accent")}, {"size", S("l")} })));
    {
        FProps Props;
        Props["action"] = S("camera.angle");
        Props["label"] = S("FPV CAMERA ANGLE");
        Props["value"] = N(Bird->GetFpvCameraAngle());
        Props["min"] = N(-45);
        Props["max"] = N(45);
        Props["step"] = N(1);
        Props["unit"] = S(FString(TEXT("°")));
        Rows.push_back(Nd("Slider", std::move(Props)));
    }
    {
        FProps Props;
        Props["action"] = S("bird.coupled");
        Props["label"] = S("COUPLED THROTTLE");
        Props["on"] = S("ON  /  click to decouple");
        Props["off"] = S("OFF  /  click to couple");
        Props["value"] = B(Bird->IsThrottleCoupled());
        Rows.push_back(Nd("Toggle", std::move(Props)));
    }

    const int32 ControlsStart=Rows.size();
    // Mouse response.
    Rows.push_back(Nd("TextBlock", P({ {"text", S("MOUSE RESPONSE")}, {"tone", S("accent")}, {"size", S("l")} })));
    Rows.push_back(Nd("TextBlock", P({ {"text", S("Negative reverses direction. Zero disables that mouse axis. Magnitude sets sensitivity.")}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        FProps Props;
        Props["action"] = S(FString::Printf(TEXT("mouse.gain.%d"), Axis));
        Props["label"] = S(FString(MouseNames[Axis]));
        Props["tooltip"] = S("Negative values reverse the axis. Zero disables it. Larger magnitudes increase sensitivity.");
        Props["value"] = N(Bird->GetMouseGains()[Axis]);
        Props["min"] = N(-2);
        Props["max"] = N(2);
        Props["step"] = N(0.05);
        Props["unit"] = S(FString(TEXT("×")));
        MouseGainSliders.Add(RowPath((int32)Rows.size()));
        Rows.push_back(Nd("Slider", std::move(Props)));
    }
    Rows.push_back(Nd("TextBlock", P({ {"text", S(FString(TEXT("−2 reverse / faster          0 off          +2 forward / faster")))}, {"tone", S("dim")}, {"size", S("xs")} })));
    {
        FProps Props;
        Props["action"] = S("mouse.reset");
        Props["label"] = S("Reset mouse response");
        Rows.push_back(Nd("Button", std::move(Props)));
    }

    // Control expo (shown as a whole-number percentage).
    Rows.push_back(Nd("TextBlock", P({ {"text", S("CONTROL EXPO  /  MOUSE + KEYBOARD")}, {"tone", S("accent")}, {"size", S("l")} })));
    {
        FProps Props;
        Props["action"] = S("control.expo");
        Props["label"] = S("EXPO");
        Props["value"] = N(Bird->GetControlExpo() * 100.0);
        Props["min"] = N(0);
        Props["max"] = N(100);
        Props["step"] = N(1);
        Props["unit"] = S("%");
        Rows.push_back(Nd("Slider", std::move(Props)));
    }

    // Mouse speed modifier (0..1).
    {
        FProps Props;
        Props["action"] = S("mouse.speed");
        Props["label"] = S("MOUSE SPEED");
        Props["value"] = N(Bird->GetSpeedModifier());
        Props["min"] = N(0);
        Props["max"] = N(1);
        Props["step"] = N(0.05);
        Props["unit"] = S(FString(TEXT("×")));
        Rows.push_back(Nd("Slider", std::move(Props)));
    }

    const int32 AssistStart=Rows.size();
    // Flight-safety reset amount — one slider, three stops.
    Rows.push_back(Nd("TextBlock", P({ {"text", S("FLIGHT SAFETY RESET")}, {"tone", S("accent")}, {"size", S("l")} })));
    Rows.push_back(Nd("TextBlock", P({ {"text", S(FString(TEXT("0 = OFF (only glitch guards)   ·   1 = VERY LOW (acro)   ·   2 = NORMAL")))}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
    {
        FProps Props;
        Props["action"] = S("flight.safety");
        Props["label"] = S("SAFETY");
        Props["tooltip"] = S("0: only numerical glitch guards. 1: light recovery assistance. 2: normal flight recovery assistance.");
        Props["value"] = N(Bird->GetFlightSafety());
        Props["min"] = N(0);
        Props["max"] = N(2);
        Props["step"] = N(1);
        Rows.push_back(Nd("Slider", std::move(Props)));
    }

    // Wingbeat-only audio: scale just the flap voice so it cuts through the
    // wind/surf bed and music when it gets buried.
    Rows.push_back(Nd("TextBlock", P({ {"text", S("AUDIO  /  WINGBEAT")}, {"tone", S("accent")}, {"size", S("l")} })));
    Rows.push_back(Nd("TextBlock", P({ {"text", S(FString(TEXT("Raises only the wing-flap voice — wind, surf and music stay untouched.")))}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
    {
        FProps Props;
        Props["action"] = S("audio.wingbeat");
        Props["label"] = S("WINGBEAT VOLUME");
        Props["tooltip"] = S("Only the wingbeat gets louder/quieter (1× = default). Wind, ocean and music are unaffected.");
        Props["value"] = N(Bird->GetWingbeatVolume());
        Props["min"] = N(0);
        Props["max"] = N(2);
        Props["step"] = N(0.05);
        Props["unit"] = S(FString(TEXT("×")));
        Rows.push_back(Nd("Slider", std::move(Props)));
    }

    // Replay shadow-doppelgängers: toggle visibility + purge saved recordings.
    Rows.push_back(Nd("TextBlock", P({ {"text", S("REPLAY SHADOWS")}, {"tone", S("accent")}, {"size", S("l")} })));
    Rows.push_back(Nd("TextBlock", P({ {"text", S(FString(TEXT("Past flights fly alongside as dark doppelgängers (gold = best round).")))}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
    {
        FProps Props;
        Props["action"] = S("replay.spirits");
        Props["label"] = S(FString(TEXT("SHADOW DOPPELGÄNGERS")));
        Props["on"] = S("ON  /  click to hide");
        Props["off"] = S("OFF  /  click to show");
        Props["value"] = B(Bird->GetReplaySpiritsEnabled());
        Rows.push_back(Nd("Toggle", std::move(Props)));
    }
    {
        FProps Props;
        Props["action"] = S("replay.delete");
        Props["label"] = S("Delete all replay shadows");
        Props["tone"] = S("danger");
        Rows.push_back(Nd("Button", std::move(Props)));
    }

    const int32 TuningStart=Rows.size();
    // Tuning — firmware knobs (metadata in Born2FlapTuning.h).
    Rows.push_back(Nd("TextBlock", P({ {"text", S("H A N G A R   /   TUNING")}, {"tone", S("accent")}, {"size", S("l")} })));
    Rows.push_back(Nd("TextBlock", P({ {"text", S(FString(TEXT("Live edits reach the firmware on the next physics step — the bird re-tunes itself.")))}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
    // Servo preset: load speed/torque/voltage defaults from a servo tested for
    // flapping. The selected preset stays highlighted until any servo slider is
    // edited by hand, which flips the readout to CUSTOM.
    {
        FValue ServoOptions; ServoOptions.kind = FValue::Kind::Array;
        for (const FServoPreset& Sp : ServoPresets) ServoOptions.arr.push_back(S(FString(Sp.Name)));
        ServoSelectPath = RowPath((int32)Rows.size());
        Rows.push_back(Nd("Dropdown", P({
            {"label", S("SERVO PRESET  /  load tested defaults")},
            {"options", std::move(ServoOptions)},
            {"value", N(ServoPresetIndex)},
            {"action", S("servo.preset")},
            {"tooltip", S("Load speed, stall torque and voltage defaults from a servo tested for flapping (dantiel.github.io/OrniFlight). Editing any servo slider afterwards marks it CUSTOM.")}
        })));
        ServoStatusPath = RowPath((int32)Rows.size());
        const FString StatusText = (ServoPresetIndex >= 0 && ServoPresetIndex < ServoPresetCount)
            ? FString(TEXT("SERVO:  ")) + ServoPresets[ServoPresetIndex].Name
            : TEXT("SERVO:  CUSTOM  /  manual");
        Rows.push_back(Nd("TextBlock", P({
            {"text", S(StatusText)},
            {"tone", S(ServoPresetIndex >= 0 ? "good" : "dim")},
            {"size", S("s")}
        })));
    }
    for (const born2flap::tuning::FRow& Row : born2flap::tuning::Rows)
    {
        FProps Props;
        Props["action"] = S(TCHAR_TO_UTF8(born2flap::tuning::Key(Row.Field)));
        Props["label"] = S(FString(Row.Label));
        Props["value"] = N(born2flap::tuning::ToDisplayValue(Row.Field, Bird->GetTuning(Row.Field)));
        Props["min"] = N(Row.Min);
        Props["max"] = N(Row.Max);
        Props["step"] = N(FMath::Pow(10.0f, (float)-Row.Decimals));
        Props["unit"] = S(FString(Row.Unit));
        Rows.push_back(Nd("Slider", std::move(Props)));
    }
    // Airframe mass + centre of gravity: physical body properties, not firmware
    // knobs — they act directly on the Chaos body (mass and COM offset).
    {
        FProps Props;
        Props["action"] = S("bird.weight");
        Props["label"] = S("BIRD WEIGHT");
        Props["value"] = N(Bird->GetBodyMassKg() * 1000.f);
        Props["min"] = N(10);
        Props["max"] = N(2000);
        Props["step"] = N(5);
        Props["unit"] = S("g");
        Rows.push_back(Nd("Slider", std::move(Props)));
    }
    {
        FProps Props;
        Props["action"] = S("bird.cg");
        Props["label"] = S("CENTRE OF GRAVITY");
        Props["value"] = N(Bird->GetCgOffsetMm());
        Props["min"] = N(-30);
        Props["max"] = N(30);
        Props["step"] = N(1);
        Props["unit"] = S("mm");
        Rows.push_back(Nd("Slider", std::move(Props)));
    }

    for(int32 I=CraftStart;I<(int32)Rows.size();++I)
        Rows[I].props["visible"]=B(SettingsPage==(I<BirdStart ? 0 : I<ControlsStart ? 1 : I<AssistStart ? 2 : I<TuningStart ? 3 : 4));
    // Save & return.
    {
        FProps Props;
        Props["action"] = S("settings.close");
        Props["label"] = S("SAVE & RETURN TO FLIGHT   /   F10");
        Props["tone"] = S("good");
        Rows.push_back(Nd("Button", std::move(Props)));
    }

    FNode Tabs=Rows.front(), Footer=Rows.back();
    Rows.front().props["visible"]=B(false);
    Rows.back().props["visible"]=B(false);

    Out.push_back(std::move(Tabs));
    Out.push_back(Nd("ScrollBox", P({}), { Nd("VerticalBox", P({ {"spacing", N(12)} }), std::move(Rows)) }));
    Out.push_back(std::move(Footer));
}

void ABorn2FlapMenu::RefreshModelButtons()
{
    if (!Renderer || !Bird.IsValid())
        return;
    static const TCHAR* ModelNames[] =
    {
        TEXT("RAVENCROW  /  folded obsidian & comb pinions"),
        TEXT("PROTOTYPE  /  elliptical feathers"),
        TEXT("COMMON KESTREL  /  falcon"),
    };
    const int32 Cur = Bird->GetBirdModel();
    for (int32 M = 0; M < 3 && M < ModelButtons.Num(); ++M)
    {
        FString Label;
        Label += ModelNames[M];
        FProps Props;
        Props["label"] = S(Label);
        Props["tone"] = S(M==Cur ? "good" : "normal");
        SendUpdate(Renderer, ModelButtons[M], std::move(Props));
    }
}

void ABorn2FlapMenu::RefreshMouseGains()
{
    if (!Renderer || !Bird.IsValid())
        return;
    for (int32 Axis = 0; Axis < 3 && Axis < MouseGainSliders.Num(); ++Axis)
    {
        FProps Props;
        Props["value"] = N(Bird->GetMouseGains()[Axis]);
        SendUpdate(Renderer, MouseGainSliders[Axis], std::move(Props));
    }
}

void ABorn2FlapMenu::MarkServoCustom()
{
    if (ServoPresetIndex == -1 || !Renderer || !Bird.IsValid())
        return;
    ServoPresetIndex = -1;
    SendUpdate(Renderer, ServoSelectPath, P({ {"value", N(-1)} }));
    SendUpdate(Renderer, ServoStatusPath, P({ {"text", S("SERVO:  CUSTOM  /  manual")}, {"tone", S("dim")} }));
}

void ABorn2FlapMenu::Close()
{
    // The menu is now the tuning surface too; persist any live flight-desk edits
    // before teardown (F10 / RESUME / SAVE all funnel through here).
    if (bInLevel)
        if (ABorn2FlapFlightPawn* B = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            B->SaveFlightPreferences();
    if (Renderer)
        Renderer->Close();
}

void ABorn2FlapMenu::RestoreView(int32 Nav, int32 SettingsSub)
{
    const int32 N = FMath::Clamp(Nav, 0, 1);
    const int32 S = FMath::Clamp(SettingsSub, 0, 4);
    if (N != NavPage || S != SettingsPage)
    {
        NavPage = N;
        SettingsPage = S;
        Build();
    }
}

void ABorn2FlapMenu::SetOpacity(float Opacity)
{
    if (!Renderer)
        return;
    Renderer->EnsureAttached();
    FProps Props;
    Props["opacity"] = N(FMath::Clamp(Opacity, 0.f, 1.f));
    TArray<FOp> Ops;
    FOp O;
    O.op = EOp::UpdateProps;
    O.path = FPath();
    O.props = std::move(Props);
    Ops.Add(O);
    Renderer->ApplyOps(Ops);
}

void ABorn2FlapMenu::OnAction(const FString& Action, float Value, const FString& Text)
{
    // ---- top-level pages: OPEN WORLDS and FLIGHT DESK share one full-screen
    // view; the nav rail switches between them, no back-and-forth ----
    if (Action == TEXT("menu.play"))     { NavPage = 0; Build(); return; }
    if (Action == TEXT("menu.settings")) { NavPage = 1; Build(); return; }

    // ---- OPEN WORLDS page ----
    if(Action==TEXT("menu.page")){SelectedPage=FMath::Clamp(FMath::RoundToInt(Value),0,2);Build();return;}
    if(Action.StartsWith(TEXT("weather.")) || Action.StartsWith(TEXT("daytime.")))
    {
        const bool Weather=Action.StartsWith(TEXT("weather."));
        const FString Level=Action.Mid(8);
        if(auto* C=LevelConditions.Find(Level))
        {
            const auto Options=Weather ? Born2FlapWeather::WeatherOptions(Level) : Born2FlapWeather::TimeOptions(Level,C->Weather);
            const int32 Index=FMath::RoundToInt(Value);
            if(Options.IsValidIndex(Index))
            {
                if(Weather) C->Weather=Options[Index]; else C->Time=Options[Index];
                *C=Born2FlapWeather::Validate(Level,*C);
                if(!FParse::Param(FCommandLine::Get(),TEXT("B2FWeatherMenuTest"))) Born2FlapWeather::Save(Level,*C);
                Build();
            }
        }
        return;
    }
    // Travel-destination list: "poi.<index>" sets the chosen start point for the
    // currently displayed world; the FLY button later travels there (?PoiKey=).
    if (Action.StartsWith(TEXT("poi.")))
    {
        SelectedPoi.Add(Worlds[SelectedWorld].Id, FCString::Atoi(*Action.Mid(4)));
        Build();
        return;
    }
    // Page between open worlds in the chooser (one at a time, never all listed).
    if (Action == TEXT("menu.prevWorld"))
    {
        SelectedWorld = (SelectedWorld + WorldCount - 1) % WorldCount;
        Build();
        return;
    }
    if (Action == TEXT("menu.nextWorld"))
    {
        SelectedWorld = (SelectedWorld + 1) % WorldCount;
        Build();
        return;
    }

    // ---- menu chrome ----
    if (Action == TEXT("menu.resume"))
    {
        if (ABorn2FlapGameMode* GM = Cast<ABorn2FlapGameMode>(UGameplayStatics::GetGameMode(this)))
            GM->CloseMainMenu();
        return;
    }
    if (Action == TEXT("menu.quit"))
    {
        UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
        return;
    }

    // ---- level travel (FLY button, handled before the flight-desk guard) ----
    // Route a level button to its map. ?Level= drives the level flags in
    // InitGame; ?SkipMenu=1 keeps the menu from re-opening on the destination.
    // This runs first so FLY works on the home screen too (no live bird yet).
    struct FEntry { const TCHAR* Action; const TCHAR* Map; const TCHAR* Level; };
    static const FEntry Entries[] = {
        { TEXT("menu.ravenstonefield"), TEXT("/Game/Ravenstonefield/Maps/RAVENSTONEFIELD"), TEXT("Ravenstonefield") },
        { TEXT("menu.shiomori"),        TEXT("/Game/Shiomori/Maps/SHIOMORI"),               TEXT("Shiomori") },
        { TEXT("menu.training"),        TEXT("/Engine/Maps/Entry"),                         TEXT("Training") },
    };
    for (const FEntry& E : Entries)
    {
        if (Action == E.Action)
        {
            FString Options = Born2FlapWeather::TravelOptions(E.Level, LevelConditions.FindChecked(E.Level));
            if (const int32* Sel = SelectedPoi.Find(E.Level))
            {
                const TArray<FBorn2FlapPoi> Cat = Born2FlapPoi::Catalog(E.Level);
                if (Cat.IsValidIndex(*Sel))
                {
                    Options += FString::Printf(TEXT("?PoiKey=%s"), *Cat[*Sel].Key);
                    if (Cat[*Sel].Number > 0)
                        Options += FString::Printf(TEXT("?PoiNum=%d"), Cat[*Sel].Number);
                }
            }
            // Cover the viewport (hide the main window) with the full-bleed
            // loading screen until the chosen world has finished loading.
            Born2FlapShowLoadingScreen();
            UGameplayStatics::OpenLevel(this, E.Map, true, Options);
            return;
        }
    }

    // ---- FLIGHT DESK page (reachable only in-level with a live bird) ----
    if (!Bird.IsValid())
        return;
    if(Action==TEXT("settings.page")){SettingsPage=FMath::Clamp(FMath::RoundToInt(Value),0,4);Build();return;}
    if (Action == TEXT("settings.close"))
    {
        if (ABorn2FlapGameMode* GM = Cast<ABorn2FlapGameMode>(UGameplayStatics::GetGameMode(this)))
            GM->CloseMainMenu();
        return;
    }
    if (Action.StartsWith(TEXT("bird.model.")))
    {
        const int32 Model = FCString::Atoi(*Action.RightChop(FCString::Strlen(TEXT("bird.model."))));
        Bird->SelectBirdModel(Model);
        RefreshModelButtons();
        return;
    }
    if (Action == TEXT("camera.fpv")) { Bird->ToggleFpvView(); return; }
    if (Action == TEXT("camera.angle")) { Bird->SetFpvCameraAngle(Value); return; }
    if (Action == TEXT("bird.coupled")) { Bird->SetThrottleCoupled(Value > 0.5f); return; }
    if (Action == TEXT("mouse.speed")) { Bird->SetSpeedModifier(Value); return; }
    if (Action == TEXT("bird.weight")) { Bird->SetBodyMassKg(Value / 1000.f); return; }
    if (Action == TEXT("bird.cg")) { Bird->SetCgOffsetMm(Value); return; }
    if (Action == TEXT("servo.preset"))
    {
        const int32 Idx = FMath::Clamp(FMath::RoundToInt(Value), 0, ServoPresetCount - 1);
        const FServoPreset& Sp = ServoPresets[Idx];
        Bird->SetTuning(ETuningField::ServoSpeed, Sp.SpeedDegS);
        Bird->SetTuning(ETuningField::StallTorque, Sp.StallTorqueNm);
        Bird->SetTuning(ETuningField::Backdrive, Sp.Backdrive);
        Bird->SetTuning(ETuningField::BatteryVoltage, Sp.Voltage);
        ServoPresetIndex = Idx;
        Build();
        return;
    }
    if (Action.StartsWith(TEXT("mouse.gain.")))
    {
        const int32 Axis = FCString::Atoi(*Action.RightChop(FCString::Strlen(TEXT("mouse.gain."))));
        Bird->SetMouseGain(Axis, Value);
        return;
    }
    if (Action == TEXT("mouse.reset"))
    {
        Bird->SetMouseGain(0, 1.f);
        Bird->SetMouseGain(1, -1.f);
        Bird->SetMouseGain(2, 1.f);
        RefreshMouseGains();
        return;
    }
    if (Action == TEXT("control.expo")) { Bird->SetControlExpo(Value / 100.f); return; }
    if (Action == TEXT("flight.safety")) { Bird->SetFlightSafety(Value); return; }
    if (Action == TEXT("audio.wingbeat")) { Bird->SetWingbeatVolume(Value); return; }
    if (Action == TEXT("replay.spirits")) { Bird->SetReplaySpiritsEnabled(Value > 0.5f); return; }
    if (Action == TEXT("replay.delete")) { Bird->DeleteReplaySpirits(); return; }

    const ETuningField Field = born2flap::tuning::FromKey(Action);
    if (Field != ETuningField::Count)
    {
        Bird->SetTuning(Field, born2flap::tuning::FromDisplayValue(Field, Value));
        if (Field == ETuningField::ServoSpeed || Field == ETuningField::StallTorque ||
            Field == ETuningField::Backdrive || Field == ETuningField::BatteryVoltage)
            MarkServoCustom();
        return;
    }

}