#include "World/Born2FlapYunsha.h"
#include "Game/Born2FlapGameMode.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundWave.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/HUD.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

namespace {
double Smooth(double A,double B,double V){const double T=FMath::Clamp((V-A)/(B-A),0.,1.);return T*T*(3-2*T);}
const FVector ThermalCentres[]={FVector(-12000,-42000,0),FVector(13000,55000,0),FVector(-16000,204800,0),FVector(30400,269600,0),FVector(-9600,339800,0)};
}
ABorn2FlapYunsha::ABorn2FlapYunsha()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("YunshaRoot"));
    LiftMotes=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("LiftMotes"));
    LiftMotes->SetupAttachment(RootComponent);LiftMotes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    LiftMotes->SetCastShadow(false);LiftMotes->SetMobility(EComponentMobility::Movable);
    Harp=CreateDefaultSubobject<UAudioComponent>(TEXT("WindHarp"));
    Bell=CreateDefaultSubobject<UAudioComponent>(TEXT("SummitBell"));
    for(auto* Audio:{Harp.Get(),Bell.Get()}){Audio->SetupAttachment(RootComponent);Audio->bAutoActivate=false;Audio->bAllowSpatialization=false;}
}
double ABorn2FlapYunsha::CanyonCentre(double Y){return 6000*FMath::Sin(Y*.000025);}
double ABorn2FlapYunsha::CanyonWidth(double Y){return 36000-29000*FMath::Exp(-FMath::Square((Y-74000)/13000));}
bool ABorn2FlapYunsha::SpringContains(double X,double Y)
{
    const double A=FMath::Square(X/15600)+FMath::Square((Y+52000)/9600);
    const double B=FMath::Square((X-5200)/13600)+FMath::Square((Y+50400)/8400);
    return A<1 && B>1;
}
double ABorn2FlapYunsha::GroundHeight(double X,double Y)
{
    // Domain-warped transverse dunes blend into a second seasonal wind field.
    // A long stoss slope and short slip face replace the old low sine bumps.
    auto Dune=[](double Phase){
        const double T=Phase-FMath::FloorToDouble(Phase);
        return T<.78 ? Smooth(0.,.78,T) : 1-Smooth(.78,1.,T);
    };
    const double Warp=1600*FMath::Sin(Y*.000041)+900*FMath::Sin(X*.000027+Y*.000019);
    const double Primary=Dune((X*.85+Y*.53+Warp)/11500);
    const double Secondary=Dune((-X*.46+Y*.89+1100*FMath::Sin(X*.000055))/7800);
    const double WindMix=Smooth(-.6,.6,FMath::Sin(X*.000013-Y*.000009));
    const double Envelope=850+650*(.5+.5*FMath::Sin(X*.000018+Y*.000023));
    double Height=100+Envelope*FMath::Lerp(Primary,Secondary,WindMix)
        +180*Dune((X*.96-Y*.28+Warp*.3)/3200);
    const double Oasis=1-Smooth(1.,2.3,FMath::Sqrt(FMath::Square((X-4000)/23000)+FMath::Square((Y+50000)/19500)));
    Height=FMath::Lerp(Height,140.,Oasis);
    // The crescent is excavated in the same sampler used by generation.
    const double Outer=FMath::Square(X/15600)+FMath::Square((Y+52000)/9600);
    const double Inner=FMath::Square((X-5200)/13600)+FMath::Square((Y+50400)/8400);
    const double Basin=(1-Smooth(.90,1.04,Outer))*Smooth(.93,1.04,Inner);
    Height=FMath::Lerp(Height,-220.,Basin);
    // The Bone Archive occupies a dry fossil field east of the pilgrimage
    // floor. Its excavation is terrain, not a pit mesh covering intact ground.
    X-=100000; // Bone Archive moved one kilometre east; local pit dimensions retained.
    const double Field=FMath::Sqrt(FMath::Square((X-47000)/23000)+FMath::Square((Y+11000)/16500));
    Height=FMath::Lerp(Height,100.,1-Smooth(.83,1.12,Field));
    const double DX=(X-47000)/13500,DY=(Y+11000)/9500;
    const double Angle=FMath::Atan2(DY,DX);
    const double R=FMath::Pow(FMath::Pow(FMath::Abs(DX),4)+FMath::Pow(FMath::Abs(DY),4),.25)
        +.045*FMath::Sin(Angle*3)+.025*FMath::Sin(Angle*7+.5);
    const double Cut=500*(1-Smooth(.92,1.,R))+650*(1-Smooth(.65,.73,R))+650*(1-Smooth(.36,.44,R));
    // A broad access ramp replaces the eastern bench faces, retaining the
    // full depth at its foot. Other faces expose the sediment sequence.
    const double Ramp=1-Smooth(.08,.23,FMath::Abs(Angle));
    const double RampCut=1800*(1-Smooth(.20,1.,R));
    Height-=FMath::Lerp(Cut,RampCut,Ramp);
    // A separate shallow exploratory trench preserves a rib of untouched rock.
    const double Trench=FMath::Max(FMath::Abs((X-30000)/3600),FMath::Abs((Y+15000)/6000));
    Height-=600*(1-Smooth(.82,1.,Trench));
    // Rectangular survey trenches on the southern apron; their floors and
    // sediment walls are shared by rendering, collision and flight queries.
    for(const FVector4 Pit:{FVector4(55000,-22000,1800,1200),FVector4(50000,-25000,1400,1000),FVector4(59000,-18000,1000,800)})
    {
        const double Rect=FMath::Max(FMath::Abs((X-Pit.X)/Pit.Z),FMath::Abs((Y-Pit.Y)/Pit.W));
        Height-=(Pit.Z*.2)*(1-Smooth(.80,1.,Rect));
    }

    return Height;
}
FVector ABorn2FlapYunsha::SoaringWind(const FVector& P,double Time,const FBorn2FlapConditions& C,FVector Ambient)
{
    const double Thermal=C.WindThermal>=0?C.WindThermal:1.;
    const double Season=C.Season==TEXT("cool")?.40:C.Season==TEXT("monsoon")?.65:1.;
    const double Sun=C.Time==TEXT("night")?.025:C.Time==TEXT("noon")?1.:.65;
    const double Alt=FMath::Max(0.,P.Z-GroundHeight(P.X,P.Y));
    for(const FVector& Centre:ThermalCentres)
    {
        const double Radius=2600+Alt*.075;
        const double R2=FVector2D(P.X-Centre.X,P.Y-Centre.Y).SizeSquared()/(Radius*Radius);
        const double Core=FMath::Exp(-R2);
        const double Ring=.16*FMath::Exp(-FMath::Square((FMath::Sqrt(R2)-1.7)/.65));
        Ambient.Z+=8.5*(Core-Ring)*Season*Sun*Thermal*(1-Smooth(65000,85000,Alt))*(.92+.08*FMath::Sin(Time*.09));
    }
    // Direction-aware upwash on windward slopes, downdraft on lee slopes.
    const double DX=(GroundHeight(P.X+600,P.Y)-GroundHeight(P.X-600,P.Y))/1200;
    const double DY=(GroundHeight(P.X,P.Y+600)-GroundHeight(P.X,P.Y-600))/1200;
    Ambient.Z+=FMath::Clamp((Ambient.X*DX+Ambient.Y*DY)*1.2,-4.,6.)*FMath::Exp(-Alt/6500);
    // Constriction accelerates the existing wind (zero wind remains zero).
    const double Slot=FMath::Exp(-FMath::Square((P.Y-74000)/11000))*FMath::Exp(-FMath::Square((P.X-CanyonCentre(P.Y))/9000));
    Ambient.X*=1+1.8*Slot;Ambient.Y*=1+1.8*Slot;
    return Ambient;
}
void ABorn2FlapYunsha::BeginPlay()
{
    Super::BeginPlay();
    auto* GM=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());if(!GM)return;
    const auto C=GM->GetConditions();
    bTest=FParse::Param(FCommandLine::Get(),TEXT("B2FYunshaTest"));
    for(TActorIterator<AStaticMeshActor> It(GetWorld());It;++It)
    {
        if(It->ActorHasTag(TEXT("YunshaMural")))
            if(auto* Mat=It->GetStaticMeshComponent()->CreateDynamicMaterialInstance(0))Pigments.Add(Mat);
        if(It->ActorHasTag(TEXT("YunshaMirror")))It->SetActorHiddenInGame(C.Season!=TEXT("monsoon"));
        if(It->ActorHasTag(TEXT("YunshaPalaceHall")))
        {
            // The desert uses fixed daylight exposure. Let the eye adapt within
            // the enclosed hall, blending back to daylight across its doorways.
            FVector Origin,Extent;It->GetActorBounds(false,Origin,Extent);
            const double H=Extent.Z*2,Base=Origin.Z-Extent.Z;
            auto* Bounds=NewObject<UBoxComponent>(this);AddInstanceComponent(Bounds);
            Bounds->SetupAttachment(RootComponent);Bounds->SetBoxExtent(FVector(H*.33,H*.33,H*.27));
            Bounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Bounds->SetCollisionResponseToAllChannels(ECR_Ignore);Bounds->SetGenerateOverlapEvents(false);
            Bounds->RegisterComponent();
            Bounds->SetWorldLocation(FVector(Origin.X,Origin.Y,Base+H*.29));
            auto* Interior=NewObject<UPostProcessComponent>(this);AddInstanceComponent(Interior);
            Interior->SetupAttachment(Bounds);Interior->bUnbound=false;Interior->Priority=110;
            Interior->BlendRadius=600;Interior->BlendWeight=1;
            Interior->Settings.bOverride_AutoExposureBias=true;Interior->Settings.AutoExposureBias=2.5;
            Interior->RegisterComponent();
            if(bTest)
            {
                float Distance=0;
                const bool Inside=Interior->EncompassesPoint(Bounds->GetComponentLocation(),0,&Distance);
                const bool OK=Inside && Interior->GetProperties().bIsEnabled;
                bPassed &= OK;
                UE_LOG(LogTemp,Display,TEXT("YunshaCheck palace-interior-exposure-volume %s centre=%s distance=%.1f"),
                    OK?TEXT("PASS"):TEXT("FAIL"),*Bounds->GetComponentLocation().ToString(),Distance);
            }
        }
    }
    LiftMotes->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere")));
    LiftMotes->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Yunsha/Materials/M_LiftMote")));
    for(int32 I=0;I<300;++I)LiftMotes->AddInstance(FTransform());
    Harp->SetSound(LoadObject<USoundWave>(nullptr,TEXT("/Game/Yunsha/Audio/WindHarp")));Harp->Play();
    Bell->SetSound(LoadObject<USoundWave>(nullptr,TEXT("/Game/Yunsha/Audio/TempleBell")));
    if(bTest)
    {
#if WITH_EDITOR
        // First-load shaders and imported mesh builds must finish before a
        // capture can be meaningful; otherwise early views show checkerboards.
        FAssetCompilingManager::Get().FinishAllCompilation();
#endif
        ValidateWorld();TestCamera=GetWorld()->SpawnActor<ACameraActor>();
        if(auto* PC=GetWorld()->GetFirstPlayerController())
        {PC->SetViewTarget(TestCamera);if(PC->GetHUD())PC->GetHUD()->bShowHUD=false;}
    }
}
void ABorn2FlapYunsha::ValidateWorld()
{
    auto Check=[&](bool OK,const TCHAR* What){bPassed&=OK;UE_LOG(LogTemp,Display,TEXT("YunshaCheck %s %s"),What,OK?TEXT("PASS"):TEXT("FAIL"));};
    auto* GM=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    Check(GM && GM->GetLevelId()==TEXT("Yunsha"),TEXT("level-routing"));
    Check(Pigments.Num()>50,TEXT("wind-harp-murals"));
    Check(Harp->Sound && Bell->Sound,TEXT("generated-audio"));
    if(GM && GM->GetConditions().Time!=TEXT("night"))
    {
        for(TActorIterator<ADirectionalLight> It(GetWorld());It;++It)
        {
            const FVector P(-14000,-52000,300);
            FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(YunshaDaylight),true);
            Check(!GetWorld()->LineTraceSingleByObjectType(Hit,P,P-It->GetActorForwardVector()*400000,
                FCollisionObjectQueryParams(ECC_WorldStatic),Q),TEXT("oasis-open-sunlight"));
            break;
        }
    }
    for(const FVector P:{FVector(-6000,-59000,0),FVector(0,-22000,0),FVector(4500,14000,0),FVector(45000,60000,0),FVector(180000,170000,0),FVector(-220000,-130000,0),FVector(80000,360000,0)})
    {
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(YunshaFloor),true);
        const bool Found=GetWorld()->LineTraceSingleByObjectType(Hit,P+FVector(0,0,90000),P-FVector(0,0,1000),FCollisionObjectQueryParams(ECC_WorldStatic),Q);
        Check(Found && FMath::Abs(Hit.ImpactPoint.Z-GroundHeight(P.X,P.Y))<120,TEXT("ground-collision"));
    }
    Check(GroundHeight(147000,-11000)<-1600 && GroundHeight(165000,-11000)>0,TEXT("excavation-depth"));
    Check(!SpringContains(147000,-11000),TEXT("excavation-dry-below-sea-level"));
    TArray<AActor*> Fossils;UGameplayStatics::GetAllActorsWithTag(this,TEXT("YunshaFossil"),Fossils);
    Check(Fossils.Num()==28,TEXT("excavation-fossils"));
    TArray<AActor*> EggBeds;UGameplayStatics::GetAllActorsWithTag(this,TEXT("YunshaPterosaurEggReconstruction"),EggBeds);
    Check(EggBeds.Num()==3,TEXT("pterosaur-egg-exposures"));
    TArray<AActor*> Tents;UGameplayStatics::GetAllActorsWithTag(this,TEXT("YunshaFieldTent"),Tents);
    Check(Tents.Num()==3,TEXT("excavation-field-camp"));
    TArray<AActor*> Fences;UGameplayStatics::GetAllActorsWithTag(this,TEXT("YunshaDigFence"),Fences);
    Check(Fences.Num()==4,TEXT("excavation-open-fence-sections"));

    for(const FVector Local:{FVector(48000,-10000,0),FVector(37000,-13000,0),FVector(30000,-14000,0),FVector(65000,-11000,0),FVector(55000,-22000,0),FVector(50000,-25000,0),FVector(59000,-18000,0)})
    {
        const FVector P=Local+FVector(100000,0,0);
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(YunshaDigFloor),true);
        const bool Found=GetWorld()->LineTraceSingleByObjectType(Hit,P+FVector(0,0,6000),P-FVector(0,0,3000),FCollisionObjectQueryParams(ECC_WorldStatic),Q);
        Check(Found && FMath::Abs(Hit.ImpactPoint.Z-GroundHeight(P.X,P.Y))<45,TEXT("excavation-floor-collision"));
    }
    // Probe an actual grotto: open mouth, deep rear wall, usable floor.
    {
        FCollisionQueryParams Q(SCENE_QUERY_STAT(YunshaGrotto),true);FHitResult Hit;
        const FVector Mouth(-122000,-54400,6500),Inside(-125000,-54400,6500);
        Check(!GetWorld()->LineTraceSingleByObjectType(Hit,Mouth,Inside,FCollisionObjectQueryParams(ECC_WorldStatic),Q),TEXT("grotto-open-mouth"));
        Check(GetWorld()->LineTraceSingleByObjectType(Hit,Inside,FVector(-128000,-54400,6500),FCollisionObjectQueryParams(ECC_WorldStatic),Q),TEXT("grotto-rear-wall"));
        Check(GetWorld()->LineTraceSingleByObjectType(Hit,Inside,Inside-FVector(0,0,1600),FCollisionObjectQueryParams(ECC_WorldStatic),Q),TEXT("grotto-floor"));
    }
    Check(SpringContains(-12000,-52000) && !SpringContains(0,-52000),TEXT("crescent-water-mask"));
    // Each marker pair spans the visible imported doorway and the full room.
    // Three overlapping 2m-diameter sweeps cover a 4.4m-wide flight corridor.
    int32 HallPassages=0,GatePassages=0,PavilionPassages=0;
    for(TActorIterator<AActor> It(GetWorld());It;++It)if(It->ActorHasTag(TEXT("YunshaPassageStart")))
    {
        AActor* End=nullptr;
        for(TActorIterator<AActor> Other(GetWorld());Other;++Other)
            if(Other->ActorHasTag(TEXT("YunshaPassageEnd")) && Other->Tags.Contains(It->Tags[1])){End=*Other;break;}
        Check(End!=nullptr,TEXT("palace-passage-pair"));if(!End)continue;
        const FVector P=It->GetActorLocation(),R=End->GetActorLocation();
        FCollisionQueryParams Q(SCENE_QUERY_STAT(YunshaPalace),true);FHitResult Hit;
        for(double Offset:{-120.,0.,120.})
        {
            const FVector Side(Offset,0,0);
            const bool Blocked=GetWorld()->SweepSingleByObjectType(Hit,P+Side,R+Side,FQuat::Identity,
                FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeSphere(100),Q);
            if(Blocked)UE_LOG(LogTemp,Warning,TEXT("Palace blockage %s offset %.0f actor %s at %s"),
                *It->Tags[1].ToString(),Offset,*GetNameSafe(Hit.GetActor()),*Hit.ImpactPoint.ToString());
            Check(!Blocked,TEXT("palace-flight-clearance"));
        }
        if(It->ActorHasTag(TEXT("YunshaHallPassage")))
        {
            ++HallPassages;const FVector Centre=(P+R)*.5;
            const bool Floor=GetWorld()->LineTraceSingleByObjectType(Hit,Centre,Centre-FVector(0,0,2000),FCollisionObjectQueryParams(ECC_WorldStatic),Q);
            Check(Floor && Hit.GetActor() && Hit.GetActor()->ActorHasTag(TEXT("YunshaHallFloor")),TEXT("palace-real-floor"));
            Check(GetWorld()->LineTraceSingleByObjectType(Hit,Centre,Centre+FVector(0,0,FVector::Distance(P,R)),FCollisionObjectQueryParams(ECC_WorldStatic),Q),TEXT("palace-real-ceiling"));
        }
        else if(It->ActorHasTag(TEXT("YunshaGatePassage")))++GatePassages;
        else if(It->ActorHasTag(TEXT("YunshaPavilionPassage")))++PavilionPassages;
    }
    Check(HallPassages==1 && GatePassages==1 && PavilionPassages==1,TEXT("open-jade-palace-pavilion-paifang"));
    auto C=Born2FlapWeather::Defaults(TEXT("Yunsha"));C.Time=TEXT("noon");
    const FVector Core=SoaringWind(FVector(-12000,-42000,4000),0,C,FVector(0,2,0));
    const FVector Outside=SoaringWind(FVector(-22000,-42000,4000),0,C,FVector(0,2,0));
    Check(Core.Z>5 && Core.Z>Outside.Z+3,TEXT("thermal-core"));
    C.WindThermal=0;
    Check(FMath::Abs(SoaringWind(FVector(-12000,-42000,4000),0,C,FVector::ZeroVector).Z)<.001,TEXT("thermal-off"));
    Check(SoaringWind(FVector(CanyonCentre(74000),74000,3000),0,C,FVector(0,2,0)).Y>4,TEXT("venturi"));
    int32 Perches=0;
    for(TActorIterator<AActor> It(GetWorld());It;++It)if(It->ActorHasTag(TEXT("YunshaPerch")))
    {
        ++Perches;const auto P=It->GetActorLocation();FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(YunshaPerch),true);
        Check(GetWorld()->LineTraceSingleByObjectType(Hit,P+FVector(0,0,500),P-FVector(0,0,800),FCollisionObjectQueryParams(ECC_WorldStatic),Q),TEXT("summit-perch"));
        Check(P.Z-GroundHeight(P.X,P.Y)>12000,TEXT("flight-only-height"));
    }
    Check(Perches>=5,TEXT("five-sky-temples"));
}
void ABorn2FlapYunsha::Tick(float Dt)
{
    Super::Tick(Dt);Elapsed+=Dt;BellCooldown-=Dt;
    auto* GM=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());if(!GM)return;
    const auto C=GM->GetConditions();const bool Warm=C.Time==TEXT("dawn")||C.Time==TEXT("sunset");
    for(auto Mat:Pigments){Mat->SetScalarParameterValue(TEXT("Reveal"),Warm?1.f:.06f);Mat->SetScalarParameterValue(TEXT("Afterglow"),C.Time==TEXT("sunset")?2.f:C.Time==TEXT("night")?.20f:0.f);}
    const float Activity=C.WindThermal==0 || C.Time==TEXT("night")?0.f:1.f;
    LiftMotes->SetVisibility(Activity>0);
    for(int32 I=0;I<300 && Activity>0;++I)
    {
        const auto Centre=ThermalCentres[I/60];const double Phase=I*2.39996+Elapsed*.08;
        const double Z=FMath::Fmod(I*219.+Elapsed*(C.Season==TEXT("cool")?90.:240.),14000.);
        const double R=500+Z*.075;
        const FVector P(Centre.X+R*FMath::Cos(Phase),Centre.Y+R*FMath::Sin(Phase),GroundHeight(Centre.X,Centre.Y)+Z);
        LiftMotes->UpdateInstanceTransform(I,FTransform(FRotator::ZeroRotator,P,FVector(.10,.10,.24)),false,I==299,true);
    }
    if(auto* Pawn=UGameplayStatics::GetPlayerPawn(this,0))
    {
        const FVector P=Pawn->GetActorLocation();const double CliffDistance=FMath::Abs(P.X+43000);
        const double Near=FMath::Exp(-CliffDistance/6500)*FMath::Exp(-FMath::Square((P.Y+10000)/80000));
        Harp->SetVolumeMultiplier(Near*.42);Harp->SetPitchMultiplier(.7+FMath::Clamp(Pawn->GetVelocity().Size()/3000.,0.,.8));
        if(P.Z>22000 && BellCooldown<=0){Bell->SetVolumeMultiplier(.25);Bell->Play();BellCooldown=75;}
    }
    if(bTest && bCapturePending)
    {
        // Camera jumps need actual rendered frames for streaming and temporal
        // history, especially centimetre-scale fossils viewed at close range.
        if(++CaptureFrames>=12 && Elapsed>=CaptureReadyAt)
        {
            FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("YUNSHA_%s_%s_%d.png"),*C.Time,*C.Season,CaptureStage),false,false);
            ++CaptureStage;bCapturePending=false;
        }
        return; // Never move to another view on the screenshot request frame.
    }
    if(bTest && Elapsed>4+CaptureStage*5)
    {
        if(CaptureStage==0)
        {
            auto Check=[&](bool OK,const TCHAR* What){bPassed&=OK;UE_LOG(LogTemp,Display,TEXT("YunshaCheck %s %s"),What,OK?TEXT("PASS"):TEXT("FAIL"));};
            int32 Mirrors=0;bool VisibilityOK=true;
            for(TActorIterator<AActor> It(GetWorld());It;++It)if(It->ActorHasTag(TEXT("YunshaMirror")))
            {++Mirrors;VisibilityOK &= It->IsHidden()==(C.Season!=TEXT("monsoon"));}
            Check(Mirrors==5 && VisibilityOK,TEXT("seasonal-mirrors"));
            Check(Pigments.Num()==60 && FMath::IsNearlyEqual(Pigments[0]->K2_GetScalarParameterValue(TEXT("Reveal")),Warm?1.f:.06f),TEXT("sun-glass-time-response"));
            if(auto* Bird=Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this,0)))
            {
                const int32 Previous=Bird->GetBirdModel();Bird->SelectBirdModel(5);
                Check(Bird->IsPterosaur() && Bird->PhysicsBirdModel()==2,TEXT("pterosaur-selection"));
                Bird->SelectBirdModel(Previous);
            }
            else Check(false,TEXT("pterosaur-selection"));
        }
        TArray<AActor*> Views;UGameplayStatics::GetAllActorsWithTag(this,*FString::Printf(TEXT("YunshaView%d"),CaptureStage),Views);
        if(Views.Num())
        {
            TestCamera->SetActorTransform(Views[0]->GetActorTransform());TestCamera->GetCameraComponent()->SetFieldOfView(65);
            auto& PP=TestCamera->GetCameraComponent()->PostProcessSettings;
            PP.bOverride_MotionBlurAmount=true;PP.MotionBlurAmount=0;
            CaptureReadyAt=Elapsed+2.5f;CaptureFrames=0;bCapturePending=true;
        }
        else {UE_LOG(LogTemp,Display,TEXT("YunshaTest %s"),bPassed?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,bPassed?0:1);bTest=false;}
    }
}

ABorn2FlapYunshaBatch::ABorn2FlapYunshaBatch()
{
    PrimaryActorTick.bCanEverTick=false;
    Instances=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Instances"));
    RootComponent=Instances;Instances->SetMobility(EComponentMobility::Static);
}
