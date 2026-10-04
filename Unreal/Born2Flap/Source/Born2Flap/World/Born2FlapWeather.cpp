#include "World/Born2FlapWeather.h"
#include "Game/Born2FlapGameMode.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/ExponentialHeightFog.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"

ABorn2FlapWeather::ABorn2FlapWeather()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("WeatherRoot"));
    Precipitation=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Precipitation"));
    Precipitation->SetupAttachment(RootComponent);
    Precipitation->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Precipitation->SetCastShadow(false);
    Clouds=CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("WeatherClouds"));
    Clouds->SetupAttachment(RootComponent);
    Clouds->SetVisibility(false);
    Clouds->SetMaterial(LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst")));
    PostProcess=CreateDefaultSubobject<UPostProcessComponent>(TEXT("WeatherExposure"));
    PostProcess->SetupAttachment(RootComponent);
    PostProcess->bUnbound=true;
    PostProcess->Priority=100.f;
}
void ABorn2FlapWeather::Configure(const FString& InLevel,const FBorn2FlapConditions& InConditions)
{
    Level=InLevel;
    Conditions=Born2FlapWeather::Validate(Level,InConditions);
    bApplied=false;
}
void ABorn2FlapWeather::Apply()
{
    const auto& W=Born2FlapWeather::Profile(Conditions.Weather);
    const auto& D=Born2FlapWeather::DayTime(Conditions.Time);
    const FRotator SunRotation(-D.Elevation,D.Yaw,0);
    const FVector SunDirection=-SunRotation.Vector();
    const FLinearColor LightColor=D.Night ? FLinearColor(.52f,.67f,1.f) :
        ((D.Id==TEXT("dawn") || D.Id==TEXT("sunset")) ? FLinearColor(1.f,.64f,.36f) : FLinearColor(1.f,.94f,.84f));
    bool bFogFound=false;
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        TInlineComponentArray<UDirectionalLightComponent*> Suns(*It);
        for(auto* Sun:Suns)
        {
            if(!Sun->bAtmosphereSunLight) continue;
            Sun->SetMobility(EComponentMobility::Movable);
            Sun->SetWorldRotation(SunRotation);
            Sun->SetIntensity(D.Lux*W.Sunlight);
            Sun->SetLightColor(LightColor);
        }
        TInlineComponentArray<USkyLightComponent*> Skies(*It);
        for(auto* Sky:Skies)
        {
            Sky->SetMobility(EComponentMobility::Movable);
            Sky->SetRealTimeCapture(true);
            Sky->SetIntensity(D.Night ? .7f : 1.2f);
        }
        TInlineComponentArray<UExponentialHeightFogComponent*> Fogs(*It);
        for(auto* Fog:Fogs)
        {
            bFogFound=true;
            Fog->SetFogDensity(W.Fog);
            Fog->SetFogHeightFalloff(W.Id==TEXT("mist") ? .12f : .25f);
            Fog->SetFogInscatteringColor(D.Night ? FLinearColor(.08f,.12f,.23f) : FLinearColor(.57f,.66f,.73f));
            Fog->SetVolumetricFog(true);
        }
        // The authored cloud props must not survive a clear-weather selection.
        if(It->GetName().Contains(TEXT("Cloud")) && *It!=this)
            It->SetActorHiddenInGame(true);
    }
    if(!bFogFound)
    {
        auto* Fog=GetWorld()->SpawnActor<AExponentialHeightFog>();
        Fog->GetComponent()->SetFogDensity(W.Fog);
    }
    auto& S=PostProcess->Settings;
    const auto* Range=IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
    const float EV=Born2FlapWeather::Exposure(Conditions);
    const float Brightness=Range && Range->GetInt() ? EV : FMath::Pow(2.f,EV);
    S.bOverride_AutoExposureMinBrightness=S.bOverride_AutoExposureMaxBrightness=S.bOverride_AutoExposureBias=true;
    S.AutoExposureMinBrightness=S.AutoExposureMaxBrightness=Brightness;
    S.AutoExposureBias=0;
    S.WeightedBlendables.Array.Reset();
    if(W.Parhelion && !D.Night)
    {
        if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Shiomori/Materials/M_SunParhelion")))
        {
            HaloMaterial=UMaterialInstanceDynamic::Create(Material,this);
            HaloMaterial->SetVectorParameterValue(TEXT("SunDirection"),FLinearColor(SunDirection));
            S.AddBlendable(HaloMaterial,1.f);
        }
    }
    if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst")))
    {
        CloudMaterial=UMaterialInstanceDynamic::Create(Material,this);
        // Engine material coverage is a noise bias (default -.2), density is
        // extinction per unit distance (default .008), not normalized opacity.
        CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"),FMath::Lerp(-.6f,.05f,W.Cloud));
        CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"),.003f+.009f*W.Cloud);
        CloudMaterial->SetScalarParameterValue(TEXT("StormClouds"),0.f);
        CloudMaterial->SetVectorParameterValue(TEXT("Cloud_AlbedoColor"),FLinearColor(.98f,.98f,.98f,.5f));
        Clouds->SetMaterial(CloudMaterial);
        Clouds->SetLayerBottomAltitude(W.Rain || W.Snow ? .6f : 1.5f);
        Clouds->SetLayerHeight(W.Rain || W.Snow ? 2.5f : 1.5f);
        Clouds->SetVisibility(W.Cloud>.2f);
    }
    Precipitation->ClearInstances();
    Positions.Reset(); Floors.Reset();
    const int32 Count=W.Rain ? 480 : (W.Snow ? 300 : 0);
    if(Count)
    {
        Precipitation->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,W.Snow ? TEXT("/Engine/BasicShapes/Sphere") : TEXT("/Engine/BasicShapes/Cube")));
        Precipitation->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,W.Snow ? TEXT("/Game/Weather/M_Snow") : TEXT("/Game/Weather/M_Rain")));
        Positions.SetNum(Count); Floors.SetNum(Count);
        auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);
        const FVector Center=Camera ? Camera->GetCameraLocation() : FVector(0,0,500);
        for(int32 I=0;I<Count;++I)
        {
            Respawn(I,Center);
            Positions[I].Z=FMath::Lerp(FMath::Max(double(Floors[I]+10),Center.Z-1000),Positions[I].Z,Random.FRand());
            Precipitation->AddInstance(FTransform(FQuat::Identity,Positions[I],FVector(.01)));
        }
    }
    bApplied=true;
    UE_LOG(LogTemp,Display,TEXT("WeatherApplied level=%s weather=%s time=%s wind=%.2fm/s gust=%.2f particles=%d EV=%.2f"),
        *Level,*Conditions.Weather,*Conditions.Time,W.WindSpeed,W.Gust,Count,EV);
}
void ABorn2FlapWeather::Respawn(int32 Index,const FVector& Camera)
{
    Positions[Index]=Camera+FVector(Random.FRandRange(-1200,1200),Random.FRandRange(-1200,1200),Random.FRandRange(800,1800));
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WeatherShelter),false,this);
    if(auto* Pawn=UGameplayStatics::GetPlayerPawn(this,0)) Query.AddIgnoredActor(Pawn);
    const FVector P=Positions[Index];
    if(GetWorld()->LineTraceSingleByChannel(Hit,P,P-FVector(0,0,10000),ECC_Visibility,Query))
        Floors[Index]=Hit.ImpactPoint.Z;
    else if(auto* GM=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
        Floors[Index]=FMath::Max(GM->GroundHeight(P.X,P.Y),GM->IsWater(P.X,P.Y) ? GM->WaterHeight() : -100000.);
    else Floors[Index]=Camera.Z-3000;
}
void ABorn2FlapWeather::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(Level.IsEmpty()) return;
    if(!bApplied) Apply();
    Elapsed+=DeltaSeconds;
    const double Time=GetWorld()->GetTimeSeconds();
    const auto& W=Born2FlapWeather::Profile(Conditions.Weather);
    const FVector Flow=Born2FlapWeather::Wind(Level,Conditions,GetActorLocation(),Time);
    if(CloudMaterial) CloudMaterial->SetVectorParameterValue(TEXT("Layout_WindControls"),FLinearColor(Flow.X*.1,Flow.Y*.1,.5f,.333f));
    if(auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0))
    {
        const FVector Center=Camera->GetCameraLocation();
        for(int32 I=0;I<Positions.Num();++I)
        {
            FVector Velocity=Born2FlapWeather::Wind(Level,Conditions,Positions[I],Time)*100;
            Velocity.Z=W.Snow ? -100 : -850;
            if(W.Snow) Velocity.X+=35*FMath::Sin(Time*1.8+I);
            Positions[I]+=Velocity*FMath::Min(DeltaSeconds,.1f);
            if(Positions[I].Z<Floors[I]+5 || Positions[I].Z<Center.Z-1200 || FVector2D(Positions[I]-Center).Size()>1800)
                Respawn(I,Center);
            const FVector Scale=W.Snow ? FVector(.035+.025*(I%7)/6.) : FVector(.004,.004,.30);
            Precipitation->UpdateInstanceTransform(I,FTransform(FRotationMatrix::MakeFromZ(Velocity).ToQuat(),Positions[I],Scale),true,false,false);
        }
        if(Positions.Num()) Precipitation->MarkRenderStateDirty();
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FWeatherTest"))) CheckTest();
}
void ABorn2FlapWeather::CheckTest()
{
    if(Elapsed>2 && !bCaptured)
    {
        auto* PC=UGameplayStatics::GetPlayerController(this,0);
        auto* View=GetWorld()->SpawnActor<ACameraActor>();
        const FVector Location=Level==TEXT("Shiomori") ? FVector(0,-6000,550) : FVector(0,0,1800);
        View->SetActorLocation(Location);
        View->SetActorRotation(FRotator(12,Level==TEXT("Shiomori") ? 90 : 20,0));
        PC->SetViewTarget(View);
        bCaptured=true;
    }
    if(Elapsed>5 && !bTestDone)
    {
        FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("WEATHER_%s_%s_%s.png"),*Level,*Conditions.Weather,*Conditions.Time),false,false);
        bool Pass=bApplied;
        for(const FString L:{FString(TEXT("Ravenstonefield")),FString(TEXT("Shiomori")),FString(TEXT("Training"))})
            for(const FString W:Born2FlapWeather::WeatherOptions(L))
                for(const FString T:Born2FlapWeather::TimeOptions(L,W))
                {
                    const FBorn2FlapConditions C{W,T};
                    const FString Options=Born2FlapWeather::TravelOptions(L,C);
                    Pass &= UGameplayStatics::ParseOption(TEXT("?")+Options,TEXT("Weather"))==W;
                    Pass &= UGameplayStatics::ParseOption(TEXT("?")+Options,TEXT("DayTime"))==T;
                    for(int32 I=0;I<12;++I)
                    {
                        const FVector P(I*320.,I*-640.,I*350.);
                        const FVector Wind=Born2FlapWeather::Wind(L,C,P,I*17.);
                        Pass &= !Wind.ContainsNaN() && Wind.Size()<20.;
                        Pass &= Wind.Equals(Born2FlapWeather::Wind(L,C,P,I*17.),1e-8);
                    }
                }
        const auto Invalid=Born2FlapWeather::Validate(TEXT("Training"),{TEXT("snow"),TEXT("night")});
        Pass &= Invalid.Weather==TEXT("sunny") && Invalid.Time==TEXT("noon");
        Pass &= Positions.Num()==(Born2FlapWeather::Profile(Conditions.Weather).Rain ? 480 : (Born2FlapWeather::Profile(Conditions.Weather).Snow ? 300 : 0));
        UE_LOG(LogTemp,Display,TEXT("WeatherTest %s: profile restrictions, travel options, finite deterministic wind and precipitation"),Pass ? TEXT("PASS") : TEXT("FAIL"));
        bTestDone=true;
    }
    if(Elapsed>7) FPlatformMisc::RequestExit(false);
}
