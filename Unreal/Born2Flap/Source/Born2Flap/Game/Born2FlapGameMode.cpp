#include "Game/Born2FlapGameMode.h"
#include "Game/Born2FlapHUD.h"
#include "Flight/Born2FlapFlightPawn.h"
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
#include "Game/Born2FlapPoiBeacon.h"
#include "Racing/Born2FlapRacing.h"
#include "Water/Born2FlapWaterDirector.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"
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
    // The main menu only appears on a plain boot (no explicit level, no test
    // mode). ?SkipMenu=1 (set by the menu itself) suppresses it on reload.
    bSkipMenu = FParse::Param(FCommandLine::Get(), TEXT("B2FNoMenu")) ||
                UGameplayStatics::ParseOption(Options, TEXT("SkipMenu")).Equals(TEXT("1"), ESearchCase::IgnoreCase);
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
        const double Island=FMath::Square((X+37000)/8500.)+FMath::Square((Y-26000)/13500.);
        if(Island<1) return -600+2200*FMath::Sqrt(1-Island);
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
bool ABorn2FlapGameMode::IsWater(double X,double Y) const
{
    return bCoastLevel ? GroundHeight(X,Y)<-50 : bNatureLevel && ABorn2FlapValley::IsWater(X,Y);
}
double ABorn2FlapGameMode::WaterHeight() const { return bCoastLevel ? -50 : ABorn2FlapValley::WaterHeight; }
FVector ABorn2FlapGameMode::WindAt(const FVector& P,double Time) const
{
    if(!bCoastLevel) return Born2FlapWind::Sample(P,Time);
    const double Gust=.3*FMath::Sin(Time*.7+P.X*.0003);
    // Onshore sea breeze meets the 1.5 m stepped seawall. A small, localized
    // aerodynamic updraft decays inland, above the wall, and beyond its ends.
    const double Lift=.85*FMath::Exp(-FMath::Square((P.Y+1400)/650.))*FMath::Exp(-FMath::Max(0.,P.Z-150)/650.)*
        (1-FMath::SmoothStep(43000.,46000.,FMath::Abs(P.X)));
    return FVector(.3+Gust,-3.2-Gust,Lift);
}
void ABorn2FlapGameMode::BeginPlay()
{
    Super::BeginPlay();
    // Points of interest are level-specific and only need the level flags
    // (set in InitGame) plus the saved-map camera actors, so they can be built
    // before the level geometry branches below.
    PopulatePOIs();
    SpawnPoiBeacons();
    // Startup splash screen: show the artwork + loading bar on real launches.
    // Automated runs (tests) pass -nosplash/-unattended and skip it entirely.
    // Spawned here (not on the first Tick) so it is already in the viewport for
    // the very first rendered frame — otherwise the freshly loaded world shows
    // for a beat before the splash covers it. The renderer only needs GetWorld(),
    // no player controller, so BeginPlay is safe.
    if (!FParse::Param(FCommandLine::Get(), TEXT("nosplash")) && !FApp::IsUnattended())
    {
        FActorSpawnParameters SplashParams;
        SplashParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SplashWidget = GetWorld()->SpawnActor<ABorn2FlapSplash>(FVector::ZeroVector, FRotator::ZeroRotator, SplashParams);
        if (SplashWidget)
            SplashElapsed = 0.f;
    }
    UWorld *World = GetWorld();
    World->GetWorldSettings()->bForceNoPrecomputedLighting = true;
    // Wire the Ruby Brain ↔ UMG transport. The bridge launches the Ruby Brain
    // (default: <repo>/Brain/bin/umghaml_brain) which authors the glass cockpit
    // in UMGHAML; the bridge tails the resulting NDJSON frames and feeds live
    // telemetry back each tick.
    ABorn2FlapUIBridge* UIBridge = nullptr;
    {
        FActorSpawnParameters UIParams;
        UIParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        UIBridge = World->SpawnActor<ABorn2FlapUIBridge>(FVector::ZeroVector, FRotator::ZeroRotator, UIParams);
    }
    // The glass cockpit is UMGHAML-authored by the Ruby Brain. Only fall back to
    // the native C++ cockpit when the Brain is not running (packaged build
    // without Ruby, or an explicit -NoRubyUI override).
    if (!UIBridge || !UIBridge->IsBrainActive())
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
        if (RadioStation->HasTrack())
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
    // Painted sky gates: the user's circle artwork floats in the sky as a
    // non-colliding, two-sided billboard. Each gate gets its own dynamic tint
    // so flying through it can recolour it (amber -> green).
    auto *Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    auto *GateMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/UI/M_Gate"));
    GateActors.Reset(Gates.Num());
    GateMaterials.Reset(Gates.Num());
    for (const FVector &Centre : Gates)
    {
        auto *Actor = World->SpawnActor<AStaticMeshActor>(Centre, FRotator(90, 0, 0), Params);
        auto *Component = Actor->GetStaticMeshComponent();
        Component->SetMobility(EComponentMobility::Movable);
        if (Plane)
            Component->SetStaticMesh(Plane);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
    // The next gate reads bright warm-white ("fly here"), passed gates turn
    // emerald green, and gates still ahead stay amber. Missing materials are
    // tolerated so the course still works before the import step has run.
    for (int32 I = 0; I < GateMaterials.Num(); ++I)
    {
        UMaterialInstanceDynamic *DMI = GateMaterials[I];
        if (!DMI)
            continue;
        FLinearColor Tint;
        if (I < GatesPassed)
            Tint = FLinearColor(0.35f, 0.95f, 0.50f);   // passed: emerald
        else if (I == GatesPassed)
            Tint = FLinearColor(1.00f, 0.95f, 0.75f);   // active: warm white
        else
            Tint = FLinearColor(0.95f, 0.62f, 0.22f);   // ahead: amber
        DMI->SetVectorParameterValue(TEXT("Tint"), Tint);
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

void ABorn2FlapGameMode::SpawnPoiBeacons()
{
    UWorld* World = GetWorld();
    PoiBeacons.Reset(POIs.Num());
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (const FBorn2FlapPoi& P : POIs)
    {
        // Localize the name once at spawn; the beacon caches the label text.
        FString Label = Born2Flap::I18n::T(P.Key);
        if (P.Number > 0)
            Label += TEXT(" ") + FString::FromInt(P.Number);
        auto* Beacon = World->SpawnActor<ABorn2FlapPoiBeacon>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
        if (Beacon)
        {
            Beacon->Initialize(Label, P.Position);
            PoiBeacons.Add(Beacon);
        }
    }
}

void ABorn2FlapGameMode::HighlightPoi(int32 Index)
{
    for (int32 I = 0; I < PoiBeacons.Num(); ++I)
        if (PoiBeacons[I])
            PoiBeacons[I]->SetSelected(I == Index);
}

void ABorn2FlapGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // Startup splash: ease the loading bar to full, fade out, then remove.
    // (The actor is spawned in BeginPlay so it already covers the first frame.)
    if (SplashWidget)
    {
        static constexpr float LoadDuration = 2.4f;
        static constexpr float FadeDuration = 0.5f;
        SplashElapsed += DeltaSeconds;
        const float T = FMath::Clamp(SplashElapsed / LoadDuration, 0.f, 1.f);
        SplashWidget->SetProgress(T * T * (3.f - 2.f * T)); // smoothstep
        if (SplashElapsed >= LoadDuration)
        {
            const float FadeT = FMath::Clamp((SplashElapsed - LoadDuration) / FadeDuration, 0.f, 1.f);
            SplashWidget->SetOpacity(1.f - FadeT);
        }
        if (SplashElapsed >= LoadDuration + FadeDuration)
        {
            SplashWidget->Destroy();
            SplashWidget = nullptr;
            SplashElapsed = -1.f;
            // Splash done — on a plain boot (no explicit level) open the main
            // menu / level selector over the world that has loaded behind it.
            if (!bSkipMenu)
            {
                FActorSpawnParameters MenuParams;
                MenuParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                MenuWidget = GetWorld()->SpawnActor<ABorn2FlapMenu>(FVector::ZeroVector, FRotator::ZeroRotator, MenuParams);
            }
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
            bool Pass=!IsWater(0,0) && IsWater(0,20000) && !IsWater(-37000,26000) && IsWater(-37000,11200);
            Pass &= WindAt(FVector(0,-1400,350),0).Z>.3 && WindAt(FVector(0,6000,350),0).Z<.01;
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
            int32 West=0,East=0;
            int32 Shelters=0,Posts=0,Rocks=0;
            for(TActorIterator<AStaticMeshActor> It(GetWorld());It;++It)
            { West+=It->ActorHasTag(TEXT("Coastal grass west")); East+=It->ActorHasTag(TEXT("Coastal grass east")); Shelters+=It->ActorHasTag(TEXT("Shelter floating roof")); Posts+=It->ActorHasTag(TEXT("Volleyball post")); Rocks+=It->ActorHasTag(TEXT("Volcanic outcrop")); }
            Pass &= Shelters==9 && Posts==2 && Rocks==60 && West>500 && East>500;
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
            if (PC->WasInputKeyJustPressed(EKeys::M)) RadioStation->TogglePlayPause();
            if (PC->WasInputKeyJustPressed(EKeys::LeftBracket)) RadioStation->AdjustVolume(-.08f);
            if (PC->WasInputKeyJustPressed(EKeys::RightBracket)) RadioStation->AdjustVolume(+.08f);
        }
    }

    if (auto *Player = UGameplayStatics::GetPlayerController(this, 0))
        if (Player->WasInputKeyJustPressed(EKeys::F4))
        {
            const TCHAR* Next=bCoastLevel?TEXT("Training"):bNatureLevel?TEXT("Shiomori"):TEXT("Ravenstonefield");
            const TCHAR* Map=bCoastLevel?TEXT("/Engine/Maps/Entry"):bNatureLevel?TEXT("/Game/Shiomori/Maps/SHIOMORI"):TEXT("/Game/Ravenstonefield/Maps/RAVENSTONEFIELD");
            UE_LOG(LogTemp,Display,TEXT("FlightLevel switch=%s"),Next);
            UGameplayStatics::OpenLevel(this,Map,true,FString(TEXT("Level="))+Next);
            return;
        }
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
}
