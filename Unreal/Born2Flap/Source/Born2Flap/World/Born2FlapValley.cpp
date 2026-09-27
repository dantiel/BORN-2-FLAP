#include "World/Born2FlapValley.h"
#include "ProceduralMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundWave.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

namespace
{
double Noise(double X,double Y) { return FMath::PerlinNoise2D(FVector2D(X,Y)); }
double Hill(double X,double Y,double Cx,double Cy,double Rx,double Ry)
{ return FMath::Exp(-FMath::Square((X-Cx)/Rx)-FMath::Square((Y-Cy)/Ry)); }
constexpr double InnerExtent=192000., InnerStep=600., OuterStep=9600.;
constexpr int32 CurrentWorldVersion=3;
}

ABorn2FlapValley::ABorn2FlapValley()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("RAVENSTONEFIELD"));
    RootComponent->SetMobility(EComponentMobility::Static);
    PrimaryActorTick.bCanEverTick=true;
}
double ABorn2FlapValley::RiverCentre(double X)
{ return 55+60*FMath::Sin(X*.0025)+15*FMath::Sin(X*.006); }
double ABorn2FlapValley::RiverWidth(double X)
{ return 7.5+3.5*FMath::Square(FMath::Sin(X*.003))+15*Hill(X,0,325,0,130,1); }
double ABorn2FlapValley::Height(double X,double Y)
{
    // Metres internally. Low Central European hills, with a broad flight meadow.
    double Z=3.0+2.2*Noise(X*.006,Y*.006)+.38*Noise(X*.04,Y*.04);
    const double Shoulder=FMath::Clamp((FMath::Abs(Y)-190)/650.,0.,1.);
    Z+=Shoulder*(24+28*Noise(X*.0018,Y*.0018)+10*Noise(X*.006,Y*.006));
    Z+=45*Hill(X,Y,-710,-590,340,245)+39*Hill(X,Y,-590,610,350,310);
    Z+=18*Hill(X,Y,290,205,75,65)+28*Hill(X,Y,1550,770,480,260);
    Z*=1-FMath::Exp(-FMath::Pow((X-80)/300.,4.)-FMath::Pow((Y+32)/47.,4.));
    // Preserve the original launch plane and reset point for flight physics.
    Z*=1-FMath::Exp(-FMath::Pow(X/95.,4.)-FMath::Pow(Y/23.,4.));
    const double Dist=(Y-RiverCentre(X))/RiverWidth(X);
    Z=FMath::Lerp(Z,-4.4+.3*Noise(X*.03,Y*.03),FMath::Exp(-FMath::Pow(FMath::Abs(Dist),4.)));
    const double LakeR=FMath::Sqrt(FMath::Square((X-965)/670.)+FMath::Square((Y-245)/405.));
    const double Edge=LakeR+.022*Noise(X*.022,Y*.022)+.013*FMath::Sin(X*.023);
    Z=FMath::Lerp(Z,-9.5+2.5*LakeR*LakeR,1-FMath::SmoothStep(.87,1.045,Edge));
    // Unequal, eroded delta islands are actual dry collision terrain.
    Z+=12.5*FMath::Pow(Hill(X,Y,520,190,68,43),.8);
    Z+=10.7*FMath::Pow(Hill(X,Y,670,95,38,24),.8);
    return Z*100;
}
double ABorn2FlapValley::GroundHeight(double X,double Y)
{
    const double Step=FMath::Max(FMath::Abs(X),FMath::Abs(Y))<=InnerExtent?InnerStep:OuterStep;
    const double X0=FMath::FloorToDouble(X/Step)*Step,Y0=FMath::FloorToDouble(Y/Step)*Step;
    const double U=(X-X0)/Step,V=(Y-Y0)/Step;
    auto H=[](double A,double B){return Height(A/100,B/100);};
    if(U+V<=1) return H(X0,Y0)*(1-U-V)+H(X0+Step,Y0)*U+H(X0,Y0+Step)*V;
    return H(X0+Step,Y0+Step)*(U+V-1)+H(X0+Step,Y0)*(1-V)+H(X0,Y0+Step)*(1-U);
}
bool ABorn2FlapValley::IsWater(double X,double Y) { return GroundHeight(X,Y)<WaterHeight; }
FString ABorn2FlapValley::PlaceName(double X,double Y)
{
    if(FVector2D(X-29000,Y-20500).Size()<6500) return TEXT("THE RAVEN WATCH");
    if(FVector2D(X-52000,Y-19000).Size()<8500) return TEXT("CROW'S ACRE");
    if(FVector2D(X-67000,Y-9500).Size()<6000) return TEXT("LAST SCRAP");
    if(X>33000 && IsWater(X,Y)) return TEXT("RAVEN LAKE");
    if(FMath::Abs(X-18500)<4500) return TEXT("THE UNDERPASS");
    if(X< -10000 && Y< -7000) return TEXT("THE OLD BARNS");
    if(FMath::Abs(Y)<6000 && X>-14000 && X<30000) return TEXT("LONG MEADOW / FLIGHT FIELD");
    return TEXT("RAVENSTONEFIELD");
}
void ABorn2FlapValley::BuildWorld()
{
    TArray<UActorComponent*> Previous; GetComponents(Previous);
    for(auto* C:Previous) if(C->ComponentHasTag(TEXT("RavenGenerated"))) C->DestroyComponent();
    Groups.Empty();
    BuildGround(InnerExtent,InnerStep,false); BuildGround(480000,OuterStep,true);
    BuildRiver(); BuildLandmarks(); PlantForest(); BuildAtmosphere();
    for(auto& Pair:Groups) Pair.Value->BuildTreeIfOutdated(false,true);
    WorldVersion=CurrentWorldVersion;
    UE_LOG(LogTemp,Display,TEXT("RAVENSTONEFIELD ready: river, lake, 2 islands, bridge, barns, village, watchtower, meadow, relics"));
}
void ABorn2FlapValley::BeginPlay()
{
    Super::BeginPlay();
    if(WorldVersion!=CurrentWorldVersion) BuildWorld();
    BuildRadio();
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FRavenTest"))) ValidateWorld();
}
void ABorn2FlapValley::BuildGround(double Extent,double Step,bool Outer)
{
    auto* Ground=NewObject<UProceduralMeshComponent>(this,NAME_None,RF_Transactional);
    Ground->ComponentTags.Add(TEXT("RavenGenerated")); Ground->ComponentTags.Add(TEXT("RavenTerrain"));
    Ground->SetupAttachment(RootComponent); Ground->SetMobility(EComponentMobility::Static);
    Ground->bUseComplexAsSimpleCollision=true; Ground->SetCollisionProfileName(TEXT("BlockAll"));
    AddInstanceComponent(Ground); Ground->RegisterComponent();
    const int32 N=FMath::RoundToInt(Extent*2/Step);
    TArray<FVector> V,Normals; TArray<FVector2D> UV; TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents; TArray<int32> T;
    V.Reserve((N+1)*(N+1)); Normals.Reserve((N+1)*(N+1)); T.Reserve(N*N*6);
    for(int32 Y=0;Y<=N;++Y) for(int32 X=0;X<=N;++X)
    {
        const double Px=(-Extent+X*Step)/100,Py=(-Extent+Y*Step)/100,Z=Height(Px,Py);
        const FVector Normal=FVector(Height(Px-1,Py)-Height(Px+1,Py),Height(Px,Py-1)-Height(Px,Py+1),200).GetSafeNormal();
        V.Add(FVector(Px*100,Py*100,Z)); Normals.Add(Normal); UV.Add(FVector2D(Px/6.,Py/6.));
        const double Bank=1-FMath::SmoothStep(-2.,2.8,Z/100);
        Colors.Add(FLinearColor(Bank,FMath::Clamp((1-Normal.Z)*3.6,0.,1.),.78+.18*Noise(Px*.03,Py*.03),1));
        Tangents.Add(FProcMeshTangent(FVector(1,0,-Normal.X/FMath::Max(Normal.Z,.01)),false));
    }
    for(int32 Y=0;Y<N;++Y) for(int32 X=0;X<N;++X)
    {
        const double Px=-Extent+(X+.5)*Step,Py=-Extent+(Y+.5)*Step;
        if(Outer && FMath::Abs(Px)<InnerExtent && FMath::Abs(Py)<InnerExtent) continue;
        const int32 A=Y*(N+1)+X,B=A+1,C=A+N+1,D=C+1; T.Append({A,C,B,B,C,D});
    }
    Ground->CreateMeshSection_LinearColor(0,V,T,Normals,UV,Colors,Tangents,true);
    Ground->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Ravenstonefield/Materials/M_Meadow")));
}
void ABorn2FlapValley::BuildRiver()
{
    auto* Water=NewObject<UProceduralMeshComponent>(this,NAME_None,RF_Transactional);
    Water->ComponentTags.Add(TEXT("RavenGenerated")); Water->ComponentTags.Add(TEXT("RavenWater"));
    Water->SetupAttachment(RootComponent); Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Water->SetCastShadow(false); AddInstanceComponent(Water); Water->RegisterComponent();
    TArray<FVector> V,N; TArray<FVector2D> UV; TArray<FLinearColor> Colors; TArray<int32> T;
    // A common surface prevents overlap where the delta joins the lake.
    constexpr int32 Count=640; constexpr double Step=600,Extent=192000;
    for(int32 Y=0;Y<Count;++Y) for(int32 X=0;X<Count;++X)
    {
        const double Px=-Extent+X*Step,Py=-Extent+Y*Step;
        const FVector2D P[]={FVector2D(Px,Py),FVector2D(Px+Step,Py),FVector2D(Px,Py+Step),FVector2D(Px+Step,Py+Step)};
        double H[4]; bool Wet=false;
        for(int32 I=0;I<4;++I){H[I]=Height(P[I].X/100,P[I].Y/100);Wet|=H[I]<WaterHeight+100;}
        if(!Wet) continue;
        const int32 A=V.Num();
        for(int32 I=0;I<4;++I)
        {
            V.Add(FVector(P[I].X,P[I].Y,WaterHeight));N.Add(FVector::UpVector);UV.Add(P[I]/800);
            Colors.Add(FLinearColor(FMath::Clamp(1-(WaterHeight-H[I])/650.,0.,1.),0,0,1));
        }
        T.Append({A,A+2,A+1,A+1,A+2,A+3});
    }
    Water->CreateMeshSection_LinearColor(0,V,T,N,UV,Colors,{},false);
    Water->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Ravenstonefield/Materials/M_LakeWater")));
}
UHierarchicalInstancedStaticMeshComponent* ABorn2FlapValley::Group(const FString& Mesh,const FString& Material,bool Collision,int32 Cull)
{
    const FString Key=Mesh+Material+(Collision?TEXT("solid"):TEXT("detail"));
    if(auto** Found=Groups.Find(Key)) return *Found;
    auto* C=NewObject<UHierarchicalInstancedStaticMeshComponent>(this,NAME_None,RF_Transactional);
    C->ComponentTags.Add(TEXT("RavenGenerated")); C->bAutoRebuildTreeOnInstanceChanges=false;
    C->SetupAttachment(RootComponent); C->SetMobility(EComponentMobility::Static);
    C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,*Mesh));
    if(!Material.IsEmpty()) C->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,*Material));
    C->SetCollisionProfileName(Collision?TEXT("BlockAll"):TEXT("NoCollision"));
    if(Cull) C->SetCullDistances(Cull*.82,Cull);
    AddInstanceComponent(C); C->RegisterComponent(); Groups.Add(Key,C); return C;
}
void ABorn2FlapValley::PlantForest()
{
    TArray<UHierarchicalInstancedStaticMeshComponent*> Trees,Rocks,Grass;
    for(int32 I=0;I<3;++I)
    {
        Trees.Add(Group(FString::Printf(TEXT("/Game/Nature/SM_Fir%d"),I),TEXT(""),false,200000));
        Grass.Add(Group(FString::Printf(TEXT("/Game/Nature/SM_Grass%d"),I),TEXT("/Game/Ravenstonefield/Materials/M_DryGrass"),false,16000));
    }
    auto* Autumn=Group(TEXT("/Game/Ravenstonefield/Meshes/SM_AutumnTree"),TEXT(""),false,200000);
    auto* Dead=Group(TEXT("/Game/Ravenstonefield/Meshes/SM_Deadwood"),TEXT(""),false,110000);
    for(int32 I=0;I<4;++I) Rocks.Add(Group(FString::Printf(TEXT("/Game/Nature/SM_Rock%d"),I),TEXT(""),false,110000));
    auto Place=[](UHierarchicalInstancedStaticMeshComponent* G,FVector P,FRotator R,double HeightCm)
    {
        if(!G->GetStaticMesh()) return;
        const FBoxSphereBounds B=G->GetStaticMesh()->GetBounds();const double S=HeightCm/FMath::Max(B.BoxExtent.Z*2,1.);
        const FVector Offset(-B.Origin.X,-B.Origin.Y,-B.Origin.Z+B.BoxExtent.Z);
        G->AddInstance(FTransform(R,P+R.RotateVector(Offset*S),FVector(S)));
    };
    auto Protected=[](double X,double Y)
    {
        if(X>-15000 && X<36500 && Y>-8500 && Y<RiverCentre(X/100)*100-1900) return true;
        if(FMath::Abs(X-18500)<1800 && Y>-28000 && Y<34000) return true;
        for(const FVector2D& Site:{FVector2D(-16000,-15500),FVector2D(-27000,-19000),FVector2D(-8500,-21000),
                                  FVector2D(27000,-15500),FVector2D(32000,-13500),FVector2D(29000,20500)})
            if(FVector2D(X-Site.X,Y-Site.Y).Size()<3200) return true;
        if(X>-49200 && X<-26000 && Y>-44300 && Y<-34800) return true;
        return false;
    };
    FRandomStream R(260926); int32 Count=0;
    for(int32 I=0;I<3400;++I)
    {
        const double X=I<600?R.FRandRange(-18000,48000):R.FRandRange(-155000,165000);
        const double Y=I<600?R.FRandRange(-33000,44000):R.FRandRange(-110000,110000);
        if(Protected(X,Y)||IsWater(X,Y)||GroundHeight(X,Y)<70) continue;
        if(FMath::Abs(Y/100-RiverCentre(X/100))<RiverWidth(X/100)+5) continue;
        if(Noise(X*.000047,Y*.000047)<-.14) continue;
        const double Z=GroundHeight(X,Y),H=R.FRandRange(800,1950);
        Place(I%5==0?Trees[I%3]:Autumn,FVector(X,Y,Z-12),FRotator(0,R.FRandRange(0,360),0),H);++Count;
        if(FVector2D(X,Y).Size()<115000)
        {
            auto* Trunk=NewObject<UCapsuleComponent>(this,NAME_None,RF_Transactional);
            Trunk->ComponentTags.Add(TEXT("RavenGenerated"));Trunk->SetupAttachment(RootComponent);
            Trunk->SetCapsuleSize(FMath::Max(14.,H*.012),H*.3);Trunk->SetRelativeLocation(FVector(X,Y,Z+H*.3));
            Trunk->SetCollisionProfileName(TEXT("BlockAll"));Trunk->SetMobility(EComponentMobility::Static);
            AddInstanceComponent(Trunk);Trunk->RegisterComponent();
        }
    }
    for(int32 I=0;I<900;++I)
    {
        const double X=R.FRandRange(-115000,150000);
        const double Y=I<430?(RiverCentre(X/100)+(I%2?1:-1)*(RiverWidth(X/100)+R.FRandRange(0,7)))*100:R.FRandRange(-70000,85000);
        if(Protected(X,Y)||IsWater(X,Y)) continue;
        Place(I%13==0?Dead:Rocks[I%4],FVector(X,Y,GroundHeight(X,Y)-12),FRotator(0,R.FRandRange(0,360),0),R.FRandRange(40,I%13==0?420:190));
    }
    for(int32 I=0;I<26000;++I)
    {
        const double X=I<18000?R.FRandRange(-9000,37000):R.FRandRange(-55000,115000);
        const double Y=I<18000?R.FRandRange(-8500,18000):R.FRandRange(-50000,85000);
        if(IsWater(X,Y)||FVector2D(X,Y).Size()<350 || (FMath::Abs(X-18500)<1200 && Y>-28000 && Y<34000)) continue;
        Place(Grass[I%3],FVector(X,Y,GroundHeight(X,Y)-2),FRotator(0,R.FRandRange(0,360),0),R.FRandRange(16,45));
    }
    UE_LOG(LogTemp,Display,TEXT("RavenVegetation trees=%d; autumn broadleaf, fir, dry grass, deadwood"),Count);
}
void ABorn2FlapValley::BuildRadio()
{
    auto* Track=LoadObject<USoundWave>(nullptr,TEXT("/Game/Ravenstonefield/Audio/TURBORAVEN"));
    if(!Track){UE_LOG(LogTemp,Error,TEXT("RavenRadio missing TURBORAVEN"));return;}
    Radio=NewObject<UAudioComponent>(this);Radio->SetupAttachment(RootComponent);
    Radio->bAutoActivate=false;Radio->bIsUISound=true;Radio->bAllowSpatialization=false;
    Radio->SetSound(Track);Radio->SetVolumeMultiplier(RadioVolume);Radio->RegisterComponent();Radio->Play();
    UE_LOG(LogTemp,Display,TEXT("RavenRadio playlist=RAVENSTONEFIELD track=TURBORAVEN duration=%.1f"),Track->Duration);
}
bool ABorn2FlapValley::HasRadioTrack() const {return Radio && Radio->Sound;}
void ABorn2FlapValley::SetPhotoView(int32 Index)
{
    auto* PC=UGameplayStatics::GetPlayerController(this,0);if(!PC)return;
    PhotoIndex=Index;if(Index<0){PC->SetViewTargetWithBlend(PC->GetPawn(),.8);return;}
    const FVector Positions[]={FVector(-8500,-17000,5900),FVector(32000,-11000,12000),FVector(26100,16800,GroundHeight(29000,20500)+1800),FVector(13400,4800,200),FVector(-18500,-19300,650)};
    const FVector Targets[]={FVector(65000,23000,600),FVector(97000,26000,-100),FVector(29300,20500,GroundHeight(29000,20500)+700),FVector(20500,9800,100),FVector(-15200,-15000,450)};
    const int32 I=Index%5;if(!PhotoCamera)PhotoCamera=GetWorld()->SpawnActor<ACameraActor>();
    PhotoCamera->SetActorLocationAndRotation(Positions[I],(Targets[I]-Positions[I]).Rotation());
    auto* C=PhotoCamera->GetCameraComponent();C->SetFieldOfView(I==0?70:65);C->bConstrainAspectRatio=false;
    PC->SetViewTargetWithBlend(PhotoCamera,.7);
}
void ABorn2FlapValley::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);auto* PC=UGameplayStatics::GetPlayerController(this,0);
    if(PC)
    {
        if(PC->WasInputKeyJustPressed(EKeys::M)&&Radio){bRadioOn=!bRadioOn;Radio->SetPaused(!bRadioOn);}
        if(PC->WasInputKeyJustPressed(EKeys::LeftBracket))RadioVolume=FMath::Max(0.f,RadioVolume-.08f);
        if(PC->WasInputKeyJustPressed(EKeys::RightBracket))RadioVolume=FMath::Min(1.f,RadioVolume+.08f);
        if(Radio)Radio->SetVolumeMultiplier(RadioVolume);
    }
    CaptureTime+=DeltaSeconds;
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FRavenCapture")))
    {
        if(CaptureStage==0){SetPhotoView(0);CaptureStage=1;CaptureTime=0;}
        if(CaptureTime>14&&CaptureStage%2==1)
        {FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("RAVENSTONEFIELD_%d.png"),PhotoIndex),false,false);++CaptureStage;CaptureTime=0;}
        else if(CaptureTime>3&&CaptureStage%2==0)
        {if(PhotoIndex>=4){FPlatformMisc::RequestExit(false);return;}SetPhotoView(PhotoIndex+1);++CaptureStage;CaptureTime=0;}
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FRavenTest"))&&CaptureTime>3)FPlatformMisc::RequestExit(false);
}
void ABorn2FlapValley::ValidateWorld()
{
    bool Pass=HasRadioTrack()&&!IsWater(0,0)&&IsWater(96500,24500)&&!IsWater(52000,19000)&&!IsWater(67000,9500);
    // Compare actual ground collision with the flight controller's sampler.
    FCollisionQueryParams Query;
    Query.AddIgnoredActor(UGameplayStatics::GetPlayerPawn(this,0));
    for(const FVector2D& P:{FVector2D(0,0),FVector2D(8000,-3000),FVector2D(53500,19000),FVector2D(95000,24500)})
    {
        FHitResult Hit;
        const bool Found=GetWorld()->LineTraceSingleByChannel(Hit,FVector(P.X,P.Y,20000),FVector(P.X,P.Y,-2000),ECC_Visibility,Query);
        Pass&=Found&&FMath::Abs(Hit.ImpactPoint.Z-GroundHeight(P.X,P.Y))<5;
    }
    UE_LOG(LogTemp,Display,TEXT("RavenWorldTest %s: dry launch, lake water, 2 dry islands, collision samples, TURBORAVEN"),Pass?TEXT("PASS"):TEXT("FAIL"));
}
