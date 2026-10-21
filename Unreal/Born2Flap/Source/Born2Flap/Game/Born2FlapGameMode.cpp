#include "Game/Born2FlapGameMode.h"
#include "Game/Born2FlapHarvestWatch.h"
#include "Born2Flap.h"
#include "World/Born2FlapBayTerrain.h"
#include "Game/Born2FlapHUD.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Flight/Born2FlapTuning.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "World/Born2FlapValley.h"
#include "World/Born2FlapWind.h"
#include "World/Born2FlapWindLeaves.h"
#include "World/Born2FlapInsectSwarm.h"
#include "World/Born2FlapNeonVoid.h"
#include "UI/Born2FlapUIBridge.h"
#include "UI/Born2FlapFlightHUD.h"
#include "UI/Born2FlapRadio.h"
#include "UI/Born2FlapI18n.h"
#include "UI/Born2FlapSplash.h"
#include "UI/Born2FlapLoading.h"
#include "UI/Born2FlapMenu.h"
#include "UI/Born2FlapUiTheme.h"
#include "Racing/Born2FlapRacing.h"
#include "Water/Born2FlapWaterDirector.h"
#include "Water/Born2FlapSurf.h"
#include "Rendering/Born2FlapGraphics.h"
#include "Audio/Born2FlapSoundscape.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Input/Born2FlapKeybinds.h"
#include "Blueprint/UserWidget.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"
#include "Platform/Born2FlapPlatformSubsystem.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Input/Born2FlapRcController.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
// Captures F10 while the main menu is open. The menu runs in FInputModeUIOnly,
// which routes keyboard input away from PlayerInput, so the GameMode's
// WasInputKeyJustPressed cannot see it. This pre-processor sees every key-down
// before that routing and defers the close to the GameMode Tick (the same
// pattern as the flight-desk panel's FPanelKeyInputProcessor).
class FMenuKeyInputProcessor : public IInputProcessor
{
public:
    bool* bMenuOpen = nullptr;

    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
    {
        if (!bMenuOpen || !*bMenuOpen)
            return false;
        if (InKeyEvent.IsRepeat())
            return false;
        if (InKeyEvent.GetKey() == born2flap::keybinds::Get(TEXT("ToggleMenu")))
        {
            bF10Pressed = true;
            return true;  // consume: the menu closes on this key, nothing else sees it
        }
        return false;
    }

    bool ConsumeF10() { const bool v = bF10Pressed; bF10Pressed = false; return v; }

private:
    bool bF10Pressed = false;
};
}  // namespace

// Open-world catalog for the Brain-authored menu (mirrors ABorn2FlapMenu's
// Worlds[]). `Id` is the level key, `Title`/`Story` the card copy.
namespace
{
struct FMenuWorld { const TCHAR* Id; const TCHAR* Title; const TCHAR* Story; };
const FMenuWorld MenuWorlds[] = {
    { TEXT("Ravenstonefield"), TEXT("RAVENSTONEFIELD"),
      TEXT("A highland valley of black basalt columns rising from amber bracken, cut by cold streams that silver in the low light. Ravens ride the thermals between the spires, and the wind carries heather, wet stone and the smell of old iron. At the valley's mouth sits an ornithopter field flown by generations of wing-builders — a meadow workshop, a raven roost and a wind-vane that has pointed the same way for a century. Fly the meadow, thread the basalt, and learn what the valley teaches about lift.") },
    { TEXT("Shiomori"), TEXT("SHIOMORI BAY"),
      TEXT("A broad pale beach arcing around a sheltered bay of dark, glassy water, where the tide draws a slow walk of basalt out toward hazy, sunlit hills. Salt-cured rock and wind-bent grass give way inland to a pine-draped headland and a university campus that clings to the cliff above the surf. It is quiet enough that you notice what the sky is doing — and what the sea is answering. Follow the shoreline, skim the terrace roofs, and ride the bay's offshore wind.") },
    { TEXT("Training"), TEXT("TRAINING"),
      TEXT("A flat grass course marked with floating sky-gates, ringed by the hum of a weather station and the chalk lines of a hundred practice rounds. This is the field where every pilot learns to fold the wing, hold the line and thread the gates against the clock. Start on the launch pad, hit the gates in order, and mind the wind arrow — the course rewards a clean line more than a fast one.") },
    { TEXT("NeonAbyss"), TEXT("NEON ABYSS"),
      TEXT("A primordial, eternal sanctuary that has always floated here — a void of soft nebulae and ringed planets hung under a circle of ancient bronzes, their verdigris seams opening to gold as you draw near. There is no ground and no horizon: a baby turtle swims the aether, chasing neon jellyfish that flutter like insects. Flap to pulse forward, glide to coast, and net the festival lights before the timer ends.") },
};
constexpr int32 MenuWorldCount = 4;
}  // namespace

ABorn2FlapGameMode::ABorn2FlapGameMode()
{
    DefaultPawnClass = ABorn2FlapFlightPawn::StaticClass();
    HUDClass = ABorn2FlapHUD::StaticClass();
    PrimaryActorTick.bCanEverTick = true;
}
void ABorn2FlapGameMode::InitGame(const FString &MapName, const FString &Options, FString &ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    // Locale: -B2FLang=de or ?Lang=de (English is the default, and the fallback).
    FString Lang;
    FParse::Value(FCommandLine::Get(), TEXT("B2FLang="), Lang);
    if (Lang.IsEmpty())
        Lang = UGameplayStatics::ParseOption(Options, TEXT("Lang"));
    if (!Lang.IsEmpty())
        Born2Flap::I18n::SetLocale(Lang);

    FString Level = UGameplayStatics::ParseOption(Options, TEXT("Level"));
    if (Level.IsEmpty())
        FParse::Value(FCommandLine::Get(), TEXT("B2FLevel="), Level);
    bCoastLevel = Level.Equals(TEXT("Shiomori"),ESearchCase::IgnoreCase) || (Level.IsEmpty() && MapName.Contains(TEXT("SHIOMORI")));
    FString Activity=UGameplayStatics::ParseOption(Options,TEXT("Activity"));
    if(Activity.IsEmpty())FParse::Value(FCommandLine::Get(),TEXT("B2FActivity="),Activity);
    bHarvestWatch=bCoastLevel && (Activity.Equals(TEXT("HarvestWatch"),ESearchCase::IgnoreCase) || FParse::Param(FCommandLine::Get(),TEXT("B2FHarvestTest")) || FParse::Param(FCommandLine::Get(),TEXT("B2FHarvestCapture")));
    bNatureLevel = !Level.Equals(TEXT("Training"), ESearchCase::IgnoreCase) &&
                   !FParse::Param(FCommandLine::Get(), TEXT("B2FFlightTest")) &&
                   !FParse::Param(FCommandLine::Get(), TEXT("B2FSoakTest")) &&
                   !FParse::Param(FCommandLine::Get(), TEXT("B2FHandlingTest"));
    bVoidLevel = Level.Equals(TEXT("NeonAbyss"), ESearchCase::IgnoreCase) || (Level.IsEmpty() && MapName.Contains(TEXT("NEONABYSS")));
    if (bVoidLevel) { bCoastLevel = false; bNatureLevel = false; }
    // Menu mode: a plain interactive boot into the minimal Entry map (no
    // explicit level) is the home screen, NOT a playable level. Nothing is
    // built and no bird spawns behind the menu — a real level only loads when
    // the player selects a world and clicks FLY (which travels with ?Level=
    // and ?SkipMenu=1). Automated/test runs are always -unattended, so they
    // never trip this and keep booting straight into their test map.
    bMenuMode = Level.IsEmpty() &&
                MapName.Contains(TEXT("Entry"), ESearchCase::IgnoreCase) &&
                (!FApp::IsUnattended() || FParse::Param(FCommandLine::Get(),TEXT("B2FRubyUITest")));
    if (bMenuMode)
    {
        bCoastLevel = false;
        bNatureLevel = false;
        bVoidLevel = false;
        DefaultPawnClass = nullptr;
        HUDClass = nullptr;
    }
    // The main menu only appears on a plain boot (no explicit level, no test
    // mode). An explicit level request — ?Level= / -B2FLevel= (play.ps1 passes
    // one for a -Level boot), ?SkipMenu=1 (set by the menu itself) or -B2FNoMenu — keeps
    // it hidden so the player lands directly in a playable map. Inside a level
    // the Escape shortcut re-opens it on demand.
    bSkipMenu = FParse::Param(FCommandLine::Get(), TEXT("B2FNoMenu")) ||
                UGameplayStatics::ParseOption(Options, TEXT("SkipMenu")).Equals(TEXT("1"), ESearchCase::IgnoreCase) ||
                !Level.IsEmpty();
    Conditions=FApp::IsUnattended() ? Born2FlapWeather::Defaults(GetLevelId()) : Born2FlapWeather::Load(GetLevelId());
    FString Weather=UGameplayStatics::ParseOption(Options,TEXT("Weather"));
    FString DayTime=UGameplayStatics::ParseOption(Options,TEXT("DayTime"));
    FString Season=UGameplayStatics::ParseOption(Options,TEXT("Season"));
    InitialPoiKey=bHarvestWatch?TEXT("Harvest Watch - field launch"):UGameplayStatics::ParseOption(Options,TEXT("PoiKey"));
    InitialPoiNumber=FCString::Atoi(*UGameplayStatics::ParseOption(Options,TEXT("PoiNum")));
    if(Weather.IsEmpty()) FParse::Value(FCommandLine::Get(),TEXT("B2FWeather="),Weather);
    if(DayTime.IsEmpty()) FParse::Value(FCommandLine::Get(),TEXT("B2FDayTime="),DayTime);
    if(Season.IsEmpty()) FParse::Value(FCommandLine::Get(),TEXT("B2FSeason="),Season);
    if(!Weather.IsEmpty()) Conditions.Weather=Weather.ToLower();
    if(!DayTime.IsEmpty()) Conditions.Time=DayTime.ToLower();
    if(!Season.IsEmpty()) Conditions.Season=Season.ToLower();
    // Wind overrides (set by the menu's wind sliders). Empty = "auto" (the
    // weather/time profile defaults); Load() above already seeded the persisted
    // values, so these URL options only layer explicit overrides on top.
    FString WindDir=UGameplayStatics::ParseOption(Options,TEXT("WindDir"));
    FString WindSpeed=UGameplayStatics::ParseOption(Options,TEXT("WindSpeed"));
    FString WindGust=UGameplayStatics::ParseOption(Options,TEXT("WindGust"));
    FString WindRandom=UGameplayStatics::ParseOption(Options,TEXT("WindRandom"));
    FString WindThermal=UGameplayStatics::ParseOption(Options,TEXT("WindThermal"));
    if(!WindDir.IsEmpty())     Conditions.WindDir=(float)FCString::Atof(*WindDir);
    if(!WindSpeed.IsEmpty())   Conditions.WindSpeed=(float)FCString::Atof(*WindSpeed);
    if(!WindGust.IsEmpty())    Conditions.WindGust=(float)FCString::Atof(*WindGust);
    if(!WindRandom.IsEmpty())  Conditions.WindRandom=(float)FCString::Atof(*WindRandom);
    if(!WindThermal.IsEmpty()) Conditions.WindThermal=(float)FCString::Atof(*WindThermal);
    Conditions=Born2FlapWeather::Validate(GetLevelId(),Conditions);
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FWeatherMenuTest")) &&
       !UGameplayStatics::ParseOption(Options,TEXT("Weather")).IsEmpty())
    {
        const bool Pass=GetLevelId()==TEXT("Shiomori") && Conditions.Weather==TEXT("rain") && Conditions.Time==TEXT("sunset") && bSkipMenu;
        UE_LOG(LogTemp,Display,TEXT("WeatherMenuTravel %s: level=%s weather=%s time=%s"),
            Pass ? TEXT("PASS") : TEXT("FAIL"),*GetLevelId(),*Conditions.Weather,*Conditions.Time);
    }
}
void ABorn2FlapGameMode::RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot)
{
    // No PlayerStart exists in the map, so FindPlayerStart falls back to
    // WorldSettings — whose transform is the world origin. Shiomori's beach
    // collision now covers (0,0,0) with natural dune berms, so an origin spawn
    // fails and leaves the player with no controllable bird (reads as a hang).
    // Spawn in the air instead; ResetFlight() re-places the bird anyway.
    if (!StartSpot || StartSpot->IsA<AWorldSettings>())
    {
        Super::RestartPlayerAtTransform(NewPlayer, FTransform(FVector(0, 0, 2500)));
        return;
    }
    Super::RestartPlayerAtPlayerStart(NewPlayer, StartSpot);
}
double ABorn2FlapGameMode::GroundHeight(double X, double Y) const
{
    if(bCoastLevel)
    {
        // Photoscans have cavities and tidal gaps: use their actual surface.
        if((X>-49500 && X<-28000 && Y>12000 && Y<40000) ||
           (FMath::Abs(X)>28000 && FMath::Abs(X)<36500 && Y>7000 && Y<14500))
        {
            FHitResult Hit;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(LavaGround),true);
            if(GetWorld()->LineTraceSingleByObjectType(Hit,FVector(X,Y,6500),FVector(X,Y,-2200),
                FCollisionObjectQueryParams(ECC_WorldStatic),Query) && Hit.GetActor() &&
                Hit.GetActor()->ActorHasTag(TEXT("ShiomoriVolcanic"))) return Hit.ImpactPoint.Z;
        }
        if(Born2FlapBay::Contains(X,Y)) return Born2FlapBay::Height(X,Y);
        if(FMath::Abs(X)<=75000 && Y>=-51000 && Y<=-3000) return 300;
        if(FMath::Abs(X)>45000 || Y<-3500) return -1750;
        if(Y>=-1200)
        {
            // Same continuous foreshore profile as create_shiomori_map.py:
            // rugged shoreline + natural X-only dune berms that fade to the
            // waterline. Strictly offshore-decreasing, so it stays monotonic.
            const double Shore=10000+520*FMath::Sin(X*.00030+1.7)+300*FMath::Sin(X*.00105+4.2)
                              +160*FMath::Sin(X*.0024+.6)+85*FMath::Sin(X*.0056+2.3)+45*FMath::Sin(X*.013+5.1);
            const double Dune=FMath::Max(20.,40.+50*FMath::Sin(X*.00029+1.2)+30*FMath::Sin(X*.00091+4.1)+15*FMath::Sin(X*.0023+.7)+8*FMath::Sin(X*.0047+2.9));
            const double D=Y-Shore;
            if(D<=-2500) return Dune;
            if(D<0) return Dune*(-D/2500.)-50*FMath::Square((D+2500)/2500);
            return FMath::Max(-1750.,-50-.04*D);
        }
        return FMath::Clamp(FMath::CeilToDouble((-Y-1200)/100.)*50.,0.,300.);
    }
    return bNatureLevel ? ABorn2FlapValley::GroundHeight(X, Y) : 0;
}
bool ABorn2FlapGameMode::IsWater(double X, double Y) const
{
    return bCoastLevel ? GroundHeight(X, Y) < -50 : bNatureLevel && ABorn2FlapValley::IsWater(X, Y);
}
double ABorn2FlapGameMode::WaterHeight() const { return bCoastLevel ? -50 : ABorn2FlapValley::WaterHeight; }
FVector ABorn2FlapGameMode::WindAt(const FVector& P,double Time) const
{
    return Born2FlapWeather::Wind(GetLevelId(),Conditions,P,Time);
}
void ABorn2FlapGameMode::BeginPlay()
{
    Super::BeginPlay();

    // Apply the persisted UI scale before any semantic UI is built (the menu
    // map and every level map run through here), so fonts, spacing and element
    // sizes scale from the very first frame instead of only after the menu opens.
    {
        born2flap::FMenuSettings BootSettings;
        born2flap::MenuSettingsLoad(BootSettings);
        born2flap::ui::theme::SetUIScale(BootSettings.UIScale);
    }

    born2flap::MenuSettingsLoad(BrainPreferences);
    if (bMenuMode) BrainRc = MakeUnique<FBorn2FlapRcController>();
    // Apply the saved renderer quality tier before any world geometry/atmosphere
    // spawns, so the very first frame is already in the right Lumen/volumetric
    // configuration (and HW ray tracing is armed before scene construction).
    Born2FlapGraphics::ApplySavedOrDefault();
    // F10-while-menu-open capture (see FMenuKeyInputProcessor). The pointer
    // is wired to bMenuOpen so the processor only consumes F10 when the menu is
    // actually on screen — otherwise Escape stays free for the F8 flight desk.
    {
        auto* Proc = new FMenuKeyInputProcessor();
        Proc->bMenuOpen = &bMenuOpen;
        MenuKeyProcessor = MakeShareable(Proc);
        if (FSlateApplication::IsInitialized())
            FSlateApplication::Get().RegisterInputPreProcessor(MenuKeyProcessor);
    }
    // Points of interest are level-specific and only need the level flags
    // (set in InitGame) plus the saved-map camera actors, so they can be built
    // before the level geometry branches below. Menu mode has no level.
    if (!bMenuMode)
        PopulatePOIs();

    // Menu world-select state (for the Brain-authored menu; the native
    // ABorn2FlapMenu keeps its own copy when the Brain is offline).
    for (int32 I = 0; I < MenuWorldCount; ++I)
        MenuLevelConditions.Add(MenuWorlds[I].Id, Born2FlapWeather::Load(MenuWorlds[I].Id));

    // Ruby Brain bridge: when the Brain process launches, it becomes the single
    // UMG host and native panels stay dormant (fallback when Ruby is absent).
    {
        FActorSpawnParameters BrainParams;
        BrainParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        BrainBridge = GetWorld()->SpawnActor<ABorn2FlapUIBridge>(FVector::ZeroVector, FRotator::ZeroRotator, BrainParams);
    }
    const bool bBrain = IsBrainActive();

    // No in-app startup splash — the only pre-UI splash is the native Windows
    // splash (Content/Splash/Splash.png, WindowsApplication.ShowSplash) shown
    // while the engine boots before the main window appears. SplashElapsed stays
    // at its -1 default, so the menu opens on the very first Tick.
    // Menu mode is the home screen: nothing to build behind it. Return now —
    // the first real level only loads when the player clicks FLY. No bird,
    // radio, weather or world geometry here.
    if (bMenuMode)
        return;
    UWorld *World = GetWorld();
    World->GetWorldSettings()->bForceNoPrecomputedLighting = true;
    // World assembly is synchronous here (inside LoadMap, before the loader
    // drops), so report the phases as they happen — the loading screen reads
    // this shared state every frame and shows it in place of the bare bar.
    Born2Flap::Loading::Report(0.10f, TEXT("ASSEMBLING WORLD"));
    // The glass cockpit is authored natively in C++ (ABorn2FlapFlightHUD) only
    // when the Ruby Brain is offline; otherwise the Brain authors it from
    // BuildBrainTelemetry() and this actor stays dormant.
    if (!bBrain)
    {
        FActorSpawnParameters CockpitParams;
        CockpitParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        World->SpawnActor<ABorn2FlapFlightHUD>(FVector::ZeroVector, FRotator::ZeroRotator, CockpitParams);
    }
    // Racing mode: replay-spirit recorder + manager. Records every round and
    // re-flies the past rounds as shadow doppelgängers (disabled in flight tests).
    {
        FActorSpawnParameters RacingParams;
        RacingParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if(!bHarvestWatch)World->SpawnActor<ABorn2FlapRacingManager>(FVector::ZeroVector, FRotator::ZeroRotator, RacingParams);
    }
    // Unified radio: one station + one semantic HUD drive every level. The
    // playlist is data-driven per level (Config/DefaultGame.ini).
    {
        RadioStation = NewObject<UBorn2FlapRadioStation>(this);
        const FString RadioLevel = bCoastLevel ? TEXT("Shiomori") : (bNatureLevel ? TEXT("Ravenstonefield") : (bVoidLevel ? TEXT("NeonAbyss") : TEXT("Training")));
        RadioStation->Initialize(World, RadioLevel);
        if (!bBrain && RadioStation->HasTrack())
        {
            FActorSpawnParameters RadioParams;
            RadioParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            RadioHUD = World->SpawnActor<ABorn2FlapRadioHUD>(FVector::ZeroVector, FRotator::ZeroRotator, RadioParams);
            if (RadioHUD)
                RadioHUD->SetStation(RadioStation);
        }
    }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    WeatherActor=World->SpawnActor<ABorn2FlapWeather>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
    WeatherActor->Configure(GetLevelId(),Conditions);
    World->SpawnActor<ABorn2FlapSoundscape>();
    // The manifold insect swarm: myriads of butterflies, dragonflies, moths,
    // mantids and brumming bummer bugs that flutter and dart around the pilot
    // on the outdoor levels (coast + valley). Skipped on the training course.
    if (bCoastLevel || bNatureLevel)
    {
        FActorSpawnParameters BugParams;
        BugParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        World->SpawnActor<ABorn2FlapInsectSwarm>(FVector::ZeroVector, FRotator::ZeroRotator, BugParams);
        // Wind-blown foliage rides the atmospheric field near the trees. Runs on
        // both the coast and the valley — wherever groves stand to shed leaves.
        World->SpawnActor<ABorn2FlapWindLeaves>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
    }
    if(bCoastLevel)
    {
        Born2Flap::Loading::Report(0.40f, TEXT("BUILDING COASTLINE"));
        World->SpawnActor<ABorn2FlapBayTerrain>();
        World->SpawnActor<ABorn2FlapSurf>();
        World->SpawnActor<ABorn2FlapHarvestWatch>();
    }
    if(bCoastLevel)
    {
        // Saved coastal map supplies geometry and atmosphere. The water
        // director owns the global ocean parameters + MPC binding for the
        // coast (spawned at runtime like the other managers).
        World->SpawnActor<AShiomoriWaterDirector>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
        return;
    }
    if (bNatureLevel)
    {
        Born2Flap::Loading::Report(0.40f, TEXT("BUILDING VALLEY"));
        // Ravenstonefield carries its own atmosphere and can be saved as a map.
        // Entry still supports the procedural fallback for old launch scripts.
        if (!TActorIterator<ABorn2FlapValley>(World))
            World->SpawnActor<ABorn2FlapValley>();
        return;
    }
    if (bVoidLevel)
    {
        Born2Flap::Loading::Report(0.40f, TEXT("BUILDING AETHER NURSERY"));
        // The saved void map supplies the cosmos backdrop, aether fog and the
        // Sanxingdui bronze ring. The runtime director owns the drift current,
        // the neon jellyfish and the capture festival.
        World->SpawnActor<ABorn2FlapNeonVoid>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
        return;
    }
    auto *Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 1000), FRotator(-35, -35, 0), Params);
    auto *Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetIntensity(50000);
    Light->SetLightColor(FLinearColor(1, .92f, .79f));
    Light->SetAtmosphereSunLight(true);
    Light->SetDynamicShadowDistanceMovableLight(bNatureLevel ? 90000 : 20000);
    auto *Atmosphere = World->SpawnActor<AActor>();
    auto *Air = NewObject<USkyAtmosphereComponent>(Atmosphere);
    Atmosphere->SetRootComponent(Air);
    Air->RegisterComponent();
    auto *Sky = World->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->SetRealTimeCapture(true);
    Sky->GetLightComponent()->SetIntensity(1.f);
    auto *Fog = World->SpawnActor<AExponentialHeightFog>();
    Fog->GetComponent()->SetFogDensity(bNatureLevel ? .007f : .003f);
    Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(.55f, .72f, .85f));
    auto *Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto *Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
    auto *Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto Part = [&](UStaticMesh *Mesh, FVector P, FVector Scale, FRotator Rot, const TCHAR *Palette,
                    bool Collision = false) {
        auto *Actor = World->SpawnActor<AStaticMeshActor>(P, Rot, Params);
        auto *Component = Actor->GetStaticMeshComponent();
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetStaticMesh(Mesh);
        Component->SetCollisionEnabled(Collision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        if (Collision)
            Component->SetCollisionResponseToAllChannels(ECR_Block);
        const FString Path = FString(TEXT("/Game/Training/M_")) + Palette;
        if (auto *Material = LoadObject<UMaterialInterface>(nullptr, *Path))
            Component->SetMaterial(0, Material);
        Actor->SetActorScale3D(Scale);
        // Set the actor gate after mesh/scale changes too. Recreating the
        // mesh physics state can otherwise restore its BlockAll profile.
        Component->SetCollisionProfileName(Collision ? TEXT("BlockAll") : TEXT("NoCollision"));
        Actor->SetActorEnableCollision(Collision);
    };
    // A 10 km floor permits unassisted RC flight beyond the practice course.
    Part(Cube, FVector(0, 0, -100), FVector(10000, 10000, 2), FRotator::ZeroRotator, TEXT("Grass"), true);
    Part(Cube, FVector(0, 0, .5), FVector(9, 9, .01), FRotator::ZeroRotator, TEXT("Sand"));
    for (int I = 0; I < 12; ++I)
        Part(Cube, FVector(500 + I * 220, 0, 1), FVector(.9, .15, .015), FRotator::ZeroRotator, TEXT("Ivory"));
    Gates = {FVector(3000, 0, 400),      FVector(6000, 0, 650),      FVector(9000, 0, 900),
             FVector(12000, 1500, 1100), FVector(13500, 4500, 1000), FVector(12000, 7500, 800)};
    // Painted sky gates: the user's artwork floats in the sky as a non-colliding,
    // two-sided billboard. Each state carries its own painted graphics — red for the
    // active gate, green once passed, and quasi/ghost rings for everything still
    // ahead — so no manual tinting is applied. The texture is swapped per gate via
    // the dynamic material's GateTex parameter.
    auto *Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    auto *GateMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/UI/M_Gate"));
    auto LoadTex = [](const TCHAR *Path) { return LoadObject<UTexture2D>(nullptr, Path); };
    GateRedTextures.Reset();
    GateGreenTextures.Reset();
    GateQuasiTextures.Reset();
    for (int I = 1; I <= 4; ++I)
        GateRedTextures.Add(LoadTex(*FString::Printf(TEXT("/Game/UI/born2flap-red-gate-%d"), I)));
    for (int I = 1; I <= 3; ++I)
        GateGreenTextures.Add(LoadTex(*FString::Printf(TEXT("/Game/UI/born2flap-green-gate-%d"), I)));
    for (int I = 1; I <= 4; ++I)
        GateQuasiTextures.Add(LoadTex(*FString::Printf(TEXT("/Game/UI/born2flap-quasi-gate-%d"), I)));

    GateActors.Reset(Gates.Num());
    GateMaterials.Reset(Gates.Num());
    GateRedVariant.Reset(Gates.Num());
    GateGreenVariant.Reset(Gates.Num());
    FRandomStream GateRand(20261005);
    for (const FVector &Centre : Gates)
    {
        auto *Actor = World->SpawnActor<AStaticMeshActor>(Centre, FRotator(90, 0, 0), Params);
        auto *Component = Actor->GetStaticMeshComponent();
        Component->SetMobility(EComponentMobility::Movable);
        if (Plane)
            Component->SetStaticMesh(Plane);
        // Imaginary gate: purely a pass-through detector, never a physical wall.
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCollisionProfileName(TEXT("NoCollision"));
        Actor->SetActorEnableCollision(false);
        Component->SetCastShadow(false);
        Actor->SetActorScale3D(FVector(5.6f, 5.6f, 1.f)); // plane 100 -> 560 ring
        if (GateMat)
        {
            UMaterialInstanceDynamic *DMI = Component->CreateAndSetMaterialInstanceDynamicFromMaterial(0, GateMat);
            GateMaterials.Add(DMI);
        }
        else
        {
            GateMaterials.Add(nullptr);
        }
        GateActors.Add(Actor);
        GateRedVariant.Add(GateRand.RandRange(0, 3));
        GateGreenVariant.Add(GateRand.RandRange(0, 2));
    }
    UpdateGateColors();
    FRandomStream Random(240924);
    for (int I = 0; I < 90; ++I)
    {
        const double X = Random.FRandRange(-16000, 24000), Y = Random.FRandRange(-18000, 18000);
        if (FMath::Abs(Y) < 1100 || (X > 10000 && Y > 0))
            continue;
        const double H = Random.FRandRange(250, 800);
        Part(Cone, FVector(X, Y, H / 2), FVector(H / 90, H / 90, H / 100), FRotator::ZeroRotator, TEXT("Leaves"));
        Part(Sphere, FVector(X + 350, Y + 180, 65), FVector(2.5, 1.6, 1.4), FRotator(0, I * 31, 0), TEXT("Stone"));
    }
    for (int I = 0; I < 16; ++I)
    {
        const double A = I * 2 * PI / 16;
        Part(Cone, FVector(33000 * FMath::Cos(A), 33000 * FMath::Sin(A), 1800), FVector(90, 90, 70),
             FRotator::ZeroRotator, TEXT("Stone"));
    }
}
void ABorn2FlapGameMode::UpdateGateColors()
{
    // Each gate state carries its own painted artwork, swapped through the dynamic
    // material's GateTex texture parameter: the active gate is red, passed gates turn
    // green, and gates still ahead read as quasi/ghost rings — the nearest ghost uses
    // quasi-gate-1, the next quasi-gate-2, and so on (capped at the last tier).
    auto Pick = [](const TArray<TObjectPtr<UTexture2D>> &Pool, int32 Variant) -> UTexture2D *
    {
        if (Pool.Num() == 0)
            return nullptr;
        return Pool[FMath::Clamp(Variant, 0, Pool.Num() - 1)];
    };
    for (int32 I = 0; I < GateActors.Num(); ++I)
    {
        UTexture2D *Tex = nullptr;
        if (I < GatesPassed)
            Tex = Pick(GateGreenTextures, GateGreenVariant.IsValidIndex(I) ? GateGreenVariant[I] : 0);
        else if (I == GatesPassed)
            Tex = Pick(GateRedTextures, GateRedVariant.IsValidIndex(I) ? GateRedVariant[I] : 0);
        else
            Tex = Pick(GateQuasiTextures, I - GatesPassed - 1); // distance-indexed ghost
        if (GateMaterials.IsValidIndex(I) && GateMaterials[I])
        {
            if (Tex)
                GateMaterials[I]->SetTextureParameterValue(TEXT("GateTex"), Tex);
        }
        // Preserve the painted artwork's aspect ratio: the ghost rings are portrait,
        // the active/passed rings are square.
        if (Tex && GateActors.IsValidIndex(I) && Tex->GetSizeY() > 0)
        {
            const float H = 5.6f;
            const float W = H * (float(Tex->GetSizeX()) / float(Tex->GetSizeY()));
            GateActors[I]->SetActorScale3D(FVector(W, H, 1.f));
        }
    }
}
bool ABorn2FlapGameMode::HasRadioTrack() const { return RadioStation && RadioStation->HasTrack(); }

void ABorn2FlapGameMode::AddPoi(const FString& Key, double X, double Y, float Clearance)
{
    FBorn2FlapPoi P;
    P.Key = Key;
    double Ground = GroundHeight(X, Y);
    // A defensive floor: never leave a reset point below the waterline (e.g. a
    // shore lookout whose camera sits at the tideline). On the coast the sea is
    // +Y and dry beach is -Y, so walk inland until solid sand is found instead
    // of dropping the bird into the surf (the "Basalt cove" scenic camera floats
    // offshore). Non-coast levels keep the old float-at-surface fallback.
    if (Ground < WaterHeight())
    {
        if (bCoastLevel)
        {
            for (int32 Step = 1; Step <= 80; ++Step)
            {
                const double YTry = Y - Step * 250.0;
                const double G = GroundHeight(X, YTry);
                if (G >= WaterHeight() + 30.0)
                {
                    Y = YTry;
                    Ground = G;
                    break;
                }
            }
        }
        else
        {
            Ground = WaterHeight();
        }
    }
    P.Position = FVector(X, Y, Ground + Clearance);
    POIs.Add(P);
}

void ABorn2FlapGameMode::AddPoi(const FString& Key, const FVector& Position, float Yaw)
{
    FBorn2FlapPoi P;
    P.Key = Key;
    P.Position = Position;
    P.Yaw = Yaw;
    POIs.Add(P);
}

void ABorn2FlapGameMode::PopulatePOIs()
{
    POIs.Reset();
    if (bCoastLevel)
    {
        // Shiomori: the launch beach + the four remaining named scenic cameras
        // in the saved map. Each camera sits at a good vantage, so re-using its
        // XY gives natural, hand-launchable reset points along the shore. The
        // old "Tidewalk"/"Shoreline" lookouts sat within ~80 m of the launch
        // beach and were redundant with poi.shiomori_beach.
        if(bHarvestWatch)AddPoi(TEXT("Harvest Watch - field launch"),FVector(0,-19400,480),90.f);
        AddPoi(TEXT("poi.shiomori_beach"), 0.0, 0.0);
        static const TCHAR* Views[] = {
            TEXT("Bay overlook"), TEXT("Basalt cove"),
            TEXT("East vegetation"), TEXT("West vegetation") };
        static const TCHAR* Keys[] = {
            TEXT("poi.bay_overlook"), TEXT("poi.basalt_cove"),
            TEXT("poi.east_vegetation"), TEXT("poi.west_vegetation") };
        for (int32 I = 0; I < 4; ++I)
        {
            for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
            {
                if (It->ActorHasTag(Views[I]))
                {
                    AddPoi(Keys[I], It->GetActorLocation().X, It->GetActorLocation().Y);
                    break;
                }
            }
        }
        // The adapted-hotel campus on the west headland (SITE_OFFSET=-12000,24000
        // in shiomori_campus.py) — named reset points for the newest landmarks:
        // the hotel forecourt, the shrine park garden (kept off the hokora and
        // torii themselves), the beach before the volcanic islet, the far-west
        // pine grove, and the service side (launch field + hangar).
        // The founder fountain forecourt sits on the hand-placed hotel terrace
        // (top at Z=900 in world space); the procedural GroundHeight() sampler
        // only knows the underlying headland (~606), so an explicit Z keeps the
        // reset point above the terrace instead of inside it (underground).
        AddPoi(TEXT("poi.campus_hotel"), FVector(-70600.0, 15000.0, 980.0));  // founder fountain forecourt
        AddPoi(TEXT("poi.shrine_park"),  -71500.0, 24000.0);  // garden on the approach, not the hokora
        AddPoi(TEXT("poi.island_beach"), -44800.0,  9500.0);  // beach before the volcanic islet
        AddPoi(TEXT("poi.pine_grove"),   -86500.0, 17500.0);  // far-west pine forest
        AddPoi(TEXT("poi.launch_field"), -67300.0,  7050.0);  // flight test field
        AddPoi(TEXT("poi.hangar"),       -71000.0, -4000.0);  // workshop apron
        return;
    }
    if (bNatureLevel)
    {
        // Ravenstonefield: the launch meadow plus the named landmarks the
        // world generator already builds (see Born2FlapRavenLandmarks.cpp).
        AddPoi(TEXT("poi.launch_meadow"), 0.0, 0.0);
        // Landmark POIs sit on open ground just clear of each structure's
        // footprint, so the bird rests and hand-launches exactly as at the
        // launch meadow.
        AddPoi(TEXT("poi.raven_watch"), 29000.0, 21400.0);   // north of the gatehouse
        AddPoi(TEXT("poi.crows_acre"), 52000.0, 20000.0);    // clear of the boathouse ruin
        AddPoi(TEXT("poi.last_scrap"), 67000.0, 10500.0);    // clear of the timber posts
        // The underpass is the motorway bridge deck, which stands clear of the
        // river bed the terrain sampler would otherwise return.
        AddPoi(TEXT("poi.underpass"), FVector(18500.0, 9522.0, 1576.0));
        AddPoi(TEXT("poi.old_barns"), -16000.0, -16500.0);   // beside the barn cluster
        return;
    }
    if (bVoidLevel)
    {
        // Neon Abyss: the bronze ring is the orientation landmark and the
        // launch/reset zone. Weightless, so the height is cosmetic — spawn inside
        // the ring's sacred-tree axis at the centre of the sanctuary.
        AddPoi(TEXT("poi.bronze_ring"), FVector(0.0, 0.0, 120.0));
        return;
    }
    // Training course: the start line plus the six sky gates (reset below each
    // gate so the hand-launch flies you through it).
    AddPoi(TEXT("poi.start_line"), 0.0, 0.0);
    for (int32 I = 0; I < Gates.Num(); ++I)
    {
        AddPoi(TEXT("poi.gate"), Gates[I].X, Gates[I].Y);
        POIs.Last().Number = I + 1;
    }
}

int32 ABorn2FlapGameMode::GetInitialPoiIndex() const
{
    // Map the menu's travel token (key + optional gate number) onto this level's
    // populated POI list. An empty key (no ?PoiKey=) returns the default launch
    // point, matching the old behaviour of spawning at POI index 0.
    for (int32 I = 0; I < POIs.Num(); ++I)
        if (POIs[I].Key == InitialPoiKey && POIs[I].Number == InitialPoiNumber)
            return I;
    return 0;
}

void ABorn2FlapGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (BrainRc && IsBrainActive()) BrainRc->Tick(GetWorld()->GetFirstPlayerController(), DeltaSeconds);
    // Platform backend heartbeat (achievements/cloud/presence store sync).
    if (UBorn2FlapPlatformSubsystem* Platform = UBorn2FlapPlatformSubsystem::Get(this))
        Platform->Tick(DeltaSeconds);
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FWeatherMenuTest")))
    {
        if(UGameplayStatics::ParseOption(OptionsString,TEXT("Weather")).IsEmpty())
        {
            if(!MenuWidget) ShowMainMenu(true);
        }
        else if(GetWorld()->GetTimeSeconds()>3.f) FPlatformMisc::RequestExit(false);
    }
    // Startup splash: ease the loading bar to full, fade out, then remove.
    // (The actor is spawned in BeginPlay so it already covers the first frame.)
    if (SplashElapsed >= 0.f)
    {
        static constexpr float LoadDuration = 2.4f;
        static constexpr float FadeDuration = 0.5f;
        SplashElapsed += DeltaSeconds;
        if (SplashWidget)
        {
            const float T = FMath::Clamp(SplashElapsed / LoadDuration, 0.f, 1.f);
            SplashWidget->SetProgress(T * T * (3.f - 2.f * T)); // smoothstep
            if (SplashElapsed >= LoadDuration)
            {
                const float FadeT = FMath::Clamp((SplashElapsed - LoadDuration) / FadeDuration, 0.f, 1.f);
                SplashWidget->SetOpacity(1.f - FadeT);
            }
        }
        if (SplashElapsed >= LoadDuration + FadeDuration)
        {
            if (SplashWidget)
            {
                SplashWidget->Close();
                SplashWidget->Destroy();
                SplashWidget = nullptr;
            }
            SplashElapsed = -1.f;
        }
    }

    // Main menu / level selector — the real home screen. Shown once the splash
    // has faded (or straight away on a splash-less boot) on a plain boot with no
    // explicit level. Decoupled from the splash so -nosplash/-unattended never
    // also skip the menu and drop the player straight into a map.
    if (!bSkipMenu && !bMenuOpen && SplashElapsed < 0.f && !MenuWidget)
        ShowMainMenu(false);

    // F10 re-opens the menu from inside a level. In GameOnly input mode the key
    // is reported reliably by the PlayerController (the menu, once open, switches
    // to UIOnly and its pre-processor takes over for the close).
    if (!bMenuOpen && bSkipMenu && !MenuWidget)
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            if (PC->WasInputKeyJustPressed(born2flap::keybinds::Get(TEXT("ToggleMenu"))))
            {
                if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
                    if (Bird->IsPoiOverlayOpen())
                        Bird->ClosePoiOverlay();
                ShowMainMenu(true);
            }
            // The RC settings key opens the same fullscreen menu, landing on
            // SETTINGS → CONTROL SETTINGS (the RC transmitter panel now lives
            // inside the menu). Guarded so the Brain-authored path keeps its own.
            if (!IsBrainActive() && PC->WasInputKeyJustPressed(born2flap::keybinds::Get(TEXT("ToggleRc"))))
            {
                MenuNavPage = 2;
                MenuSettingsPage = 0;
                MenuPrefsPage = 0;
                ShowMainMenu(true);
            }
        }
    }
    else if (bMenuOpen)
    {
        if (const auto Proc = StaticCastSharedPtr<FMenuKeyInputProcessor>(MenuKeyProcessor))
            if (Proc->ConsumeF10()) {
                if(IsBrainActive()) BrainBridge->HandleAction(TEXT("ui.back"),0,TEXT(""));
                else CloseMainMenu();
            }
    }
    if(bCoastLevel && FParse::Param(FCommandLine::Get(),TEXT("B2FCoastTest")))
    {
        CoastTestTime+=DeltaSeconds;
        auto* PC=UGameplayStatics::GetPlayerController(this,0);
        const bool bSkyTest=FParse::Param(FCommandLine::Get(),TEXT("B2FSkyTest"));
        const TCHAR* Views[]={bSkyTest ? TEXT("Sun horizon") : TEXT("Bay overlook"),bSkyTest ? TEXT("Sun horizon") : TEXT("Tidewalk"),bSkyTest ? TEXT("Sun horizon") : TEXT("Basalt cove"),TEXT("Shoreline"),TEXT("East vegetation"),TEXT("West vegetation")};
        if(CoastCaptureStage<12 && CoastTestTime>2+CoastCaptureStage*2)
        {
            if(CoastCaptureStage%2==0)
            {
                for(TActorIterator<ACameraActor> It(GetWorld());It;++It)
                    if(It->ActorHasTag(Views[CoastCaptureStage/2]) && PC)
                    {
                        // Exercise the actual flight-camera optics. Previously these
                        // screenshots bypassed the sky effect, hiding black-frame bugs.
                        if (APawn* Pawn=PC->GetPawn())
                            if (auto* Camera=Pawn->FindComponentByClass<UCameraComponent>())
                                It->GetCameraComponent()->PostProcessSettings=Camera->PostProcessSettings;
                        if (bSkyTest && (CoastCaptureStage==2 || CoastCaptureStage==4))
                        {
                            FRotator Rotation=It->GetActorRotation();
                            Rotation.Yaw+=180.f;
                            It->SetActorRotation(Rotation);
                            if (CoastCaptureStage==4)
                                if (auto* Fisheye=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/UI/M_FpvFisheye")))
                                    It->GetCameraComponent()->PostProcessSettings.AddBlendable(Fisheye,1.f);
                        }
                        PC->SetViewTarget(*It);
                    }
            }
            else FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("SHIOMORI_%d.png"),CoastCaptureStage/2),false,false);
            ++CoastCaptureStage;
        }
        if(CoastTestTime>27)
        {
            const bool MaskCenter=!IsWater(0,0),MaskSea=IsWater(0,20000),MaskRock=!IsWater(-39500,28500),MaskNear=IsWater(-37000,11200);
            UE_LOG(LogTemp,Display,TEXT("Water mask: center=%d sea=%d rock=%d near=%d"),MaskCenter,MaskSea,MaskRock,MaskNear);
            const double WindNear=WindAt(FVector(0,-1400,350),0).Z,WindOff=WindAt(FVector(0,6000,350),0).Z;
            UE_LOG(LogTemp,Display,TEXT("Wind at probes: near=%.3f offshore=%.3f"),WindNear,WindOff);
            // Upwash transect across the tidewalk sea wall (x=0, bird 3.5 m up).
            for(double Y : {-4000.,-3000.,-2000.,-1500.,-1250.,-1000.,0.,2000.,4000.,6000.})
                UE_LOG(LogTemp,Display,TEXT("Upwash transect y=%.0f lift=%.2f m/s"),Y,WindAt(FVector(0,Y,350),0).Z);
            bool Pass=MaskCenter && MaskSea && MaskRock && MaskNear;
            Pass &= WindNear>.3 && WindOff<.01;
            {
                FHitResult Hit;
                bool HitGround=GetWorld()->LineTraceSingleByChannel(Hit,FVector(2000,3000,700),FVector(2000,3000,-700),ECC_Visibility);
                Pass &= HitGround && FMath::Abs(Hit.ImpactPoint.Z-GroundHeight(2000,3000))<150;
                for(const FVector& P : {FVector(2000,-1750,300),FVector(2000,-2500,300)})
                {
                    HitGround=GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(0,0,700),P-FVector(0,0,700),ECC_Visibility);
                    Pass &= HitGround && FMath::Abs(Hit.ImpactPoint.Z-P.Z)<3;
                }
            }
            // Transects cross dry sand, the waterline and submerged sand. This
            // catches gaps, vertical shelf edges and stale collision/water masks
            // while allowing natural dune undulation (no strict monotonic check).
            for(double X : {-30000.,0.,30000.})
            {
                double Previous=GroundHeight(X,7000);
                for(double Y=7250;Y<=15000;Y+=250)
                {
                    const double Z=GroundHeight(X,Y);
                    FHitResult Hit;
                    const bool Found=GetWorld()->LineTraceSingleByChannel(Hit,FVector(X,Y,700),FVector(X,Y,-1000),ECC_Visibility);
                    Pass &= Found && FMath::Abs(Hit.ImpactPoint.Z-Z)<150;
                    Pass &= Z<=Previous+40.0;
                    Pass &= IsWater(X,Y)==(Z<-50);
                    Previous=Z;
                }
            }
            Pass &= IsWater(-35000,22500); // An intentional tidal channel between scanned outcrops.
            for(const FVector& P : {FVector(-39500,28500,0),FVector(-36800,24800,0),FVector(-42000,31600,0)})
            {
                FHitResult Hit;
                const bool Found=GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(0,0,3500),P-FVector(0,0,500),ECC_Visibility);
                const double Error=Found?FMath::Abs(Hit.ImpactPoint.Z-GroundHeight(P.X,P.Y)):9999.;
                Pass &= Found && Error<180;
                UE_LOG(LogTemp,Display,TEXT("Volcanic collision: hit=%d heightErrorCm=%.1f"),Found,Error);
            }
            for(const FVector& P : {FVector(60000,22000,0),FVector(48000,-28000,0),FVector(0,-58000,0),FVector(-80000,-80000,0)})
            {
                FHitResult Hit;
                const double H=Born2FlapBay::Height(P.X,P.Y);
                const bool Found=GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(0,0,H+1500),P+FVector(0,0,H-1500),ECC_Visibility);
                Pass &= Found && FMath::Abs(Hit.ImpactPoint.Z-H)<120;
                UE_LOG(LogTemp,Display,TEXT("BayLand collision: hit=%d heightErrorCm=%.1f"),Found,Found?FMath::Abs(Hit.ImpactPoint.Z-H):9999.);
            }
            const double Sheltered=Born2FlapBay::WindExposure(FVector(34000,20000,400),FVector(-1,0,0));
            // New outer-campus escarpment: visible geometry, collision and
            // aerodynamic support must agree, including calm/offshore cases.
            for(double X:{-80000.,-72000.,-64000.})
            {
                const double Edge=Born2FlapBay::CliffEdge(X);
                for(double Offset:{-2400.,-500.,500.,1700.})
                {
                    const double H=Born2FlapBay::Height(X,Edge+Offset);
                    FHitResult Hit;
                    const bool Found=GetWorld()->LineTraceSingleByChannel(Hit,FVector(X,Edge+Offset,6000),FVector(X,Edge+Offset,-4000),ECC_Visibility);
                    const double Error=Found?FMath::Abs(Hit.ImpactPoint.Z-H):9999;
                    Pass &= Found && Error<100;
                    UE_LOG(LogTemp,Display,TEXT("CliffTransect x=%.0f offset=%.0f height=%.1f hit=%d error=%.1f"),X,Offset,H,Found,Error);
                }
                // The eroded face starts 8 m inland; sample above its crest,
                // not above the lower ledge in the middle of the escarpment.
                const FVector P(X,Edge+450,Born2FlapBay::Height(X,Edge-1100)+350);
                const double On=Born2FlapBay::CliffLift(P,FVector(0,-4,0));
                const double Off=Born2FlapBay::CliffLift(P,FVector(0,4,0));
                Pass &= On>2 && Off==0 && Born2FlapBay::CliffLift(P,FVector::ZeroVector)==0;
                UE_LOG(LogTemp,Display,TEXT("CliffLift onshore=%.2f offshore=%.2f"),On,Off);
            }
            const double Open=Born2FlapBay::WindExposure(FVector(0,65000,400),FVector(-1,0,0));
            bool EastShorePass=true;
            for(double X:{61000.,75000.,90000.})
            {
                for(double Offset:{-8500.,-2500.,0.,3000.})
                {
                    const double Y=Born2FlapBay::EastShoreEdge(X)+Offset,H=Born2FlapBay::Height(X,Y);
                    FHitResult Hit;
                    const bool Found=GetWorld()->LineTraceSingleByChannel(Hit,FVector(X,Y,6000),FVector(X,Y,-4000),ECC_Visibility);
                    const double Error=Found?FMath::Abs(Hit.ImpactPoint.Z-H):9999;
                    Pass &= Found && Error<100;
                    EastShorePass &= Found && Error<100;
                    UE_LOG(LogTemp,Display,TEXT("EastShoreTransect x=%.0f offset=%.0f height=%.1f hit=%d error=%.1f"),X,Offset,H,Found,Error);
                }
            }
            if(FParse::Param(FCommandLine::Get(),TEXT("B2FEastShoreCollisionTest")))
            {
                // New granite shoulder and terrace probes deliberately avoid
                // placed stones: terrain collision must agree with flight height.
                for(const FVector2D Point:{FVector2D(65000,20000),FVector2D(72000,32000),FVector2D(81600,28800)})
                {
                    FHitResult Hit;const double H=Born2FlapBay::Height(Point.X,Point.Y);
                    const bool Found=GetWorld()->LineTraceSingleByChannel(Hit,FVector(Point.X,Point.Y,H+50),FVector(Point.X,Point.Y,H-100),ECC_Visibility);
                    const double Error=Found?FMath::Abs(Hit.ImpactPoint.Z-H):9999;
                    EastShorePass &= Found && Error<25;
                    UE_LOG(LogTemp,Display,TEXT("EastGraniteCollision x=%.0f y=%.0f error=%.1f"),Point.X,Point.Y,Error);
                }
                FHitResult Door;
                const bool Locked=GetWorld()->LineTraceSingleByChannel(Door,FVector(79500,28130,900),FVector(79900,28130,900),ECC_Visibility);
                EastShorePass &= Locked;
                UE_LOG(LogTemp,Display,TEXT("EastBunkerDoor blocked=%d"),Locked);
                UE_LOG(LogTemp,Display,TEXT("EastShoreCollisionTest %s: shore, granite, terrace and locked bunker"),EastShorePass?TEXT("PASS"):TEXT("FAIL"));
                FPlatformMisc::RequestExitWithStatus(false,EastShorePass?0:1);return;
            }
            const double Above=Born2FlapBay::WindExposure(FVector(34000,20000,15000),FVector(-1,0,0));
            Pass &= Sheltered<Open-.05 && Above>Sheltered+.05;
            UE_LOG(LogTemp,Display,TEXT("Bay wind exposure: sheltered=%.2f open=%.2f above=%.2f"),Sheltered,Open,Above);
            int32 West=0,East=0;
            int32 Shelters=0,Posts=0,Rocks=0,RockWest=0,RockEast=0,IslandGrass=0,IslandTrees=0;
            for(TActorIterator<AStaticMeshActor> It(GetWorld());It;++It)
            { West+=It->ActorHasTag(TEXT("Coastal grass west")); East+=It->ActorHasTag(TEXT("Coastal grass east")); Shelters+=It->ActorHasTag(TEXT("Shelter floating roof")); Posts+=It->ActorHasTag(TEXT("Volleyball post")); Rocks+=It->ActorHasTag(TEXT("Volcanic outcrop"));
              RockWest+=It->ActorHasTag(TEXT("Volcanic beach west")); RockEast+=It->ActorHasTag(TEXT("Volcanic beach east"));
              IslandGrass+=It->ActorHasTag(TEXT("Island grass pocket")); IslandTrees+=It->ActorHasTag(TEXT("Island small tree")); }
            Pass &= Shelters==9 && Posts==2 && Rocks>=12 && West>500 && East>500;
            Pass &= RockWest==12 && RockEast==12;
            UE_LOG(LogTemp,Display,TEXT("Volcanic scenery: west=%d east=%d grass=%d trees=%d"),RockWest,RockEast,IslandGrass,IslandTrees);
            UE_LOG(LogTemp,Display,TEXT("ShiomoriTest %s: sand/stair/promenade collision, bay/island water mask, stair lift, shelters=%d posts=%d volcanic rocks=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Shelters,Posts,Rocks);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
        }
        return;
    }

    // Unified radio controls — identical across every level (M pause, [ / ] volume).
    if (RadioStation)
    {
        RadioStation->Tick(DeltaSeconds);
        if (auto* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            if (PC->WasInputKeyJustPressed(born2flap::keybinds::Get(TEXT("RadioMute")))) RadioStation->ToggleMute();
            if (PC->WasInputKeyJustPressed(born2flap::keybinds::Get(TEXT("RadioVolDown")))) RadioStation->AdjustVolume(-.08f);
            if (PC->WasInputKeyJustPressed(born2flap::keybinds::Get(TEXT("RadioVolUp")))) RadioStation->AdjustVolume(+.08f);
        }
    }

    // F4 (quick level cycle) removed — level travel now lives in the F10 menu.
    auto *Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!Bird)
        return;

    // Gate-race clock: start on launch, stop when the course is cleared or the
    // bird is grounded back at the start (which also resets the gate sequence).
    if (Gates.Num() > 0)
    {
        if (Bird->IsFlying() && !bRaceRunning && GatesPassed < Gates.Num())
        {
            bRaceRunning = true;
            RaceTime = 0;
        }
        if (bRaceRunning)
            RaceTime += DeltaSeconds;
    }

    if (!Bird->IsFlying() && Bird->GetActorLocation().Size2D() < 100)
    {
        if (GatesPassed != 0)
        {
            GatesPassed = 0;
            UpdateGateColors();
        }
        bRaceRunning = false;
        RaceTime = 0;
    }
    if (Gates.IsValidIndex(GatesPassed) && FVector::Dist(Bird->GetActorLocation(), Gates[GatesPassed]) < 260)
    {
        // A gate only counts on a clean pass: no wing contact within the last
        // second. A graze/bump fouls the attempt — the gate stays active (red)
        // until the bird threads it without touching anything.
        const float SinceWingTouch = float(GetWorld()->GetTimeSeconds() - Bird->GetLastWingTouchTime());
        if (SinceWingTouch > 1.0f)
        {
            ++GatesPassed;
            UpdateGateColors();
            UE_LOG(LogTemp, Display, TEXT("FlightGate passed=%d total=%d"), GatesPassed, Gates.Num());
            if (GatesPassed >= Gates.Num())
            {
                bRaceRunning = false;
                if (BestLapTime <= 0 || RaceTime < BestLapTime)
                    BestLapTime = RaceTime;
                UE_LOG(LogTemp, Display, TEXT("FlightRace complete lap=%.2fs best=%.2fs"), RaceTime, BestLapTime);
            }
        }
        else
        {
            UE_LOG(LogTemp, Display, TEXT("FlightGate fouled — wing contact %.1fs ago"), SinceWingTouch);
        }
    }
}

void ABorn2FlapGameMode::ShowMainMenu(bool bInLevel)
{
    if (MenuWidget || (IsBrainActive() && bMenuOpen))
        return;
    bMenuOpen = true;
    bMenuInLevel = bInLevel;
    if (RadioStation)
        RadioStation->SetPaused(true);

    // The menu is modal: the game stops receiving input (UIOnly) and the cursor
    // is freed so its buttons are clickable. The opaque full-bleed backdrop
    // covers the world; no SetPause, so the GameMode keeps ticking and Escape
    // can be handled by the pre-processor.
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
    {
        PC->bShowMouseCursor = true;
        FInputModeUIOnly Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(Mode);
        PC->FlushPressedKeys();
    }

    // Brain path: the menu is authored by the Ruby Brain from telemetry; there
    // is no native actor to spawn (MenuWidget stays null).
    if (IsBrainActive())
        return;

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    MenuWidget = GetWorld()->SpawnActor<ABorn2FlapMenu>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
    if (MenuWidget)
    {
        MenuWidget->SetInLevel(bInLevel);
        // Re-open on the last view the player had open. Only in-level — the
        // home screen has no live bird, so FLIGHT DESK is unreachable there.
        MenuWidget->RestoreView(bInLevel ? MenuNavPage : 0, bInLevel ? MenuSettingsPage : 0, bInLevel ? MenuPrefsPage : 0);
    }
}

void ABorn2FlapGameMode::CloseMainMenu()
{
    if (!MenuWidget && !bMenuOpen)
        return;
    bMenuOpen = false;
    bMenuInLevel = false;
    bBrainPrefsOpen = false;
    // Brain path: the menu's RC page may have opened the transmitter panel
    // (bPanel) so its learn/calibration capture stays live. Close it now so
    // the standalone RC overlay never leaks into flight after the menu closes.
    if (IsBrainActive())
    {
        ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
        FBorn2FlapRcController* Rc = Bird ? Bird->GetRcControllerMutable() : BrainRc.Get();
        if (Rc) Rc->ClosePanel();
    }
    if (RadioStation)
        RadioStation->SetPaused(false);

    // Detach the UMG window now (Destroy() only schedules GC — the window would
    // otherwise linger until the next collection). In the Brain path MenuWidget
    // is already null (the Ruby Brain hides the menu from the telemetry flag).
    if (MenuWidget)
    {
        MenuNavPage = MenuWidget->GetNavPage();
        MenuSettingsPage = MenuWidget->GetSettingsPage();
        MenuPrefsPage = MenuWidget->GetPrefsPage();
        MenuWidget->Close();
        MenuWidget->Destroy();
        MenuWidget = nullptr;
    }

    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
    {
        PC->bShowMouseCursor = false;
        PC->SetInputMode(FInputModeGameOnly());
        PC->FlushPressedKeys();
    }
}

bool ABorn2FlapGameMode::IsBrainActive() const
{
    return BrainBridge && BrainBridge->IsBrainActive();
}

void ABorn2FlapGameMode::HandleBrainAction(const FString& Action, float Value, const FString& Text)
{
    if(Action==TEXT("graphics.quality")) {
        const int32 Q=FMath::Clamp(FMath::RoundToInt(Value),0,3);
        Born2FlapGraphics::SaveQuality(Q);
        Born2FlapGraphics::Apply(Born2FlapGraphics::ForQuality(static_cast<Born2FlapGraphics::EQuality>(Q)));
        return;
    }
    if(Action.StartsWith(TEXT("keybind."))) {
        if(Action==TEXT("keybind.reset")) born2flap::keybinds::ResetAll();
        else {
            const FString Id=Action.Mid(8); const FKey Key{FName(*Text)};
            if(Key.IsValid()) for(const auto& A:born2flap::keybinds::Catalog())
                if(Id==A.Id) { born2flap::keybinds::Set(A.Id,Key); break; }
        }
        return;
    }
    if(Action==TEXT("language.set")) {
        static const TCHAR* Codes[]={TEXT("en"),TEXT("de"),TEXT("es"),TEXT("fr"),TEXT("it"),TEXT("pt"),TEXT("no"),TEXT("ar"),TEXT("hi"),TEXT("ja"),TEXT("ko"),TEXT("ru"),TEXT("zh")};
        BrainPreferences.Language=Codes[FMath::Clamp(FMath::RoundToInt(Value),0,12)];
        Born2Flap::I18n::SetLocale(BrainPreferences.Language);
        if(auto* Bird=Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this,0)))
            born2flap::MenuSettingsFromPawn(Bird,BrainPreferences);
        born2flap::MenuSettingsSave(BrainPreferences); return;
    }
    auto* ActiveBird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this,0));
    auto* ActiveRc = ActiveBird ? ActiveBird->GetRcControllerMutable() : BrainRc.Get();
    if (Action.StartsWith(TEXT("rc.axis.")) || Action.StartsWith(TEXT("rc.invert.")) || Action.StartsWith(TEXT("rc.learn.axis."))) {
        const int32 Channel=FCString::Atoi(*Action.Mid(Action.Find(TEXT("."),ESearchCase::CaseSensitive,ESearchDir::FromEnd)+1));
        if(ActiveRc && Channel>=0 && Channel<5) {
            if(Action.StartsWith(TEXT("rc.axis."))) ActiveRc->AssignChannelAxis(Channel,FMath::Clamp(FMath::RoundToInt(Value),0,7));
            else if(Action.StartsWith(TEXT("rc.invert."))) ActiveRc->ToggleChannelInvert(Channel);
            else ActiveRc->LearnChannelAxis(Channel);
        }
        return;
    }
    if (!ActiveBird && bMenuMode) {
        auto& S=BrainPreferences;
        bool Changed=true;
        if(Action==TEXT("bird.select")) S.BirdModel=FMath::Clamp(FMath::RoundToInt(Value),0,3);
        else if(Action==TEXT("bird.weight")) S.BodyMassKg=FMath::Clamp(Value,.01f,2.f);
        else if(Action==TEXT("bird.cg")) S.CgOffsetMm=FMath::Clamp(Value,-100.f,100.f);
        else if(Action==TEXT("bird.coupled")) S.bCoupledThrottle=Value>.5f;
        else if(Action==TEXT("camera.fpv")) S.bFpvAirView=!S.bFpvAirView;
        else if(Action==TEXT("camera.angle")) S.FpvCameraAngleDeg=Value;
        else if(Action==TEXT("camera.freelook.invert")) S.bFreeLookInvert=Value>.5f;
        else if(Action==TEXT("control.expo")) S.ControlExpo=FMath::Clamp(Value/100.f,0.f,1.f);
        else if(Action==TEXT("mouse.speed")) S.SpeedModifier=Value;
        else if(Action.StartsWith(TEXT("mouse.gain."))) S.MouseGains[FMath::Clamp(FCString::Atoi(*Action.Mid(11)),0,2)]=Value;
        else if(Action==TEXT("mouse.reset")) S.MouseGains=FVector(1,-1,1);
        else if(Action==TEXT("flight.safety")) S.FlightSafety=Value;
        else if(Action==TEXT("replay.spirits")) S.bReplaySpirits=Value>.5f;
        else if(Action==TEXT("audio.wingbeat")) S.WingbeatVolume=FMath::Clamp(Value,0.f,4.f);
        else {
            const bool Display=Action.StartsWith(TEXT("tuning.display."));
            const auto Field=born2flap::tuning::FromKey(Display?Action.Mid(15):Action);
            if(Field!=ETuningField::Count) born2flap::MenuSettingsSetTuning(S,Field,Display?born2flap::tuning::FromDisplayValue(Field,Value):Value);
            else Changed=false;
        }
        if(Changed) { born2flap::MenuSettingsSave(S); return; }
    }
    // POI overlay (F8): select + teleport, or close.
    if (Action == TEXT("poi.close"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->ClosePoiOverlay();
        return;
    }
    if (Action.StartsWith(TEXT("poi.")))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
        {
            Bird->SelectPoi(FCString::Atoi(*Action.Mid(4)));
            Bird->ClosePoiOverlay();
        }
        return;
    }

    if(Action.StartsWith(TEXT("launch."))) {
        const FString Level=MenuWorlds[MenuWorldIndex].Id;
        const int32 Index=FCString::Atoi(*Action.Mid(7));
        if(Born2FlapPoi::Catalog(Level).IsValidIndex(Index)) MenuSelectedPoi.Add(Level,Index);
        return;
    }
    // Menu weather / time-of-day selects (per world).
    if (Action.StartsWith(TEXT("weather.")) || Action.StartsWith(TEXT("daytime.")))
    {
        const bool bWeather = Action.StartsWith(TEXT("weather."));
        const FString Level = Action.Mid(8);
        if (FBorn2FlapConditions* C = MenuLevelConditions.Find(Level))
        {
            const auto Options = bWeather
                ? Born2FlapWeather::WeatherOptions(Level)
                : Born2FlapWeather::TimeOptions(Level, C->Weather);
            const int32 Index = FMath::RoundToInt(Value);
            if (Options.IsValidIndex(Index))
            {
                if (bWeather) C->Weather = Options[Index]; else C->Time = Options[Index];
                *C = Born2FlapWeather::Validate(Level, *C);
                Born2FlapWeather::Save(Level, *C);
            }
        }
        return;
    }

    // Menu season select (per world). One visible knob; the whole hidden stage
    // art (sun path, light spectrum, fog, insects, foliage, radio) follows.
    if (Action.StartsWith(TEXT("season.")))
    {
        const FString Level = Action.Mid(7);
        if (FBorn2FlapConditions* C = MenuLevelConditions.Find(Level))
        {
            const auto Options = Born2FlapWeather::SeasonOptions(Level);
            const int32 Index = FMath::RoundToInt(Value);
            if (Options.IsValidIndex(Index))
            {
                C->Season = Options[Index];
                *C = Born2FlapWeather::Validate(Level, *C);
                Born2FlapWeather::Save(Level, *C);
            }
        }
        return;
    }

    // Wind overrides (per selected world). The sliders write an explicit value
    // on top of the weather/time profile; a reset returns every field to -1
    // ("auto", follow the profile default). Saved + carried into level travel.
    if (Action.StartsWith(TEXT("wind.")))
    {
        const FString Level = MenuWorlds[MenuWorldIndex].Id;
        if (FBorn2FlapConditions* C = MenuLevelConditions.Find(Level))
        {
            if (Action == TEXT("wind.reset"))
            {
                C->WindDir = C->WindSpeed = C->WindGust = C->WindRandom = C->WindThermal = -1.f;
            }
            else
            {
                const float V = Value;
                if      (Action == TEXT("wind.dir"))     C->WindDir = V;
                else if (Action == TEXT("wind.speed"))   C->WindSpeed = V;
                else if (Action == TEXT("wind.gust"))    C->WindGust = V;
                else if (Action == TEXT("wind.random"))  C->WindRandom = V;
                else if (Action == TEXT("wind.thermal")) C->WindThermal = V;
            }
            *C = Born2FlapWeather::Validate(Level, *C);
            Born2FlapWeather::Save(Level, *C);
        }
        return;
    }

    if (Action == TEXT("menu.resume")) { CloseMainMenu(); return; }
    if (Action == TEXT("menu.open_worlds"))
    {
        MenuNavPage = 0;
        if (ActiveRc) ActiveRc->ClosePanel();
        return;
    }
    if (Action == TEXT("menu.settings"))
    {
        // FLIGHT DESK is a menu page now (not an overlay). Flipping the page
        // keeps the world selector from ever stacking on top of it.
        MenuNavPage = 1;
        if (ActiveRc) ActiveRc->ClosePanel();
        return;
    }
    if (Action == TEXT("menu.controls"))
    {
        // RC TRANSMITTER is a menu page. Opening the real panel keeps the
        // transmitter's learn/calibration capture live while on the page.
        MenuNavPage = 3;
        if (ActiveRc) ActiveRc->OpenPanel();
        return;
    }
    if (Action == TEXT("menu.preferences"))
    {
        // APP SETTINGS is a menu page (no separate overlay).
        MenuNavPage = 2;
        if (ActiveRc) ActiveRc->ClosePanel();
        return;
    }

    // RC transmitter control-settings panel (semantic action surface — mirrors
    // the native keyboard driver TAB/C/ENTER/X/G/L/K in Born2FlapRcController).
    if (Action == TEXT("rc.close"))
    {
        if (ActiveRc) ActiveRc->ClosePanel();
        return;
    }
    if (Action == TEXT("rc.device.next"))
    {
        if (ActiveRc) ActiveRc->NextDevice();
        return;
    }
    if (Action == TEXT("rc.calibrate.start"))
    {
        if (ActiveRc) ActiveRc->StartCalibration();
        return;
    }
    if (Action == TEXT("rc.calibrate.advance"))
    {
        if (ActiveRc) ActiveRc->AdvanceCalibration();
        return;
    }
    if (Action == TEXT("rc.calibrate.cancel"))
    {
        if (ActiveRc) ActiveRc->CancelCalibration();
        return;
    }
    if (Action == TEXT("rc.enable.toggle"))
    {
        if (ActiveRc) ActiveRc->ToggleEnabled();
        return;
    }
    if (Action == TEXT("rc.learn.launch"))
    {
        if (ActiveRc) ActiveRc->LearnLaunch();
        return;
    }
    if (Action == TEXT("rc.learn.reset"))
    {
        if (ActiveRc) ActiveRc->LearnReset();
        return;
    }
    if (Action == TEXT("menu.page")) { MenuPage=FMath::Clamp(FMath::RoundToInt(Value),0,2); return; }
    if (Action == TEXT("settings.page")) { SettingsPage=FMath::Clamp(FMath::RoundToInt(Value),0,4); return; }
    if (Action == TEXT("rc.page")) { RcPage=FMath::Clamp(FMath::RoundToInt(Value),0,2); return; }
    if (Action == TEXT("preferences.page")) { PrefsPage=FMath::Clamp(FMath::RoundToInt(Value),0,2); return; }
    if (Action == TEXT("preferences.close")) { bBrainPrefsOpen=false; return; }
    if (Action == TEXT("settings.close") || Action == TEXT("editor.close"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->CloseFlightSettings();
        return;
    }
    // Semantic selection emitted by UMGHAML; native code only applies gameplay.
    if (Action == TEXT("bird.select"))
    {
        if (auto* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->SelectBirdModel(FMath::Clamp(FMath::RoundToInt(Value), 0, 4));
        return;
    }
    if (Action == TEXT("radio.play") || Action == TEXT("radio.volume") || Action == TEXT("radio.mute"))
    {
        if(RadioStation) {
            if(Action == TEXT("radio.play")) RadioStation->TogglePlayPause();
            else if(Action == TEXT("radio.mute")) RadioStation->ToggleMute();
            else RadioStation->SetVolume(FMath::Clamp(Value,0.f,1.f));
        } else if(Action==TEXT("radio.volume")) UBorn2FlapRadioStation::SavePreferenceVolume(Value);
        return;
    }
    // CRAFT tab.
    if (Action.StartsWith(TEXT("bird.model.")))
    {
        const int32 Model = FCString::Atoi(*Action.RightChop(FCString::Strlen(TEXT("bird.model."))));
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->SelectBirdModel(Model);
        return;
    }
    if (Action == TEXT("camera.fpv"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->ToggleFpvView();
        return;
    }
    // BIRD tab (further bird tuning).
    if (Action == TEXT("camera.angle"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->SetFpvCameraAngle(Value);
        return;
    }
    if (Action == TEXT("bird.weight"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->SetBodyMassKg(Value);
        return;
    }
    if (Action == TEXT("bird.cg"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->SetCgOffsetMm(Value);
        return;
    }
    if (Action == TEXT("bird.coupled"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->SetThrottleCoupled(Value > 0.5f);
        return;
    }
    if (Action == TEXT("mouse.speed"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->SetSpeedModifier(Value);
        return;
    }
    // CONTROLS tab.
    if (Action.StartsWith(TEXT("mouse.gain.")))
    {
        const int32 Axis = FCString::Atoi(*Action.RightChop(FCString::Strlen(TEXT("mouse.gain."))));
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->SetMouseGain(Axis, Value);
        return;
    }
    if (Action == TEXT("mouse.reset"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
        {
            Bird->SetMouseGain(0, 1.f);
            Bird->SetMouseGain(1, -1.f);
            Bird->SetMouseGain(2, 1.f);
        }
        return;
    }
    if (Action == TEXT("control.expo"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->SetControlExpo(Value / 100.f);
        return;
    }
    // ASSIST tab.
    if (Action == TEXT("flight.safety"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->SetFlightSafety(Value);
        return;
    }
    if (Action == TEXT("replay.spirits"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->SetReplaySpiritsEnabled(Value > 0.5f);
        return;
    }
    if (Action == TEXT("replay.delete"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->DeleteReplaySpirits();
        return;
    }
    if (Action.StartsWith(TEXT("tuning.display.")))
    {
        const auto F=born2flap::tuning::FromKey(Action.Mid(15));
        if(F!=ETuningField::Count)
            if(auto* Bird=Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this,0)))
                Bird->SetTuning(F,born2flap::tuning::FromDisplayValue(F,Value));
        return;
    }
    if (Action == TEXT("audio.wingbeat"))
    {
        if(auto* Bird=Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this,0))) Bird->SetWingbeatVolume(Value);
        return;
    }
    if (Action == TEXT("camera.freelook.invert"))
    {
        if(auto* Bird=Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this,0))) Bird->SetFreeLookInvert(Value>.5f);
        return;
    }
    // TUNING tab (12 firmware knobs).
    const ETuningField Field = born2flap::tuning::FromKey(Action);
    if (Field != ETuningField::Count)
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->SetTuning(Field, Value);
        return;
    }
    if (Action == TEXT("menu.prevWorld")) { MenuWorldIndex = (MenuWorldIndex + MenuWorldCount - 1) % MenuWorldCount; return; }
    if (Action == TEXT("menu.nextWorld")) { MenuWorldIndex = (MenuWorldIndex + 1) % MenuWorldCount; return; }
    if (Action == TEXT("menu.quit")) { UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false); return; }

    UE_LOG(LogTemp, Verbose, TEXT("UMGHAML action: %s value=%g"), *Action, Value);
    // Level travel (menu.<world>).
    struct FEntry { const TCHAR* ActionKey; const TCHAR* Map; const TCHAR* Level; };
    static const FEntry Entries[] = {
        { TEXT("menu.ravenstonefield"), TEXT("/Game/Ravenstonefield/Maps/RAVENSTONEFIELD"), TEXT("Ravenstonefield") },
        { TEXT("menu.harvestwatch"), TEXT("/Game/Shiomori/Maps/SHIOMORI"), TEXT("Shiomori") },
        { TEXT("menu.shiomori"),        TEXT("/Game/Shiomori/Maps/SHIOMORI"),               TEXT("Shiomori") },
        { TEXT("menu.training"),        TEXT("/Engine/Maps/Entry"),                         TEXT("Training") },
        { TEXT("menu.neonabyss"),       TEXT("/Game/NeonAbyss/Maps/NEONABYSS"),             TEXT("NeonAbyss") },
    };
    for (const FEntry& E : Entries)
    {
        if (Action == E.ActionKey)
        {
            FString Options = Born2FlapWeather::TravelOptions(E.Level, MenuLevelConditions.FindChecked(E.Level));
            if(Action==TEXT("menu.harvestwatch"))Options+=TEXT("?Activity=HarvestWatch");
            if (const int32* Sel = MenuSelectedPoi.Find(E.Level))
            {
                const TArray<FBorn2FlapPoi> Cat = Born2FlapPoi::Catalog(E.Level);
                if (Cat.IsValidIndex(*Sel))
                {
                    Options += FString::Printf(TEXT("?PoiKey=%s"), *Cat[*Sel].Key);
                    if (Cat[*Sel].Number > 0)
                        Options += FString::Printf(TEXT("?PoiNum=%d"), Cat[*Sel].Number);
                }
            }
            Born2FlapShowLoadingScreen();
            // Remove the Brain-authored menu host from the viewport immediately,
            // before the travel tears the old world down. EndPlay also closes it,
            // but that fires during teardown — potentially after the loading
            // screen has already dropped — so close it here to deterministically
            // stop the menu from flashing over the destination map.
            if (BrainBridge)
                BrainBridge->CloseRenderer();
            // Stamp `loading:true` into the Brain's telemetry file now, before
            // the engine stops ticking for the movie. The destination map spawns
            // a fresh Brain that reads whatever is on disk; without this it
            // would see the menu map's stale `frontend:true, loading:false` and
            // paint the menu on top of the loading screen.
            if (BrainBridge)
                BrainBridge->ForceTelemetry();
            UGameplayStatics::OpenLevel(this, E.Map, true, Options);
            return;
        }
    }
}

void ABorn2FlapGameMode::HandleBrainCommand(const FString& Name, const TSharedPtr<FJsonObject>& Payload)
{
    if(Name==TEXT("world.load") && Payload.IsValid()) {
        FString Level;
        if(Payload->TryGetStringField(TEXT("level"),Level))
            for(const auto& World:MenuWorlds) if(Level==World.Id) {
                HandleBrainAction(TEXT("menu.")+Level.ToLower(),0,TEXT("")); return;
            }
        return;
    }
    // The Ruby Brain is the orchestrator. Commands are its imperative channel
    // into the game core — non-time-critical flow control only; the per-tick
    // simulation stays in C++. Extend this table (and the Ruby side) to expose
    // new game-flow hooks for modding (world assembly, race gates, weather,
    // flight phase, …).
    if (Name == TEXT("boot"))
    {
        FString Ruby;
        if (Payload.IsValid())
            Payload->TryGetStringField(TEXT("ruby"), Ruby);
        UE_LOG(LogTemp, Display, TEXT("Born2Flap Brain booted (ruby %s) — orchestrator online"), *Ruby);
        return;
    }
    if (Name == TEXT("ping"))
    {
        UE_LOG(LogTemp, Display, TEXT("Born2Flap Brain ping"));
        return;
    }
    if (Name == TEXT("log"))
    {
        FString Msg;
        if (Payload.IsValid() && Payload->TryGetStringField(TEXT("message"), Msg))
            UE_LOG(LogTemp, Display, TEXT("Born2Flap Brain: %s"), *Msg);
        return;
    }
    UE_LOG(LogTemp, Warning, TEXT("Born2Flap Brain: unknown command '%s'"), *Name);
}

FString ABorn2FlapGameMode::BuildBrainTelemetry() const
{
    TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject());

    ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
    // F7 suspends all visual telemetry, including open panels. Keep routing and
    // input acknowledgement alive; the next snapshot restores the current UI.
    // Exception: the F10 menu is the one surface that must escape F7. Opening
    // the menu with the HUD hidden already switches the game to UIOnly + frees
    // the cursor; if `ui_hidden` still short-circuits here, the Brain never sees
    // `menu` and renders nothing — leaving the game stuck in UIOnly with no
    // visible menu and no flight input ("broken UI"). Fall through instead so
    // `menu`/`menu_inlevel` reach the Brain and the menu frame renders.
    if (Bird && Bird->IsUIHidden() && !bMenuOpen)
        return TEXT("{\"frontend\":false,\"ui_hidden\":true,\"hud\":false}");
    if (Bird)
    {
        Root->SetNumberField(TEXT("altitude"), Bird->GetAltitude());
        Root->SetNumberField(TEXT("climb"), Bird->GetClimbRate());
        Root->SetNumberField(TEXT("speed"), Bird->GetSpeed());
        Root->SetNumberField(TEXT("battery"), Bird->GetBattery() * 100.0);
        Root->SetNumberField(TEXT("throttle"), Bird->GetEffort());
        Root->SetStringField(TEXT("status"), Bird->GetFlightStatus());
        // Wind readout for the Ruby cockpit. The relative source bearing is
        // pre-computed in screen space (0° = ahead, clockwise positive) so the
        // Brain stays declarative: it feeds the WindArrow's `angle` prop and the
        // custom widget owns the continuously-rotating paint. Rounded to 0.1°
        // / 0.1 m/s so telemetry only changes on a perceptible wind shift.
        {
            const FVector Wind = Bird->GetWind();
            const float WindSpeed = FVector2D(Wind.X, Wind.Y).Size();
            const float BlowBearing = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(Wind.X, Wind.Y)) + 360.0f, 360.0f);
            const float SourceBearing = FMath::Fmod(BlowBearing + 180.0f, 360.0f);
            float ViewYaw = 0.f;
            if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
                if (APlayerCameraManager* Cam = PC->PlayerCameraManager)
                    ViewYaw = Cam->GetCameraRotation().Yaw;
            const float ViewBearing = FMath::Fmod(90.0f - ViewYaw + 360.0f, 360.0f);
            const float RelAngle = FMath::Fmod(ViewBearing - SourceBearing + 360.0f, 360.0f);
            Root->SetNumberField(TEXT("wind_speed"), FMath::RoundToFloat(WindSpeed * 10.f) / 10.f);
            Root->SetNumberField(TEXT("wind_arrow_angle"), FMath::RoundToFloat(RelAngle * 10.f) / 10.f);
        }
        Root->SetBoolField(TEXT("settings"), Bird->IsFlightSettingsOpen());
        Root->SetBoolField(TEXT("poi"), Bird->IsPoiOverlayOpen());
        // F7 hides the whole HUD layer (cockpit + radio). Mirrors the native
        // cockpit collapse in Born2FlapFlightHUD::Refresh().
        Root->SetBoolField(TEXT("hud"), !(Bird->IsBlindFlight() || Bird->IsFlightSettingsOpen() || Bird->IsUIHidden()));

        // POI list (current level).
        const TArray<FBorn2FlapPoi>& PoiList = Bird->GetPOIs();
        const int32 PoiSelected = Bird->GetSelectedPoi();
        TArray<TSharedPtr<FJsonValue>> PoiArr;
        for (int32 I = 0; I < PoiList.Num(); ++I)
        {
            FString Name = Born2Flap::I18n::T(PoiList[I].Key);
            if (PoiList[I].Number > 0)
                Name += TEXT(" ") + FString::FromInt(PoiList[I].Number);
            TSharedPtr<FJsonObject> PoiObj = MakeShareable(new FJsonObject());
            PoiObj->SetStringField(TEXT("name"), Name);
            PoiObj->SetBoolField(TEXT("selected"), I == PoiSelected);
            PoiArr.Add(MakeShareable(new FJsonValueObject(PoiObj)));
        }
        Root->SetArrayField(TEXT("pois"), PoiArr);

    }
    // Capture festival (Neon Abyss): raw C++ state for the Ruby capture ruleset.
    // Ruby owns scoring and the finished? decision; C++ only supplies the feed.
    {
        TSharedPtr<FJsonObject> Cap = MakeShareable(new FJsonObject());
        bool bHasCapture = false;
        if (TActorIterator<ABorn2FlapNeonVoid> It(GetWorld()); It)
        {
            Cap->SetNumberField(TEXT("captured"), It->GetCaptured());
            Cap->SetNumberField(TEXT("target"), It->GetTargetCount());
            Cap->SetNumberField(TEXT("combo"), It->GetCombo());
            Cap->SetNumberField(TEXT("time"), It->GetTimeRemaining());
            Cap->SetNumberField(TEXT("best"), It->GetBestScore());
            Cap->SetNumberField(TEXT("active"), It->GetActiveJellyfish());
            Cap->SetBoolField(TEXT("running"), It->IsFestivalRunning());
            bHasCapture = true;
        }
        Root->SetObjectField(TEXT("capture"), Cap);
        Root->SetBoolField(TEXT("capture_active"), bHasCapture);
    }
    auto Settings=BrainPreferences;
    if(Bird) born2flap::MenuSettingsFromPawn(Bird,Settings);
        // Flight-desk settings state (CRAFT / BIRD / CONTROLS / ASSIST / TUNING).
        Root->SetNumberField(TEXT("bird_model"), Settings.BirdModel);
        Root->SetBoolField(TEXT("camera_fpv"), Settings.bFpvAirView);
        Root->SetNumberField(TEXT("fpv_angle"), Settings.FpvCameraAngleDeg);
        Root->SetNumberField(TEXT("body_mass_kg"), Settings.BodyMassKg);
        Root->SetNumberField(TEXT("cg_offset_mm"), Settings.CgOffsetMm);
        Root->SetBoolField(TEXT("coupled_throttle"), Settings.bCoupledThrottle);
        Root->SetNumberField(TEXT("speed_modifier"), Settings.SpeedModifier);
        Root->SetNumberField(TEXT("flight_safety"), Settings.FlightSafety);
        Root->SetBoolField(TEXT("replay_spirits"), Settings.bReplaySpirits);
        Root->SetNumberField(TEXT("control_expo"), Settings.ControlExpo);
        {
            const FVector Gains = Settings.MouseGains;
            TArray<TSharedPtr<FJsonValue>> GainArr;
            for (int32 Axis = 0; Axis < 3; ++Axis)
                GainArr.Add(MakeShareable(new FJsonValueNumber((double)Gains[Axis])));
            Root->SetArrayField(TEXT("mouse_gains"), GainArr);
        }
        for (uint8 I = 0; I < (uint8)ETuningField::Count; ++I)
        {
            const ETuningField Field = (ETuningField)I;
            Root->SetNumberField(born2flap::tuning::Key(Field), born2flap::MenuSettingsGetTuning(Settings,Field));
        }

        TArray<TSharedPtr<FJsonValue>> Knobs;
        for(const auto& R : born2flap::tuning::Rows) {
            auto K=MakeShared<FJsonObject>();
            K->SetStringField(TEXT("key"),born2flap::tuning::Key(R.Field));
            K->SetStringField(TEXT("label"),R.Label); K->SetStringField(TEXT("unit"),R.Unit);
            K->SetStringField(TEXT("help"),R.Help);
            K->SetNumberField(TEXT("min"),R.Min); K->SetNumberField(TEXT("max"),R.Max);
            K->SetNumberField(TEXT("step"),FMath::Pow(10.f,-R.Decimals));
            K->SetNumberField(TEXT("decimals"),R.Decimals);
            K->SetNumberField(TEXT("value"),born2flap::tuning::ToDisplayValue(R.Field,born2flap::MenuSettingsGetTuning(Settings,R.Field)));
            Knobs.Add(MakeShared<FJsonValueObject>(K));
        }
        Root->SetArrayField(TEXT("tuning_fields"),Knobs);
        Root->SetNumberField(TEXT("wingbeat_volume"),Settings.WingbeatVolume);
        Root->SetBoolField(TEXT("freelook_invert"),Settings.bFreeLookInvert);
        // RC sender (F3 panel) + live channels.
        const FBorn2FlapRcController* Rc = Bird ? Bird->GetRcController() : BrainRc.Get();
        Root->SetBoolField(TEXT("rc"), Rc && Rc->IsPanelOpen());
        Root->SetStringField(TEXT("rc_device"), Rc ? Rc->GetDeviceName() : TEXT(""));
        Root->SetStringField(TEXT("rc_status"), Rc ? Rc->GetStatus() : TEXT(""));
        Root->SetStringField(TEXT("rc_instruction"), Rc ? Rc->GetInstruction() : TEXT(""));
        Root->SetBoolField(TEXT("rc_connected"), Rc && Rc->IsConnected());
        Root->SetBoolField(TEXT("rc_armed"), Rc && Rc->IsArmed());
        Root->SetStringField(TEXT("rc_buttons"), Rc ? Rc->GetButtons() : TEXT(""));
        Root->SetBoolField(TEXT("rc_enabled"), Rc && Rc->IsEnabled());
        Root->SetNumberField(TEXT("rc_stage"), Rc ? Rc->GetStage() : -1);
        Root->SetStringField(TEXT("rc_notice"), Rc ? Rc->GetNotice() : TEXT(""));
        static const TCHAR* ChannelNames[] = { TEXT("GAS"), TEXT("ROLL"), TEXT("PITCH"), TEXT("YAW"), TEXT("SPEED") };
        TArray<TSharedPtr<FJsonValue>> ChArr;
        if (Rc)
        {
            const auto& Ch = Rc->GetChannels();
            for (int32 I = 0; I < 5; ++I)
            {
                TSharedPtr<FJsonObject> ChObj = MakeShareable(new FJsonObject());
                ChObj->SetStringField(TEXT("name"), ChannelNames[I]);
                ChObj->SetNumberField(TEXT("value"), (double)Ch[I]);
                ChArr.Add(MakeShareable(new FJsonValueObject(ChObj)));
            }
        }
        Root->SetArrayField(TEXT("rc_channels"), ChArr);

        // Enumerated devices (for the dropdown / next-device readout).
        TArray<TSharedPtr<FJsonValue>> DevArr;
        if (Rc)
            for (const FString& N : Rc->GetDeviceNames())
                DevArr.Add(MakeShareable(new FJsonValueString(N)));
        Root->SetArrayField(TEXT("rc_devices"), DevArr);

        // Live axes (8) for the meter readout.
        TArray<TSharedPtr<FJsonValue>> AxArr;
        if (Rc)
            for (int32 I = 0; I < 8; ++I)
            {
                TSharedPtr<FJsonObject> A = MakeShareable(new FJsonObject());
                A->SetNumberField(TEXT("value"), (double)FMath::Clamp(float(Rc->GetRawAxes()[I]), 0.f, 1.f));
                A->SetBoolField(TEXT("available"), Rc->GetAvailableAxes()[I] && Rc->IsConnected());
                AxArr.Add(MakeShareable(new FJsonValueObject(A)));
            }
        Root->SetArrayField(TEXT("rc_axes"), AxArr);

        // Channel mappings (5 strings) for the readout.
        TArray<TSharedPtr<FJsonValue>> MapArr;
        if (Rc)
            for (int32 I = 0; I < 5; ++I)
                MapArr.Add(MakeShareable(new FJsonValueString(Rc->GetMapping(I))));
        Root->SetArrayField(TEXT("rc_mappings"), MapArr);
        TArray<TSharedPtr<FJsonValue>> ChannelAxes;
        if(Rc) for(int32 I=0;I<5;++I) ChannelAxes.Add(MakeShared<FJsonValueNumber>(Rc->GetChannelAxis(I)));
        Root->SetArrayField(TEXT("rc_channel_axes"),ChannelAxes);

    Root->SetNumberField(TEXT("graphics_quality"),Born2FlapGraphics::LoadQuality());
    Root->SetStringField(TEXT("lang"),Born2Flap::I18n::GetLocale());
    TArray<TSharedPtr<FJsonValue>> Keys;
    for(const auto& A:born2flap::keybinds::Catalog()) {
        auto K=MakeShared<FJsonObject>();
        K->SetStringField(TEXT("id"),A.Id); K->SetStringField(TEXT("label"),A.Label);
        K->SetStringField(TEXT("key"),born2flap::keybinds::Get(A.Id).GetFName().ToString());
        Keys.Add(MakeShared<FJsonValueObject>(K));
    }
    Root->SetArrayField(TEXT("keybindings"),Keys);
    // Level-loading screen: while the movie player's loader is active, the
    // Brain suppresses the whole UI (menu + HUD) so nothing bleeds through
    // behind the loading screen. Progress/status mirror the shared
    // Born2Flap::Loading state the C++ loader paints.
    {
        float LProgress = -1.f; FString LStatus;
        Born2Flap::Loading::State().Get(LProgress, LStatus);
        Root->SetBoolField(TEXT("loading"), Born2Flap::Loading::State().IsActive());
        Root->SetNumberField(TEXT("loading_progress"), (double)(LProgress < 0.f ? 0.f : LProgress));
        Root->SetStringField(TEXT("loading_status"), LStatus);
    }
    // Menu / level selector.
    Root->SetBoolField(TEXT("frontend"), bMenuMode);
    Root->SetBoolField(TEXT("menu"), bMenuOpen);
    Root->SetBoolField(TEXT("menu_inlevel"), bMenuInLevel);
    Root->SetBoolField(TEXT("preferences"), bBrainPrefsOpen);
    Root->SetNumberField(TEXT("preferences_page"), PrefsPage);
    Root->SetNumberField(TEXT("world_index"), MenuWorldIndex);
    Root->SetNumberField(TEXT("menu_page"), MenuPage);
    Root->SetNumberField(TEXT("nav_page"), MenuNavPage);
    Root->SetNumberField(TEXT("settings_page"), SettingsPage);
    Root->SetNumberField(TEXT("rc_page"), RcPage);

    TArray<TSharedPtr<FJsonValue>> WorldArr;
    for (int32 I = 0; I < MenuWorldCount; ++I)
    {
        const FMenuWorld& W = MenuWorlds[I];
        const FBorn2FlapConditions& C = MenuLevelConditions.FindChecked(W.Id);
        TSharedPtr<FJsonObject> WObj = MakeShareable(new FJsonObject());
        WObj->SetStringField(TEXT("id"), W.Id);
        WObj->SetStringField(TEXT("title"), W.Title);
        WObj->SetStringField(TEXT("story"), W.Story);
        WObj->SetStringField(TEXT("weather"), C.Weather);
        WObj->SetStringField(TEXT("time"), C.Time);
        WObj->SetStringField(TEXT("season"), C.Season);
        TArray<TSharedPtr<FJsonValue>> WeatherOpts, TimeOpts, SeasonOpts;
        for (const FString& S : Born2FlapWeather::WeatherOptions(W.Id))
            WeatherOpts.Add(MakeShareable(new FJsonValueString(S)));
        for (const FString& S : Born2FlapWeather::TimeOptions(W.Id, C.Weather))
            TimeOpts.Add(MakeShareable(new FJsonValueString(S)));
        for (const FString& S : Born2FlapWeather::SeasonOptions(W.Id))
            SeasonOpts.Add(MakeShareable(new FJsonValueString(S)));
        WObj->SetArrayField(TEXT("weathers"), WeatherOpts);
        WObj->SetArrayField(TEXT("times"), TimeOpts);
        WObj->SetArrayField(TEXT("seasons"), SeasonOpts);

        // Wind config: effective values for the menu sliders (an explicit
        // override when >= 0, otherwise the weather/time profile "auto" default)
        // plus a flag that shows the reset button only when the player tuned it.
        auto EffectiveWind = [](float Override, float AutoDefault)
        { return (double)(Override >= 0.f ? Override : AutoDefault); };
        WObj->SetNumberField(TEXT("wind_dir"),     EffectiveWind(C.WindDir, Born2FlapWeather::DefaultWindDirection(W.Id)));
        WObj->SetNumberField(TEXT("wind_speed"),   EffectiveWind(C.WindSpeed, 1.f));
        WObj->SetNumberField(TEXT("wind_gust"),    EffectiveWind(C.WindGust, 1.f));
        WObj->SetNumberField(TEXT("wind_random"),  EffectiveWind(C.WindRandom, 1.f));
        WObj->SetNumberField(TEXT("wind_thermal"), EffectiveWind(C.WindThermal, 1.f));
        WObj->SetBoolField(TEXT("wind_custom"),
            C.WindDir >= 0.f || C.WindSpeed >= 0.f || C.WindGust >= 0.f ||
            C.WindRandom >= 0.f || C.WindThermal >= 0.f);

        TArray<TSharedPtr<FJsonValue>> Launches;
        for(const auto& Poi:Born2FlapPoi::Catalog(W.Id)) {
            FString Label=Born2Flap::I18n::T(Poi.Key);
            if(Poi.Number>0) Label+=TEXT(" ")+FString::FromInt(Poi.Number);
            Launches.Add(MakeShared<FJsonValueString>(Label));
        }
        WObj->SetArrayField(TEXT("launch_points"),Launches);
        WObj->SetNumberField(TEXT("launch_index"),MenuSelectedPoi.FindRef(W.Id));
        WorldArr.Add(MakeShareable(new FJsonValueObject(WObj)));
    }
    Root->SetArrayField(TEXT("worlds"), WorldArr);

    // Splash: driven by SplashElapsed (native actor or Ruby overlay).
    if (SplashElapsed >= 0.f)
    {
        static constexpr float LoadDuration = 2.4f;
        static constexpr float FadeDuration = 0.5f;
        const float T = FMath::Clamp(SplashElapsed / LoadDuration, 0.f, 1.f);
        Root->SetBoolField(TEXT("splash"), true);
        Root->SetNumberField(TEXT("splash_progress"), T * T * (3.f - 2.f * T));
        Root->SetNumberField(TEXT("splash_opacity"),
            SplashElapsed >= LoadDuration ? 1.f - FMath::Clamp((SplashElapsed - LoadDuration) / FadeDuration, 0.f, 1.f) : 1.f);
    }
    else
    {
        Root->SetBoolField(TEXT("splash"), false);
        Root->SetNumberField(TEXT("splash_progress"), 0.0);
        Root->SetNumberField(TEXT("splash_opacity"), 1.0);
    }

    // Radio.
    Root->SetBoolField(TEXT("radio_visible"), RadioStation != nullptr && RadioStation->HasTrack());
    Root->SetStringField(TEXT("radio_station"), RadioStation ? RadioStation->GetStationName() : TEXT(""));
    Root->SetStringField(TEXT("radio_track"), RadioStation ? RadioStation->GetTrackName() : TEXT(""));
    Root->SetBoolField(TEXT("radio_playing"), RadioStation && RadioStation->IsPlaying());
    // Effective volume (0 while muted) so the HUD slider drops to zero on M;
    // radio_muted lets the face show an explicit MUTED badge.
    Root->SetNumberField(TEXT("radio_volume"), RadioStation ? RadioStation->GetVolume() : UBorn2FlapRadioStation::PreferenceVolume());
    Root->SetBoolField(TEXT("radio_muted"), RadioStation && RadioStation->IsMuted());
    Root->SetStringField(TEXT("radio_cover"), RadioStation ? RadioStation->GetCoverPath() : TEXT(""));

    // Channels (F2) — the Ruby path folds this into the RC readout for now.
    Root->SetBoolField(TEXT("channels"), false);

    FString Out;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    return Out;
}

void ABorn2FlapGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (MenuKeyProcessor && FSlateApplication::IsInitialized())
        FSlateApplication::Get().UnregisterInputPreProcessor(MenuKeyProcessor);
    MenuKeyProcessor.Reset();
    Super::EndPlay(EndPlayReason);
}