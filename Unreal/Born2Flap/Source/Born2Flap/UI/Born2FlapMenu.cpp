// Born2FlapMenu.cpp — the semantic main-menu / level-selector implementation.

#include "UI/Born2FlapMenu.h"

#include "Born2Flap.h"
#include "UI/Born2FlapUIRenderer.h"
#include "UI/Born2FlapUiOps.h"
#include "UI/Born2FlapI18n.h"
#include "Game/Born2FlapGameMode.h"
#include "Game/Born2FlapPoi.h"
#include "UI/Born2FlapRadio.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Flight/Born2FlapTuning.h"
#include "Input/Born2FlapRcController.h"
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

// Available locales for the language switch. Names are the languages' native
// endonyms (always shown in their own language regardless of the active locale);
// codes must match the locale JSON filenames under Brain/lib/born2flap/i18n/locales.
struct FLanguageDef { const TCHAR* Code; const TCHAR* Name; };
static const FLanguageDef Languages[] = {
    { TEXT("en"), TEXT("English") },
    { TEXT("de"), TEXT("Deutsch") },
    { TEXT("es"), TEXT("Español") },
    { TEXT("fr"), TEXT("Français") },
    { TEXT("it"), TEXT("Italiano") },
    { TEXT("pt"), TEXT("Português") },
    { TEXT("no"), TEXT("Norsk") },
    { TEXT("ar"), TEXT("العربية") },
    { TEXT("hi"), TEXT("हिन्दी") },
    { TEXT("ja"), TEXT("日本語") },
    { TEXT("ko"), TEXT("한국어") },
    { TEXT("ru"), TEXT("Русский") },
    { TEXT("zh"), TEXT("中文") },
};
constexpr int32 LanguageCount = (int32)(sizeof(Languages) / sizeof(Languages[0]));

}  // namespace

ABorn2FlapMenu::ABorn2FlapMenu()
{
    PrimaryActorTick.bCanEverTick = true;  // drive the home-screen RC fallback
}

ABorn2FlapMenu::~ABorn2FlapMenu() = default;

void ABorn2FlapMenu::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // Home screen: the pawn isn't ticking an RC controller, so the menu drives
    // its own fallback (device scan + axis polling for CONTROL SETTINGS).
    if (!Bird.IsValid() && RcFallback)
    {
        APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
        RcFallback->Tick(PC, DeltaSeconds);
    }
}

FBorn2FlapRcController* ABorn2FlapMenu::ActiveRc()
{
    if (Bird.IsValid())
        if (FBorn2FlapRcController* Rc = Bird->GetRcControllerMutable())
            return Rc;
    if (!RcFallback)
        RcFallback = MakeUnique<FBorn2FlapRcController>();
    return RcFallback.Get();
}

void ABorn2FlapMenu::SyncSettingsFromBird()
{
    if (Bird.IsValid())
        born2flap::MenuSettingsFromPawn(Bird.Get(), Settings);
}

void ABorn2FlapMenu::ApplySettingsToBird()
{
    if (Bird.IsValid())
        born2flap::MenuSettingsToPawn(Settings, Bird.Get());
}

void ABorn2FlapMenu::PersistSettings()
{
    born2flap::MenuSettingsSave(Settings);
}

float ABorn2FlapMenu::TuningValue(ETuningField Field) const
{
    return born2flap::MenuSettingsGetTuning(Settings, Field);
}

void ABorn2FlapMenu::SetTuningValue(ETuningField Field, float Value)
{
    born2flap::MenuSettingsSetTuning(Settings, Field, Value);
}

int32 ABorn2FlapMenu::CurBirdModel() const
{
    return Settings.BirdModel < 0 ? 0 : Settings.BirdModel;
}

void ABorn2FlapMenu::BeginPlay()
{
    Super::BeginPlay();
    Renderer = NewObject<UBorn2FlapUIRenderer>(this);
    Renderer->ViewportZOrder = 90; // under the splash (100), above the cockpit (0)
    Renderer->OnComponentAction.AddDynamic(this, &ABorn2FlapMenu::OnAction);
    born2flap::MenuSettingsLoad(Settings);
    // Apply the persisted language before the first Build() so every localized
    // string (nav rail, pane titles, HUD/status via the shared locale) is right.
    Born2Flap::I18n::SetLocale(Settings.Language);
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
    if (bInLevel && Bird.IsValid())
        SyncSettingsFromBird();

    FString PaneTitle = Born2Flap::I18n::T("menu.open_worlds");
    std::vector<FNode> PaneChildren;
    FNode ContentPane;

    if (NavPage == 2)
    {
        PaneTitle = Born2Flap::I18n::T("menu.settings");
        BuildPreferences(PaneChildren);
        ContentPane = Nd("SizeBox", P({ {"width", N(1040)}, {"height", N(860)} }),
        {
            Nd("Panel", P({ {"title", S(PaneTitle)}, {"bg", S("solid")}, {"padding", N(26)}, {"spacing", N(12)} }), std::move(PaneChildren))
        });
    }
    else if (NavPage == 1)
    {
        PaneTitle = Born2Flap::I18n::T("menu.flight_desk");
        // ---- FLIGHT DESK page: the integrated tuning panel ----
        // Always reachable (home screen or in-level). Edits live in Settings and
        // are mirrored into the bird when one is present.
        BuildFlightDesk(PaneChildren);
        ContentPane = Nd("SizeBox", P({ {"width", N(1040)}, {"height", N(860)} }),
        {
            Nd("Panel", P({ {"title", S(PaneTitle)}, {"bg", S("solid")}, {"padding", N(26)}, {"spacing", N(12)} }), std::move(PaneChildren))
        });
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
    Sidebar.push_back(Ghost(Born2Flap::I18n::T("menu.open_worlds"), "menu.play", NavPage == 0));
    Sidebar.push_back(Divider());
    Sidebar.push_back(Ghost(Born2Flap::I18n::T("menu.flight_desk"), "menu.settings", NavPage == 1));
    Sidebar.push_back(Divider());
    Sidebar.push_back(Ghost(Born2Flap::I18n::T("menu.settings"), "menu.preferences", NavPage == 2));
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
    // The desk is purely the hangar — choose the aircraft and tune it. Global
    // preferences (camera, audio, input, replay) live in SETTINGS → GENERAL.
    static const TArray<int32> VBox{ 1, 1, 0, 1, 0 };
    auto RowPath = [&](int32 Row) { TArray<int32> P = VBox; P.Add(Row); return P; };

    const int32 CurModel = CurBirdModel();

    ServoSelectPath.Reset();
    ServoStatusPath.Reset();
    MouseGainSliders.Reset();

    std::vector<FNode> Rows;
    std::vector<int32> RowPage;  // flight-desk tab index per row (or -1 for chrome)

    auto Push = [&](FNode Node_, int32 Page) { RowPage.push_back(Page); Rows.push_back(std::move(Node_)); };

    // Sub-tabs: CRAFT / BIRD / SERVO / FLIGHT.
    FValue Pages; Pages.kind = FValue::Kind::Array;
    for (const char* Page : { "CRAFT", "BIRD", "SERVO", "FLIGHT" }) Pages.arr.push_back(S(Page));
    Push(Nd("Select", P({ {"options", Pages}, {"value", N(SettingsPage)}, {"action", S("settings.page")}, {"tooltip", S("Choose the aircraft, tune the bird, configure the servo and battery, or trim the flight.")} })), -1);

    // A tuning row grouped into one of the desk's tabs.
    auto PushTuning = [&](ETuningField Field, int32 Page)
    {
        const born2flap::tuning::FRow& R = born2flap::tuning::Row(Field);
        FProps Props;
        Props["action"] = S(TCHAR_TO_UTF8(born2flap::tuning::Key(Field)));
        Props["label"] = S(FString(R.Label));
        Props["value"] = N(born2flap::tuning::ToDisplayValue(Field, TuningValue(Field)));
        Props["min"] = N(R.Min);
        Props["max"] = N(R.Max);
        Props["step"] = N(FMath::Pow(10.0f, (float)-R.Decimals));
        Props["unit"] = S(FString(R.Unit));
        Props["tooltip"] = S(FString(R.Help));
        Push(Nd("Slider", std::move(Props)), Page);
    };

    // ---- CRAFT (0): choose the silhouette (segmented, one selection) ----
    Push(Nd("TextBlock", P({ {"text", S("CHOOSE YOUR SILHOUETTE")}, {"tone", S("accent")}, {"size", S("l")} })), 0);
    {
        FValue Silhouettes; Silhouettes.kind = FValue::Kind::Array;
        for (const TCHAR* Name : { TEXT("RAVENCROW"), TEXT("PROTOTYPE"), TEXT("COMMON KESTREL") }) Silhouettes.arr.push_back(S(FString(Name)));
        Push(Nd("Segment", P({
            {"options", std::move(Silhouettes)},
            {"value", N(CurModel)},
            {"action", S("bird.model")},
            {"tooltip", S("Select the aircraft silhouette for the next flight — it applies to whichever world you enter.")}
        })), 0);
    }

    // ---- BIRD (1): physical body properties ----
    Push(Nd("TextBlock", P({ {"text", S("BODY")}, {"tone", S("accent")}, {"size", S("l")} })), 1);
    {
        FProps Props;
        Props["action"] = S("bird.weight");
        Props["label"] = S("BIRD WEIGHT");
        Props["tooltip"] = S("Airframe mass. Light = smaller wing, faster flap, twitchy; heavy = larger wing, slower, more inertia.");
        Props["value"] = N(Settings.BodyMassKg * 1000.f);
        Props["min"] = N(10);
        Props["max"] = N(2000);
        Props["step"] = N(5);
        Props["unit"] = S("g");
        Push(Nd("Slider", std::move(Props)), 1);
    }
    {
        const float CgRange = CgRangeCm();
        FProps Props;
        Props["action"] = S("bird.cg");
        Props["label"] = S("CENTRE OF GRAVITY");
        Props["tooltip"] = S("Longitudinal balance along the body (0 = centred). Negative = forward/nose-heavy (stable, resists pitch-up); positive = aft/tail-heavy (nervous, wants to climb). Range scales with the craft's length.");
        Props["value"] = N(Settings.CgOffsetMm / 10.f);
        Props["min"] = N(-CgRange);
        Props["max"] = N(CgRange);
        Props["step"] = N(0.1);
        Props["unit"] = S("cm");
        Push(Nd("Slider", std::move(Props)), 1);
    }
    {
        FProps Props;
        Props["action"] = S("bird.coupled");
        Props["label"] = S("COUPLED THROTTLE");
        Props["on"] = S("ON  /  click to decouple");
        Props["off"] = S("OFF  /  click to couple");
        Props["value"] = B(Settings.bCoupledThrottle);
        Push(Nd("Toggle", std::move(Props)), 1);
    }
    Push(Nd("TextBlock", P({ {"text", S("MOUNT")}, {"tone", S("accent")}, {"size", S("l")} })), 1);
    PushTuning(ETuningField::MountAngle, 1);

    // ---- SERVO & POWER (2) ----
    Push(Nd("TextBlock", P({ {"text", S("SERVO")}, {"tone", S("accent")}, {"size", S("l")} })), 2);
    // Servo preset: load speed/torque/voltage defaults from a servo tested for
    // flapping. The selected preset stays highlighted until any servo slider is
    // edited by hand, which flips the readout to CUSTOM.
    {
        FValue ServoOptions; ServoOptions.kind = FValue::Kind::Array;
        for (const FServoPreset& Sp : ServoPresets) ServoOptions.arr.push_back(S(FString(Sp.Name)));
        ServoSelectPath = RowPath((int32)Rows.size());
        Push(Nd("Dropdown", P({
            {"label", S("SERVO PRESET  /  load tested defaults")},
            {"options", std::move(ServoOptions)},
            {"value", N(ServoPresetIndex)},
            {"action", S("servo.preset")},
            {"tooltip", S("Load speed, stall torque and voltage defaults from a servo tested for flapping (dantiel.github.io/OrniFlight). Editing any servo slider afterwards marks it CUSTOM.")}
        })), 2);
        ServoStatusPath = RowPath((int32)Rows.size());
        const FString StatusText = (ServoPresetIndex >= 0 && ServoPresetIndex < ServoPresetCount)
            ? FString(TEXT("SERVO:  ")) + ServoPresets[ServoPresetIndex].Name
            : TEXT("SERVO:  CUSTOM  /  manual");
        Push(Nd("TextBlock", P({
            {"text", S(StatusText)},
            {"tone", S(ServoPresetIndex >= 0 ? "good" : "dim")},
            {"size", S("s")}
        })), 2);
    }
    PushTuning(ETuningField::ServoSpeed, 2);
    PushTuning(ETuningField::StallTorque, 2);
    PushTuning(ETuningField::Backdrive, 2);
    Push(Nd("TextBlock", P({ {"text", S("BATTERY")}, {"tone", S("accent")}, {"size", S("l")} })), 2);
    PushTuning(ETuningField::BatteryVoltage, 2);
    PushTuning(ETuningField::BatteryResistance, 2);
    PushTuning(ETuningField::BatteryCapacity, 2);

    // ---- FLIGHT (3): stroke, trim, control authority, assist ----
    Push(Nd("TextBlock", P({ {"text", S("STROKE")}, {"tone", S("accent")}, {"size", S("l")} })), 3);
    PushTuning(ETuningField::FlapBaseFreq, 3);
    PushTuning(ETuningField::StrokeFerocity, 3);
    Push(Nd("TextBlock", P({ {"text", S("TRIM")}, {"tone", S("accent")}, {"size", S("l")} })), 3);
    PushTuning(ETuningField::TailElevatorAngle, 3);
    PushTuning(ETuningField::GlideAngle, 3);
    Push(Nd("TextBlock", P({ {"text", S("CONTROL AUTHORITY")}, {"tone", S("accent")}, {"size", S("l")} })), 3);
    PushTuning(ETuningField::AileronScale, 3);
    PushTuning(ETuningField::ElevatorScale, 3);
    Push(Nd("TextBlock", P({ {"text", S("ASSIST")}, {"tone", S("accent")}, {"size", S("l")} })), 3);
    Push(Nd("TextBlock", P({ {"text", S(FString(TEXT("0 = OFF (only glitch guards)   ·   1 = VERY LOW (acro)   ·   2 = NORMAL")))}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })), 3);
    {
        FProps Props;
        Props["action"] = S("flight.safety");
        Props["label"] = S("SAFETY");
        Props["tooltip"] = S("0: only numerical glitch guards. 1: light recovery assistance. 2: normal flight recovery assistance.");
        Props["value"] = N(Settings.FlightSafety);
        Props["min"] = N(0);
        Props["max"] = N(2);
        Props["step"] = N(1);
        Push(Nd("Slider", std::move(Props)), 3);
    }

    // Show only the rows belonging to the active sub-tab.
    for (int32 I = 0; I < (int32)Rows.size(); ++I)
        if (RowPage[I] >= 0)
            Rows[I].props["visible"] = B(SettingsPage == RowPage[I]);

    // Save & return.
    {
        FProps Props;
        Props["action"] = S("settings.close");
        Props["label"] = S("SAVE & RETURN TO FLIGHT   /   F10");
        Props["tone"] = S("good");
        Push(Nd("Button", std::move(Props)), -1);
    }

    FNode Tabs=Rows.front(), Footer=Rows.back();
    Rows.front().props["visible"]=B(false);
    Rows.back().props["visible"]=B(false);

    Out.push_back(std::move(Tabs));
    Out.push_back(Nd("ScrollBox", P({}), { Nd("VerticalBox", P({ {"spacing", N(12)} }), std::move(Rows)) }));
    Out.push_back(std::move(Footer));
}

void ABorn2FlapMenu::BuildPreferences(std::vector<FNode>& Out)
{
    FValue Pages; Pages.kind = FValue::Kind::Array;
    for (const char* Pg : { "CONTROL SETTINGS", "GENERAL SETTINGS" }) Pages.arr.push_back(S(Pg));
    Out.push_back(Nd("Select", P({ {"options", Pages}, {"value", N(PrefsPage)}, {"action", S("prefs.page")}, {"tooltip", S("Configure the RC transmitter, or browse general game options.")} })));

    if (PrefsPage == 0)
    {
        // ---- CONTROL SETTINGS: the RC transmitter panel, embedded ----
        if (FBorn2FlapRcController* Rc = ActiveRc())
        {
                Out.push_back(Nd("Banner", P({ {"text", S("RC TRANSMITTER")}, {"tone", S("accent")} })));
                Out.push_back(Nd("TextBlock", P({ {"text", S(Rc->GetDeviceName())}, {"tone", S("info")}, {"size", S("m")}, {"wrap", B(true)} })));
                Out.push_back(Nd("TextBlock", P({ {"text", S(Rc->GetStatus())}, {"tone", S("dim")}, {"size", S("s")} })));
                Out.push_back(Nd("TextBlock", P({ {"text", S(Rc->GetInstruction())}, {"tone", S("accent")}, {"size", S("s")}, {"wrap", B(true)} })));
                if (!Rc->GetNotice().IsEmpty())
                    Out.push_back(Nd("TextBlock", P({ {"text", S(Rc->GetNotice())}, {"tone", S("accent")}, {"size", S("s")}, {"wrap", B(true)} })));

                Out.push_back(Nd("Field", P({ {"label", S("DEVICE")} })));
                Out.push_back(Nd("HorizontalBox", P({ {"spacing", N(8)} }), {
                    Nd("Button", P({ {"label", S("NEXT DEVICE")}, {"action", S("rc.device.next")} })),
                    Nd("Button", P({ {"label", S(Rc->IsEnabled() ? "DISABLE RC" : "ENABLE RC")}, {"action", S("rc.enable.toggle")}, {"tone", S("dim")} }))
                }));

                Out.push_back(Nd("Field", P({ {"label", S("CALIBRATION")} })));
                Out.push_back(Nd("HorizontalBox", P({ {"spacing", N(8)} }), {
                    Nd("Button", P({ {"label", S("START")}, {"action", S("rc.calibrate.start")}, {"tone", S("good")} })),
                    Nd("Button", P({ {"label", S("NEXT STEP")}, {"action", S("rc.calibrate.advance")} })),
                    Nd("Button", P({ {"label", S("CANCEL")}, {"action", S("rc.calibrate.cancel")}, {"tone", S("danger")} }))
                }));
                Out.push_back(Nd("HorizontalBox", P({ {"spacing", N(8)} }), {
                    Nd("Button", P({ {"label", S("LEARN START")}, {"action", S("rc.learn.launch")}, {"tone", S("dim")} })),
                    Nd("Button", P({ {"label", S("LEARN RESET")}, {"action", S("rc.learn.reset")}, {"tone", S("dim")} }))
                }));

                Out.push_back(Nd("Field", P({ {"label", S("LIVE AXES")} })));
                const auto& Axes = Rc->GetRawAxes();
                const auto& Avail = Rc->GetAvailableAxes();
                for (int32 I = 0; I < 8; ++I)
                {
                    FString AxisLabel;
                    if (Avail[I]) AxisLabel = FString::Printf(TEXT("%d: %d%%"), I + 1, FMath::RoundToInt(Axes[I] * 100.f));
                    else          AxisLabel = FString::Printf(TEXT("%d: --"), I + 1);
                    Out.push_back(Nd("Stat", P({ {"label", S(AxisLabel)}, {"value", N(Axes[I])}, {"tone", S(Avail[I] ? "info" : "dim")} })));
                }

                Out.push_back(Nd("Field", P({ {"label", S("CHANNEL MAPPING")} })));
                for (int32 I = 0; I < 8; ++I)
                    Out.push_back(Nd("TextBlock", P({ {"text", S(Rc->GetMapping(I))}, {"tone", S("dim")}, {"size", S("s")} })));
                Out.push_back(Nd("TextBlock", P({ {"text", S(Rc->GetButtons())}, {"tone", S("dim")}, {"size", S("xs")} })));
            }
    }
    else
    {
        // ---- GENERAL SETTINGS: camera, audio, input and replay, grouped ----
        {
            static const TArray<int32> VBox{ 1, 1, 0, 1, 0 };
            auto RowPath = [&](int32 Row) { TArray<int32> P = VBox; P.Add(Row); return P; };
            static const TCHAR* MouseNames[] =
            {
                TEXT("ROLL  /  mouse X"),
                TEXT("PITCH  /  mouse Y"),
                TEXT("YAW  /  mouse X"),
            };

            MouseGainSliders.Reset();
            std::vector<FNode> Rows;

            // LANGUAGE
            Rows.push_back(Nd("TextBlock", P({ {"text", S(Born2Flap::I18n::T("settings.language"))}, {"tone", S("accent")}, {"size", S("l")} })));
            {
                FValue LangOptions; LangOptions.kind = FValue::Kind::Array;
                int32 CurrentLang = 0;
                const FString Active = Born2Flap::I18n::GetLocale();
                for (int32 I = 0; I < LanguageCount; ++I)
                {
                    LangOptions.arr.push_back(S(FString(Languages[I].Name)));
                    if (Active.Equals(Languages[I].Code, ESearchCase::IgnoreCase))
                        CurrentLang = I;
                }
                FProps Props;
                Props["action"] = S("language.set");
                Props["label"] = S(Born2Flap::I18n::T("settings.language_hint"));
                Props["options"] = std::move(LangOptions);
                Props["value"] = N(CurrentLang);
                Rows.push_back(Nd("Dropdown", std::move(Props)));
            }

            // DISPLAY / CAMERA
            Rows.push_back(Nd("TextBlock", P({ {"text", S("DISPLAY  /  CAMERA")}, {"tone", S("accent")}, {"size", S("l")} })));
            {
                FProps Props;
                Props["action"] = S("camera.fpv");
                Props["label"] = S("AIRBORNE CAMERA");
                Props["on"] = S("FPV  /  click for chase");
                Props["off"] = S("CHASE  /  click for FPV");
                Props["value"] = B(Settings.bFpvAirView);
                Rows.push_back(Nd("Toggle", std::move(Props)));
            }
            {
                FProps Props;
                Props["action"] = S("camera.angle");
                Props["label"] = S("FPV CAMERA ANGLE");
                Props["tooltip"] = S("Fixed pitch of the onboard FPV lens. Negative = look down; positive = look up.");
                Props["value"] = N(Settings.FpvCameraAngleDeg);
                Props["min"] = N(-45);
                Props["max"] = N(45);
                Props["step"] = N(1);
                Props["unit"] = S(FString(TEXT("°")));
                Rows.push_back(Nd("Slider", std::move(Props)));
            }

            // AUDIO
            Rows.push_back(Nd("TextBlock", P({ {"text", S("AUDIO")}, {"tone", S("accent")}, {"size", S("l")} })));
            Rows.push_back(Nd("TextBlock", P({ {"text", S(FString(TEXT("Raises only the wing-flap voice — wind, surf and music stay untouched.")))}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
            {
                FProps Props;
                Props["action"] = S("audio.wingbeat");
                Props["label"] = S("WINGBEAT VOLUME");
                Props["tooltip"] = S("Only the wingbeat gets louder/quieter (1× = default, up to 4×). Wind, ocean and music are unaffected.");
                Props["value"] = N(Settings.WingbeatVolume);
                Props["min"] = N(0);
                Props["max"] = N(4);
                Props["step"] = N(0.05);
                Props["unit"] = S(FString(TEXT("×")));
                Rows.push_back(Nd("Slider", std::move(Props)));
            }
            {
                // RADIO VOLUME — the field radio's music level. M mutes/unmutes
                // in-game (it flips this same setting), [ / ] nudge it live.
                if (ABorn2FlapGameMode* GM = Cast<ABorn2FlapGameMode>(UGameplayStatics::GetGameMode(this)))
                {
                    if (UBorn2FlapRadioStation* Radio = GM->GetRadioStation())
                    {
                        FProps Props;
                        Props["action"] = S("audio.radio");
                        Props["label"] = S("RADIO VOLUME");
                        Props["tooltip"] = S("Field-radio music level. M mutes/unmutes it in-game; [ and ] nudge it up or down.");
                        Props["value"] = N(Radio->GetUserVolume());
                        Props["min"] = N(0);
                        Props["max"] = N(1);
                        Props["step"] = N(0.05);
                        Props["unit"] = S(FString(TEXT("×")));
                        Rows.push_back(Nd("Slider", std::move(Props)));
                    }
                }
            }

            // INPUT / CONTROLS
            Rows.push_back(Nd("TextBlock", P({ {"text", S("INPUT  /  CONTROLS")}, {"tone", S("accent")}, {"size", S("l")} })));
            Rows.push_back(Nd("TextBlock", P({ {"text", S("Negative reverses direction. Zero disables that mouse axis. Magnitude sets sensitivity.")}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                FProps Props;
                Props["action"] = S(FString::Printf(TEXT("mouse.gain.%d"), Axis));
                Props["label"] = S(FString(MouseNames[Axis]));
                Props["tooltip"] = S("Negative values reverse the axis. Zero disables it. Larger magnitudes increase sensitivity.");
                Props["value"] = N(Settings.MouseGains[Axis]);
                Props["min"] = N(-2);
                Props["max"] = N(2);
                Props["step"] = N(0.05);
                Props["unit"] = S(FString(TEXT("×")));
                MouseGainSliders.Add(RowPath((int32)Rows.size()));
                Rows.push_back(Nd("Slider", std::move(Props)));
            }
            {
                FProps Props;
                Props["action"] = S("mouse.reset");
                Props["label"] = S("Reset mouse response");
                Rows.push_back(Nd("Button", std::move(Props)));
            }
            {
                FProps Props;
                Props["action"] = S("control.expo");
                Props["label"] = S("EXPO");
                Props["tooltip"] = S("Softens stick/mouse response around centre. Low = linear/direct; high = very gentle centre, more throw near the edges.");
                Props["value"] = N(Settings.ControlExpo * 100.0);
                Props["min"] = N(0);
                Props["max"] = N(100);
                Props["step"] = N(1);
                Props["unit"] = S("%");
                Rows.push_back(Nd("Slider", std::move(Props)));
            }
            {
                FProps Props;
                Props["action"] = S("mouse.speed");
                Props["label"] = S("MOUSE SPEED");
                Props["tooltip"] = S("Mouse-to-flap speed mapping. Low = slow/gentle; high = fast and direct.");
                Props["value"] = N(Settings.SpeedModifier);
                Props["min"] = N(0);
                Props["max"] = N(1);
                Props["step"] = N(0.05);
                Props["unit"] = S(FString(TEXT("×")));
                Rows.push_back(Nd("Slider", std::move(Props)));
            }

            // REPLAY
            Rows.push_back(Nd("TextBlock", P({ {"text", S("REPLAY SHADOWS")}, {"tone", S("accent")}, {"size", S("l")} })));
            Rows.push_back(Nd("TextBlock", P({ {"text", S(FString(TEXT("Past flights fly alongside as grayed-out, translucent ghosts (gold = best round).")))}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
            {
                FProps Props;
                Props["action"] = S("replay.spirits");
                Props["label"] = S(FString(TEXT("REPLAY SHADOWS")));
                Props["on"] = S("ON  /  click to hide");
                Props["off"] = S("OFF  /  click to show");
                Props["value"] = B(Settings.bReplaySpirits);
                Rows.push_back(Nd("Toggle", std::move(Props)));
            }
            {
                FProps Props;
                Props["action"] = S("replay.delete");
                Props["label"] = S("Delete all replay shadows");
                Props["tone"] = S("danger");
                Rows.push_back(Nd("Button", std::move(Props)));
            }

            Out.push_back(Nd("ScrollBox", P({}), { Nd("VerticalBox", P({ {"spacing", N(12)} }), std::move(Rows)) }));
        }
    }
}

void ABorn2FlapMenu::RefreshMouseGains()
{
    if (!Renderer)
        return;
    for (int32 Axis = 0; Axis < 3 && Axis < MouseGainSliders.Num(); ++Axis)
    {
        FProps Props;
        Props["value"] = N(Settings.MouseGains[Axis]);
        SendUpdate(Renderer, MouseGainSliders[Axis], std::move(Props));
    }
}

void ABorn2FlapMenu::MarkServoCustom()
{
    if (ServoPresetIndex == -1 || !Renderer)
        return;
    ServoPresetIndex = -1;
    SendUpdate(Renderer, ServoSelectPath, P({ {"value", N(-1)} }));
    SendUpdate(Renderer, ServoStatusPath, P({ {"text", S("SERVO:  CUSTOM  /  manual")}, {"tone", S("dim")} }));
}

void ABorn2FlapMenu::Close()
{
    // Persist whatever the menu edited — on the home screen there is no bird to
    // save, so the settings store writes Config/FlightPreferences.ini directly.
    PersistSettings();
    if (Renderer)
        Renderer->Close();
}

void ABorn2FlapMenu::RestoreView(int32 Nav, int32 SettingsSub, int32 PrefsSub)
{
    const int32 N = FMath::Clamp(Nav, 0, 2);
    const int32 S = FMath::Clamp(SettingsSub, 0, 3);
    const int32 P = FMath::Clamp(PrefsSub, 0, 1);
    if (N != NavPage || S != SettingsPage || P != PrefsPage)
    {
        NavPage = N;
        SettingsPage = S;
        PrefsPage = P;
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
    // ---- top-level pages: OPEN WORLDS, FLIGHT DESK and SETTINGS share one
    // full-screen view; the nav rail switches between them, no back-and-forth ----
    if (Action == TEXT("menu.play"))         { NavPage = 0; Build(); return; }
    if (Action == TEXT("menu.settings"))     { NavPage = 1; Build(); return; }
    if (Action == TEXT("menu.preferences"))  { NavPage = 2; PrefsPage = 0; Build(); return; }
    if (Action == TEXT("prefs.page"))        { PrefsPage = FMath::Clamp(FMath::RoundToInt(Value), 0, 1); Build(); return; }
    if (Action == TEXT("settings.page"))     { SettingsPage = FMath::Clamp(FMath::RoundToInt(Value), 0, 3); Build(); return; }
    // OPEN WORLDS sub-page: LOCATION / WEATHER & TIME / ROUTE (segmented).
    if (Action == TEXT("menu.page"))         { SelectedPage = FMath::Clamp(FMath::RoundToInt(Value), 0, 2); Build(); return; }

    // ---- CONTROL SETTINGS: RC transmitter actions ----
    if (Action.StartsWith(TEXT("rc.")))
    {
        if (FBorn2FlapRcController* Rc = ActiveRc())
        {
            if      (Action == TEXT("rc.device.next"))       Rc->NextDevice();
            else if (Action == TEXT("rc.calibrate.start"))   Rc->StartCalibration();
            else if (Action == TEXT("rc.calibrate.advance")) Rc->AdvanceCalibration();
            else if (Action == TEXT("rc.calibrate.cancel"))  Rc->CancelCalibration();
            else if (Action == TEXT("rc.enable.toggle"))     Rc->ToggleEnabled();
            else if (Action == TEXT("rc.learn.launch"))      Rc->LearnLaunch();
            else if (Action == TEXT("rc.learn.reset"))       Rc->LearnReset();
        }
        Build();
        return;
    }

    // ---- OPEN WORLDS page ----
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
        PersistSettings();
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
            PersistSettings();
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

    // ---- FLIGHT DESK / GENERAL SETTINGS: settings-store backed ----
    // Editable on the home screen too; a live bird mirrors the change immediately
    // (and re-reads the same ini when it spawns in a world).
    if (Action == TEXT("settings.close"))
    {
        if (ABorn2FlapGameMode* GM = Cast<ABorn2FlapGameMode>(UGameplayStatics::GetGameMode(this)))
            GM->CloseMainMenu();
        return;
    }
    if (Action == TEXT("bird.model"))
    {
        Settings.BirdModel = FMath::Clamp(FMath::RoundToInt(Value), 0, 2);
        Settings.CgOffsetMm = FMath::Clamp(Settings.CgOffsetMm, -CgRangeMm(), CgRangeMm());
        if (Bird.IsValid()) Bird->SelectBirdModel(Settings.BirdModel);
        Build();   // CG slider min/max scale with the new craft's length
        return;
    }
    if (Action == TEXT("camera.fpv")) { Settings.bFpvAirView = !Settings.bFpvAirView; if (Bird.IsValid()) Bird->ToggleFpvView(); return; }
    if (Action == TEXT("camera.angle")) { Settings.FpvCameraAngleDeg = FMath::Clamp(Value, -45.f, 45.f); if (Bird.IsValid()) Bird->SetFpvCameraAngle(Settings.FpvCameraAngleDeg); return; }
    if (Action == TEXT("replay.spirits")) { Settings.bReplaySpirits = Value > 0.5f; if (Bird.IsValid()) Bird->SetReplaySpiritsEnabled(Settings.bReplaySpirits); return; }
    if (Action == TEXT("replay.delete")) { if (Bird.IsValid()) Bird->DeleteReplaySpirits(); return; }
    if (Action == TEXT("language.set"))
    {
        const int32 Idx = FMath::Clamp(FMath::RoundToInt(Value), 0, LanguageCount - 1);
        Settings.Language = Languages[Idx].Code;
        Born2Flap::I18n::SetLocale(Settings.Language);
        PersistSettings();
        Build();   // re-render so the nav rail, pane title and localized labels switch
        return;
    }
    if (Action == TEXT("bird.weight")) { Settings.BodyMassKg = FMath::Clamp(Value / 1000.f, 0.01f, 2.0f); if (Bird.IsValid()) Bird->SetBodyMassKg(Settings.BodyMassKg); return; }
    if (Action == TEXT("bird.cg")) { Settings.CgOffsetMm = FMath::Clamp(Value * 10.f, -CgRangeMm(), CgRangeMm()); if (Bird.IsValid()) Bird->SetCgOffsetMm(Settings.CgOffsetMm); return; }   // slider is in cm
    if (Action == TEXT("servo.preset"))
    {
        const int32 Idx = FMath::Clamp(FMath::RoundToInt(Value), 0, ServoPresetCount - 1);
        const FServoPreset& Sp = ServoPresets[Idx];
        SetTuningValue(ETuningField::ServoSpeed, Sp.SpeedDegS);
        SetTuningValue(ETuningField::StallTorque, Sp.StallTorqueNm);
        SetTuningValue(ETuningField::Backdrive, Sp.Backdrive);
        SetTuningValue(ETuningField::BatteryVoltage, Sp.Voltage);
        ServoPresetIndex = Idx;
        ApplySettingsToBird();
        Build();
        return;
    }
    if (Action.StartsWith(TEXT("mouse.gain.")))
    {
        const int32 Axis = FCString::Atoi(*Action.RightChop(FCString::Strlen(TEXT("mouse.gain."))));
        if (Axis >= 0 && Axis < 3) { Settings.MouseGains[Axis] = FMath::Clamp(Value, -2.f, 2.f); if (Bird.IsValid()) Bird->SetMouseGain(Axis, Settings.MouseGains[Axis]); }
        return;
    }
    if (Action == TEXT("mouse.reset"))
    {
        Settings.MouseGains = FVector(1.f, -1.f, 1.f);
        if (Bird.IsValid()) for (int32 Axis = 0; Axis < 3; ++Axis) Bird->SetMouseGain(Axis, Settings.MouseGains[Axis]);
        RefreshMouseGains();
        return;
    }
    if (Action == TEXT("control.expo")) { Settings.ControlExpo = FMath::Clamp(Value / 100.f, 0.f, 1.f); if (Bird.IsValid()) Bird->SetControlExpo(Settings.ControlExpo); return; }
    if (Action == TEXT("flight.safety")) { Settings.FlightSafety = FMath::Clamp(Value, 0.f, 2.f); if (Bird.IsValid()) Bird->SetFlightSafety(Settings.FlightSafety); return; }
    if (Action == TEXT("audio.wingbeat")) { Settings.WingbeatVolume = FMath::Clamp(Value, 0.f, 4.f); if (Bird.IsValid()) Bird->SetWingbeatVolume(Settings.WingbeatVolume); return; }
    if (Action == TEXT("audio.radio"))
    {
        if (ABorn2FlapGameMode* GM = Cast<ABorn2FlapGameMode>(UGameplayStatics::GetGameMode(this)))
            if (UBorn2FlapRadioStation* Radio = GM->GetRadioStation())
                Radio->SetVolume(Value);
        return;
    }
    if (Action == TEXT("replay.spirits")) { Settings.bReplaySpirits = Value > 0.5f; if (Bird.IsValid()) Bird->SetReplaySpiritsEnabled(Settings.bReplaySpirits); return; }
    if (Action == TEXT("replay.delete")) { if (Bird.IsValid()) Bird->DeleteReplaySpirits(); return; }

    const ETuningField Field = born2flap::tuning::FromKey(Action);
    if (Field != ETuningField::Count)
    {
        SetTuningValue(Field, born2flap::tuning::FromDisplayValue(Field, Value));
        if (Bird.IsValid()) Bird->SetTuning(Field, TuningValue(Field));
        if (Field == ETuningField::ServoSpeed || Field == ETuningField::StallTorque ||
            Field == ETuningField::Backdrive || Field == ETuningField::BatteryVoltage)
            MarkServoCustom();
        return;
    }

}