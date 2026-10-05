// Born2FlapMenu.cpp — the semantic main-menu / level-selector implementation.

#include "UI/Born2FlapMenu.h"

#include "UI/Born2FlapUIRenderer.h"
#include "UI/Born2FlapUiOps.h"
#include "UI/Born2FlapI18n.h"
#include "Game/Born2FlapGameMode.h"
#include "Game/Born2FlapPoi.h"
#include "Flight/Born2FlapFlightPawn.h"
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

// Open worlds: shown one at a time in the chooser. Each carries a short fictional
// vignette so the player can read about the place before flying there.
struct FWorldDef { const TCHAR* Id; const TCHAR* Title; const TCHAR* Story; };
const FWorldDef Worlds[] = {
    { TEXT("Ravenstonefield"), TEXT("RAVENSTONEFIELD"),
      TEXT("A highland valley of black basalt columns rising from amber bracken. Ravens ride the thermals between the spires, and the wind carries heather and cold stone. An old ornithopter field, flown by generations of wing-builders.") },
    { TEXT("Shiomori"), TEXT("SHIOMORI BAY"),
      TEXT("A sheltered bay where the low winter sun splits into three, its parhelion false suns hanging on either side of the true one. Salt spray, dark water and a long pale beach. Only here does the ice-halo weather reveal itself.") },
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
        Renderer->OnComponentAction.Broadcast(TEXT("weather.Shiomori"),2.f,TEXT(""));
        Renderer->OnComponentAction.Broadcast(TEXT("daytime.Shiomori"),3.f,TEXT(""));
        const auto C=LevelConditions.FindChecked(TEXT("Shiomori"));
        const bool Pass=C.Weather==TEXT("rain") && C.Time==TEXT("sunset") &&
            !Born2FlapWeather::WeatherOptions(TEXT("Training")).Contains(TEXT("snow"));
        UE_LOG(LogTemp,Display,TEXT("WeatherMenuTest %s: selection relays and per-level options"),Pass ? TEXT("PASS") : TEXT("FAIL"));
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[](float){
            FScreenshotRequest::RequestScreenshot(TEXT("WEATHER_MENU.png"),true,false);return false;
        }),2.f);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float){
            OnAction(TEXT("menu.shiomori"),1.f,TEXT(""));return false;
        }),4.f);
    }
}

void ABorn2FlapMenu::Build()
{
    if (!Renderer)
        return;

    // Full-bleed menu artwork with a centred card: the world browser (the level
    // selector, shown first) or the main menu (title + OPEN WORLDS/QUIT).
    //   []                 Overlay (root, children fill)
    //   [0]                Image (born2flap-background, full-bleed)
    //   [1]                Overlay (align center, valign center)
    //   [1,0]              Panel (dark scrim card)
    //   [1,0,...]          screen-specific content
    std::vector<FNode> CardChildren;
    if (bLevelSelect)
    {
        // One open world at a time: a story card for the selected world with its
        // weather/time conditions, a FLY button, and PREV/NEXT to page between
        // the open worlds. The worlds are never all listed on one screen.
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

        CardChildren.push_back(Nd("Banner", P({ {"text", S(WD.Title)}, {"tone", S("accent")}, {"size", S("xl")} })));
        const FString Preview = Level == TEXT("Shiomori") ? TEXT("Coast") : Level == TEXT("Ravenstonefield") ? TEXT("Valley") : TEXT("Islands");
        CardChildren.push_back(Nd("HorizontalBox", P({{"spacing",N(18)}}), {
            Nd("SizeBox",P({{"width",N(234)},{"height",N(128)}}),{
                Nd("Image",P({{"texture",S(TEXT("/Game/UI/Elements/")+Preview+TEXT(".")+Preview)}}))}),
            Nd("SizeBox",P({{"width",N(420)}}),{
                Nd("TextBlock", P({ {"text", S(WD.Story)}, {"size", S("m")}, {"wrap", B(true)} }))})
        }));
        CardChildren.push_back(Nd("Spacer", P({})));
        CardChildren.push_back(Nd("Select", P({ {"label", S("Weather")}, {"options", WeatherLabels}, {"value", N(Weathers.IndexOfByKey(C.Weather))}, {"action", S(TEXT("weather.") + Level)} })));
        CardChildren.push_back(Nd("Select", P({ {"label", S("Time of day")}, {"options", TimeLabels}, {"value", N(Times.IndexOfByKey(C.Time))}, {"action", S(TEXT("daytime.") + Level)} })));
        CardChildren.push_back(Nd("TextBlock", P({ {"text", S(FString::Printf(TEXT("Wind %.1f m/s | %s"), Speed, W.Gust < .5f ? TEXT("gentle gusts") : TEXT("variable breeze")))}, {"size", S("s")} })));
        // Travel destinations: a scrollable list of this world's points of
        // interest. Choosing one makes it the reset start point the FLY button
        // travels to. It replaces the old in-world beacon/cycling UI.
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
        CardChildren.push_back(Nd("TextBlock", P({ {"text", S("TRAVEL TO")}, {"size", S("xs")}, {"tone", S("dim")} })));
        CardChildren.push_back(Nd("SizeBox", P({ {"height", N(150)} }),
        {
            Nd("ScrollBox", P({}),
            {
                Nd("VerticalBox", P({}), std::move(PoiButtons))
            })
        }));
        CardChildren.push_back(Nd("Button", P({ {"label", S(FString(TEXT("FLY  ")) + WD.Title)}, {"action", S(TEXT("menu.") + Level.ToLower())}, {"tone", S("good")} })));

        CardChildren.push_back(Nd("Spacer", P({})));
        CardChildren.push_back(Nd("TextBlock", P({ {"text", S(FString::Printf(TEXT("WORLD %d OF %d"), SelectedWorld + 1, WorldCount))}, {"size", S("xs")}, {"align", S("center")}, {"tone", S("dim")} })));
        CardChildren.push_back(Nd("HorizontalBox", P({}),
        {
            Nd("Button", P({ {"label", S("PREV")}, {"action", S("menu.prevWorld")} })),
            Nd("Button", P({ {"label", S("NEXT")}, {"action", S("menu.nextWorld")} })),
        }));
        if (bInLevel)
            CardChildren.push_back(Nd("Button", P({ {"label", S(Born2Flap::I18n::T("menu.resume"))}, {"action", S("menu.resume")} })));
        CardChildren.push_back(Nd("Button", P({ {"label", S(Born2Flap::I18n::T("menu.back"))}, {"action", S("menu.back")} })));
    }
    else
    {
        CardChildren = {
            Nd("Banner", P({ {"text", S(Born2Flap::I18n::T("menu.title"))}, {"tone", S("accent")}, {"size", S("xxl")} })),
            Nd("TextBlock", P({ {"text", S(Born2Flap::I18n::T("menu.subtitle"))}, {"size", S("m")}, {"align", S("center")} })),
            Nd("Spacer", P({})),
        };
        if (bInLevel)
        {
            CardChildren.push_back(Nd("Button", P({ {"label", S(Born2Flap::I18n::T("menu.resume"))}, {"action", S("menu.resume")} })));
            CardChildren.push_back(Nd("Button", P({ {"label", S("FLIGHT DESK")}, {"action", S("menu.settings")} })));
        }
        CardChildren.push_back(Nd("Button", P({ {"label", S("OPEN WORLDS")}, {"action", S("menu.play")} })));
        CardChildren.push_back(Nd("Button", P({ {"label", S(Born2Flap::I18n::T("menu.quit"))}, {"action", S("menu.quit")} })));
    }

    FNode Root = Nd("Overlay", P({}),
    {
        Nd("Image", P({ {"texture", S("/Game/Splash/born2flap-background.born2flap-background")} })),
        Nd("Overlay", P({ {"align", S("center")}, {"valign", S("center")} }),
        {
            Nd("SizeBox",P({{"width",N(bLevelSelect ? 760 : 480)}}),{
                Nd("Panel", P({ {"bg", S("solid")}, {"padding", N(30)}, {"spacing", N(10)} }), std::move(CardChildren))})
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

void ABorn2FlapMenu::Close()
{
    if (Renderer)
        Renderer->Close();
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
    // RESUME (only shown when the menu was opened from inside a level): close
    // the menu and return to the running flight.
    if (Action == TEXT("menu.resume"))
    {
        if (ABorn2FlapGameMode* GM = Cast<ABorn2FlapGameMode>(UGameplayStatics::GetGameMode(this)))
            GM->CloseMainMenu();
        return;
    }

    // FLIGHT DESK (only shown in-level): leave the menu and open the settings
    // panel. It is now reached from here — F8 is the POI overlay, not settings.
    if (Action == TEXT("menu.settings"))
    {
        if (ABorn2FlapGameMode* GM = Cast<ABorn2FlapGameMode>(UGameplayStatics::GetGameMode(this)))
            GM->CloseMainMenu();
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->OpenFlightSettings();
        return;
    }

    // Main menu <-> level selection.
    if (Action == TEXT("menu.play"))
    {
        bLevelSelect = true;
        Build();
        return;
    }
    if (Action == TEXT("menu.back"))
    {
        bLevelSelect = false;
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
    if (Action == TEXT("menu.quit"))
    {
        UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
        return;
    }

    // Route a level button to its map. ?Level= drives the level flags in
    // InitGame; ?SkipMenu=1 keeps the menu from re-opening on the destination.
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
            UGameplayStatics::OpenLevel(this, E.Map, true, Options);
            return;
        }
    }
}
