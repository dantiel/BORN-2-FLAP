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
#include "World/Born2FlapValley.h"
#include "World/Born2FlapWind.h"
#include "World/Born2FlapWindLeaves.h"
#include "UI/Born2FlapUIBridge.h"
#include "Racing/Born2FlapRacing.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
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
    FString Level = UGameplayStatics::ParseOption(Options, TEXT("Level"));
    if (Level.IsEmpty())
        FParse::Value(FCommandLine::Get(), TEXT("B2FLevel="), Level);
    bCoastLevel = Level.Equals(TEXT("Shiomori"),ESearchCase::IgnoreCase) || (Level.IsEmpty() && MapName.Contains(TEXT("SHIOMORI")));
    bNatureLevel = !Level.Equals(TEXT("Training"), ESearchCase::IgnoreCase) &&
                   !FParse::Param(FCommandLine::Get(), TEXT("B2FFlightTest")) &&
                   !FParse::Param(FCommandLine::Get(), TEXT("B2FSoakTest")) &&
                   !FParse::Param(FCommandLine::Get(), TEXT("B2FHandlingTest"));
}
double ABorn2FlapGameMode::GroundHeight(double X, double Y) const
{
    if(bCoastLevel)
    {
        const double Island=FMath::Square((X+37000)/8500.)+FMath::Square((Y-26000)/13500.);
        if(Island<1) return -600+2200*FMath::Sqrt(1-Island);
        if(FMath::Abs(X)<=75000 && Y>=-51000 && Y<=-3000) return 150;
        if(FMath::Abs(X)>45000 || Y>10000 || Y<-3500) return -400;
        if(Y>=-1200) return 0;
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
    UWorld *World = GetWorld();
    World->GetWorldSettings()->bForceNoPrecomputedLighting = true;
    // Wire the Ruby Brain ↔ UMG transport (react-native-umg). The bridge tails
    // the NDJSON frame source and applies frames to the UMG renderer each tick;
    // it is a no-op unless -B2FUIFile=/-B2FUIBrain= is supplied.
    {
        FActorSpawnParameters UIParams;
        UIParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        World->SpawnActor<ABorn2FlapUIBridge>(FVector::ZeroVector, FRotator::ZeroRotator, UIParams);
    }
    // Racing mode: replay-spirit recorder + manager. Records every round and
    // re-flies the past rounds as shadow doppelgängers (disabled in flight tests).
    {
        FActorSpawnParameters RacingParams;
        RacingParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        World->SpawnActor<ABorn2FlapRacingManager>(FVector::ZeroVector, FRotator::ZeroRotator, RacingParams);
    }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if(bCoastLevel) return; // Saved coastal map supplies geometry and atmosphere.
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
    for (const FVector &Centre : Gates)
        for (int I = 0; I < 32; ++I)
        {
            const double A = I * 2 * PI / 32;
            Part(Cube, Centre + FVector(0, 280 * FMath::Cos(A), 280 * FMath::Sin(A)), FVector(.16, .58, .16),
                 FRotator(0, 0, -FMath::RadiansToDegrees(A) - 90), TEXT("Gold"));
        }
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
void ABorn2FlapGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(bCoastLevel && FParse::Param(FCommandLine::Get(),TEXT("B2FCoastTest")))
    {
        CoastTestTime+=DeltaSeconds;
        auto* PC=UGameplayStatics::GetPlayerController(this,0);
        const TCHAR* Views[]={TEXT("Bay overlook"),TEXT("Tidewalk"),TEXT("Basalt cove")};
        if(CoastCaptureStage<6 && CoastTestTime>2+CoastCaptureStage*2)
        {
            if(CoastCaptureStage%2==0)
            {
                for(TActorIterator<ACameraActor> It(GetWorld());It;++It)
                    if(It->ActorHasTag(Views[CoastCaptureStage/2]) && PC) PC->SetViewTarget(*It);
            }
            else FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("SHIOMORI_%d.png"),CoastCaptureStage/2),false,false);
            ++CoastCaptureStage;
        }
        if(CoastTestTime>15)
        {
            bool Pass=!IsWater(0,0) && IsWater(0,20000) && !IsWater(-37000,26000) && IsWater(-37000,11200);
            Pass &= WindAt(FVector(0,-1400,350),0).Z>.3 && WindAt(FVector(0,6000,350),0).Z<.01;
            for(const FVector& P : {FVector(2000,3000,0),FVector(2000,-1750,150),FVector(2000,-2500,150)})
            {
                FHitResult Hit;
                const bool HitGround=GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(0,0,700),P-FVector(0,0,700),ECC_Visibility);
                Pass &= HitGround && FMath::Abs(Hit.ImpactPoint.Z-P.Z)<3;
            }
            int32 Shelters=0,Posts=0,Rocks=0;
            for(TActorIterator<AStaticMeshActor> It(GetWorld());It;++It)
            { Shelters+=It->ActorHasTag(TEXT("Shelter floating roof")); Posts+=It->ActorHasTag(TEXT("Volleyball post")); Rocks+=It->ActorHasTag(TEXT("Volcanic outcrop")); }
            Pass &= Shelters==9 && Posts==2 && Rocks==60;
            UE_LOG(LogTemp,Display,TEXT("ShiomoriTest %s: sand/stair/promenade collision, bay/island water mask, stair lift, shelters=%d posts=%d volcanic rocks=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Shelters,Posts,Rocks);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
        }
        return;
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
    if (!Bird->IsFlying() && Bird->GetActorLocation().Size2D() < 100)
        GatesPassed = 0;
    if (Gates.IsValidIndex(GatesPassed) && FVector::Dist(Bird->GetActorLocation(), Gates[GatesPassed]) < 260)
    {
        ++GatesPassed;
        UE_LOG(LogTemp, Display, TEXT("FlightGate passed=%d total=%d"), GatesPassed, Gates.Num());
    }
}