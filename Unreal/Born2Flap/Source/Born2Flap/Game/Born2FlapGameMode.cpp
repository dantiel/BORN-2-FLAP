#include "Game/Born2FlapGameMode.h"
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
#include "UI/Born2FlapUIBridge.h"
#include "UI/Born2FlapFlightHUD.h"
#include "UI/Born2FlapRadio.h"
#include "UI/Born2FlapI18n.h"
#include "UI/Born2FlapSplash.h"
#include "UI/Born2FlapMenu.h"
#include "Racing/Born2FlapRacing.h"
#include "Water/Born2FlapWaterDirector.h"
#include "Water/Born2FlapSurf.h"
#include "Audio/Born2FlapSoundscape.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Blueprint/UserWidget.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"
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
        if (InKeyEvent.GetKey() == EKeys::F10)
        {
            bF10Pressed = true;
            return true;  // consume: the menu closes on this F10, nothing else sees it
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
      TEXT("A highland valley of black basalt columns rising from amber bracken. Ravens ride the thermals between the spires, and the wind carries heather and cold stone. An old ornithopter field, flown by generations of wing-builders.") },
    { TEXT("Shiomori"), TEXT("SHIOMORI BAY"),
      TEXT("A broad pale beach arcing around a sheltered bay of dark, glassy water. Salt-cured rock, wind-bent grass and a low tide-walk of basalt stretch inland toward hazy, sunlit hills. Quiet enough that you notice what the sky is doing.") },
    { TEXT("Training"), TEXT("TRAINING"),
      TEXT("A flat grass course marked with floating sky-gates. The field where every pilot learns to fold the wing, hold the line and thread the gates against the clock.") },
};
constexpr int32 MenuWorldCount = 3;
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
    bNatureLevel = !Level.Equals(TEXT("Training"), ESearchCase::IgnoreCase) &&
                   !FParse::Param(FCommandLine::Get(), TEXT("B2FFlightTest")) &&
                   !FParse::Param(FCommandLine::Get(), TEXT("B2FSoakTest")) &&
                   !FParse::Param(FCommandLine::Get(), TEXT("B2FHandlingTest"));
    // Menu mode: a plain interactive boot into the minimal Entry map (no
    // explicit level) is the home screen, NOT a playable level. Nothing is
    // built and no bird spawns behind the menu — a real level only loads when
    // the player selects a world and clicks FLY (which travels with ?Level=
    // and ?SkipMenu=1). Automated/test runs are always -unattended, so they
    // never trip this and keep booting straight into their test map.
    bMenuMode = Level.IsEmpty() &&
                MapName.Contains(TEXT("Entry"), ESearchCase::IgnoreCase) &&
                !FApp::IsUnattended();
    if (bMenuMode)
    {
        bCoastLevel = false;
        bNatureLevel = false;
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
    InitialPoiKey=UGameplayStatics::ParseOption(Options,TEXT("PoiKey"));
    InitialPoiNumber=FCString::Atoi(*UGameplayStatics::ParseOption(Options,TEXT("PoiNum")));
    if(Weather.IsEmpty()) FParse::Value(FCommandLine::Get(),TEXT("B2FWeather="),Weather);
    if(DayTime.IsEmpty()) FParse::Value(FCommandLine::Get(),TEXT("B2FDayTime="),DayTime);
    if(!Weather.IsEmpty()) Conditions.Weather=Weather.ToLower();
    if(!DayTime.IsEmpty()) Conditions.Time=DayTime.ToLower();
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
        if(FMath::Abs(X)<=75000 && Y>=-51000 && Y<=-3000) return 150;
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
        return FMath::Clamp(FMath::CeilToDouble((-Y-1200)/100.)*25.,0.,150.);
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
        World->SpawnActor<ABorn2FlapRacingManager>(FVector::ZeroVector, FRotator::ZeroRotator, RacingParams);
    }
    // Unified radio: one station + one semantic HUD drive every level. The
    // playlist is data-driven per level (Config/DefaultGame.ini).
    {
        RadioStation = NewObject<UBorn2FlapRadioStation>(this);
        const FString RadioLevel = bCoastLevel ? TEXT("Shiomori") : (bNatureLevel ? TEXT("Ravenstonefield") : TEXT("Training"));
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
    if(bCoastLevel)
    {
        World->SpawnActor<ABorn2FlapBayTerrain>();
        World->SpawnActor<ABorn2FlapSurf>();
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
        // Ravenstonefield carries its own atmosphere and can be saved as a map.
        // Entry still supports the procedural fallback for old launch scripts.
        if (!TActorIterator<ABorn2FlapValley>(World))
            World->SpawnActor<ABorn2FlapValley>();
        World->SpawnActor<ABorn2FlapWindLeaves>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
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
    // shore lookout whose camera sits at the tideline). Float it just above the
    // surface instead of dropping the bird into the sea.
    if (Ground < WaterHeight())
        Ground = WaterHeight();
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
        // Shiomori: the launch beach + the six named scenic cameras in the
        // saved map. Each camera sits at a good vantage, so re-using its XY
        // gives natural, hand-launchable reset points along the shore.
        AddPoi(TEXT("poi.shiomori_beach"), 0.0, 0.0);
        static const TCHAR* Views[] = {
            TEXT("Bay overlook"), TEXT("Tidewalk"), TEXT("Basalt cove"),
            TEXT("Shoreline"), TEXT("East vegetation"), TEXT("West vegetation") };
        static const TCHAR* Keys[] = {
            TEXT("poi.bay_overlook"), TEXT("poi.tidewalk"), TEXT("poi.basalt_cove"),
            TEXT("poi.shoreline"), TEXT("poi.east_vegetation"), TEXT("poi.west_vegetation") };
        for (int32 I = 0; I < 6; ++I)
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
            if (PC->WasInputKeyJustPressed(EKeys::F10))
            {
                if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
                    if (Bird->IsPoiOverlayOpen())
                        Bird->ClosePoiOverlay();
                ShowMainMenu(true);
            }
            // F3 opens the same fullscreen menu, landing on SETTINGS → CONTROL
            // SETTINGS (the RC transmitter panel now lives inside the menu).
            // Guarded so the Brain-authored path keeps its own F3 handling.
            if (!IsBrainActive() && PC->WasInputKeyJustPressed(EKeys::F3))
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
            if (Proc->ConsumeF10())
                CloseMainMenu();
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
            bool Pass=MaskCenter && MaskSea && MaskRock && MaskNear;
            Pass &= WindNear>.3 && WindOff<.01;
            {
                FHitResult Hit;
                bool HitGround=GetWorld()->LineTraceSingleByChannel(Hit,FVector(2000,3000,700),FVector(2000,3000,-700),ECC_Visibility);
                Pass &= HitGround && FMath::Abs(Hit.ImpactPoint.Z-GroundHeight(2000,3000))<150;
                for(const FVector& P : {FVector(2000,-1750,150),FVector(2000,-2500,150)})
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
            const double Open=Born2FlapBay::WindExposure(FVector(0,65000,400),FVector(-1,0,0));
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
            if (PC->WasInputKeyJustPressed(EKeys::M)) RadioStation->ToggleMute();
            if (PC->WasInputKeyJustPressed(EKeys::LeftBracket)) RadioStation->AdjustVolume(-.08f);
            if (PC->WasInputKeyJustPressed(EKeys::RightBracket)) RadioStation->AdjustVolume(+.08f);
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

    if (Action == TEXT("menu.resume")) { CloseMainMenu(); return; }
    if (Action == TEXT("menu.settings"))
    {
        CloseMainMenu();
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->OpenFlightSettings();
        return;
    }
    if (Action == TEXT("menu.controls"))
    {
        // Open the RC control-settings panel from the fullscreen main menu.
        CloseMainMenu();
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            if (FBorn2FlapRcController* Rc = Bird->GetRcControllerMutable())
                Rc->OpenPanel();
        return;
    }

    // RC transmitter control-settings panel (semantic action surface — mirrors
    // the native keyboard driver TAB/C/ENTER/X/G/L/K in Born2FlapRcController).
    if (Action == TEXT("rc.close"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            if (FBorn2FlapRcController* Rc = Bird->GetRcControllerMutable())
                Rc->ClosePanel();
        return;
    }
    if (Action == TEXT("rc.device.next"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            if (FBorn2FlapRcController* Rc = Bird->GetRcControllerMutable())
                Rc->NextDevice();
        return;
    }
    if (Action == TEXT("rc.calibrate.start"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            if (FBorn2FlapRcController* Rc = Bird->GetRcControllerMutable())
                Rc->StartCalibration();
        return;
    }
    if (Action == TEXT("rc.calibrate.advance"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            if (FBorn2FlapRcController* Rc = Bird->GetRcControllerMutable())
                Rc->AdvanceCalibration();
        return;
    }
    if (Action == TEXT("rc.calibrate.cancel"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            if (FBorn2FlapRcController* Rc = Bird->GetRcControllerMutable())
                Rc->CancelCalibration();
        return;
    }
    if (Action == TEXT("rc.enable.toggle"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            if (FBorn2FlapRcController* Rc = Bird->GetRcControllerMutable())
                Rc->ToggleEnabled();
        return;
    }
    if (Action == TEXT("rc.learn.launch"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            if (FBorn2FlapRcController* Rc = Bird->GetRcControllerMutable())
                Rc->LearnLaunch();
        return;
    }
    if (Action == TEXT("rc.learn.reset"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            if (FBorn2FlapRcController* Rc = Bird->GetRcControllerMutable())
                Rc->LearnReset();
        return;
    }
    if (Action == TEXT("menu.page")) { MenuPage=FMath::Clamp(FMath::RoundToInt(Value),0,1); return; }
    if (Action == TEXT("settings.page")) { SettingsPage=FMath::Clamp(FMath::RoundToInt(Value),0,4); return; }
    if (Action == TEXT("settings.close") || Action == TEXT("editor.close"))
    {
        if (ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
            Bird->CloseFlightSettings();
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

    // Level travel (menu.<world>).
    struct FEntry { const TCHAR* ActionKey; const TCHAR* Map; const TCHAR* Level; };
    static const FEntry Entries[] = {
        { TEXT("menu.ravenstonefield"), TEXT("/Game/Ravenstonefield/Maps/RAVENSTONEFIELD"), TEXT("Ravenstonefield") },
        { TEXT("menu.shiomori"),        TEXT("/Game/Shiomori/Maps/SHIOMORI"),               TEXT("Shiomori") },
        { TEXT("menu.training"),        TEXT("/Engine/Maps/Entry"),                         TEXT("Training") },
    };
    for (const FEntry& E : Entries)
    {
        if (Action == E.ActionKey)
        {
            FString Options = Born2FlapWeather::TravelOptions(E.Level, MenuLevelConditions.FindChecked(E.Level));
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
            UGameplayStatics::OpenLevel(this, E.Map, true, Options);
            return;
        }
    }
}

FString ABorn2FlapGameMode::BuildBrainTelemetry() const
{
    TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject());

    ABorn2FlapFlightPawn* Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (Bird)
    {
        Root->SetNumberField(TEXT("altitude"), Bird->GetAltitude());
        Root->SetNumberField(TEXT("climb"), Bird->GetClimbRate());
        Root->SetNumberField(TEXT("speed"), Bird->GetSpeed());
        Root->SetNumberField(TEXT("battery"), Bird->GetBattery() * 100.0);
        Root->SetNumberField(TEXT("throttle"), Bird->GetEffort());
        Root->SetStringField(TEXT("status"), Bird->GetFlightStatus());
        Root->SetBoolField(TEXT("settings"), Bird->IsFlightSettingsOpen());
        Root->SetBoolField(TEXT("poi"), Bird->IsPoiOverlayOpen());
        // F7 hides the whole HUD layer (cockpit + radio). Mirrors the native
        // cockpit collapse in Born2FlapFlightHUD::Refresh().
        Root->SetBoolField(TEXT("hud"), !(Bird->IsBlindFlight() || Bird->IsFlightSettingsOpen() || Bird->IsUIHidden()));

        // Flight-desk settings state (CRAFT / BIRD / CONTROLS / ASSIST / TUNING).
        Root->SetNumberField(TEXT("bird_model"), Bird->GetBirdModel());
        Root->SetBoolField(TEXT("camera_fpv"), Bird->IsFpvAirView());
        Root->SetNumberField(TEXT("fpv_angle"), Bird->GetFpvCameraAngle());
        Root->SetNumberField(TEXT("body_mass_kg"), Bird->GetBodyMassKg());
        Root->SetNumberField(TEXT("cg_offset_mm"), Bird->GetCgOffsetMm());
        Root->SetBoolField(TEXT("coupled_throttle"), Bird->IsThrottleCoupled());
        Root->SetNumberField(TEXT("speed_modifier"), Bird->GetSpeedModifier());
        Root->SetNumberField(TEXT("flight_safety"), Bird->GetFlightSafety());
        Root->SetBoolField(TEXT("replay_spirits"), Bird->GetReplaySpiritsEnabled());
        Root->SetNumberField(TEXT("control_expo"), Bird->GetControlExpo());
        {
            const FVector Gains = Bird->GetMouseGains();
            TArray<TSharedPtr<FJsonValue>> GainArr;
            for (int32 Axis = 0; Axis < 3; ++Axis)
                GainArr.Add(MakeShareable(new FJsonValueNumber((double)Gains[Axis])));
            Root->SetArrayField(TEXT("mouse_gains"), GainArr);
        }
        for (uint8 I = 0; I < (uint8)ETuningField::Count; ++I)
        {
            const ETuningField Field = (ETuningField)I;
            Root->SetNumberField(born2flap::tuning::Key(Field), Bird->GetTuning(Field));
        }

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

        // RC sender (F3 panel) + live channels.
        const FBorn2FlapRcController* Rc = Bird->GetRcController();
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
    }

    // Menu / level selector.
    Root->SetBoolField(TEXT("menu"), bMenuOpen);
    Root->SetBoolField(TEXT("menu_inlevel"), bMenuInLevel);
    Root->SetNumberField(TEXT("world_index"), MenuWorldIndex);
    Root->SetNumberField(TEXT("menu_page"), MenuPage);
    Root->SetNumberField(TEXT("settings_page"), SettingsPage);

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
        TArray<TSharedPtr<FJsonValue>> WeatherOpts, TimeOpts;
        for (const FString& S : Born2FlapWeather::WeatherOptions(W.Id))
            WeatherOpts.Add(MakeShareable(new FJsonValueString(S)));
        for (const FString& S : Born2FlapWeather::TimeOptions(W.Id, C.Weather))
            TimeOpts.Add(MakeShareable(new FJsonValueString(S)));
        WObj->SetArrayField(TEXT("weathers"), WeatherOpts);
        WObj->SetArrayField(TEXT("times"), TimeOpts);
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
    Root->SetNumberField(TEXT("radio_volume"), RadioStation ? RadioStation->GetVolume() : 0.5);
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