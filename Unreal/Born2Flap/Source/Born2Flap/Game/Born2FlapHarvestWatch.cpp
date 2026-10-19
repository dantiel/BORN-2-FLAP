#include "Game/Born2FlapHarvestWatch.h"
#include "Game/Born2FlapGameMode.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

using namespace HarvestWatch;

ABorn2FlapHarvestWatch::ABorn2FlapHarvestWatch()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("HarvestWatchRoot"));
}
UInstancedStaticMeshComponent* ABorn2FlapHarvestWatch::Group(const TCHAR* Mesh,const TCHAR* Material,bool Collision)
{
    auto* C=NewObject<UInstancedStaticMeshComponent>(this);C->SetupAttachment(RootComponent);
    C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Mesh));
    C->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,Material));
    C->SetCollisionProfileName(Collision?TEXT("BlockAll"):TEXT("NoCollision"));
    C->SetMobility(EComponentMobility::Movable);C->RegisterComponent();return C;
}
void ABorn2FlapHarvestWatch::BeginPlay()
{
    Super::BeginPlay();Round.Reset();BuildScarecrows();
    Bodies=Group(TEXT("/Engine/BasicShapes/Sphere"),TEXT("/Game/Shiomori/HarvestWatch/M_Raven"));
    Heads=Group(TEXT("/Engine/BasicShapes/Sphere"),TEXT("/Game/Shiomori/HarvestWatch/M_Raven"));
    Wings=Group(TEXT("/Engine/BasicShapes/Sphere"),TEXT("/Game/Shiomori/HarvestWatch/M_Feather"));
    Tails=Group(TEXT("/Engine/BasicShapes/Sphere"),TEXT("/Game/Shiomori/HarvestWatch/M_Feather"));
    Beaks=Group(TEXT("/Engine/BasicShapes/Cone"),TEXT("/Game/Shiomori/HarvestWatch/M_Beak"));
    Legs=Group(TEXT("/Engine/BasicShapes/Cylinder"),TEXT("/Game/Shiomori/HarvestWatch/M_Beak"));
    for(int I=0;I<18;++I)
    {
        for(auto* C:{Bodies.Get(),Heads.Get(),Beaks.Get(),Tails.Get()})C->AddInstance(FTransform::Identity);
        for(int J=0;J<12;++J)Wings->AddInstance(FTransform::Identity);
        for(int J=0;J<4;++J)Legs->AddInstance(FTransform::Identity);
    }
    PoseBirds();
    UE_LOG(LogTemp,Display,TEXT("HarvestWatch READY ravens=18 plots=3 scarecrows=3"));
}
void ABorn2FlapHarvestWatch::BuildScarecrows()
{
    auto* Wood=Group(TEXT("/Engine/BasicShapes/Cylinder"),TEXT("/Game/Shiomori/HarvestWatch/M_Wood"),true);
    auto* Shirt=Group(TEXT("/Engine/BasicShapes/Cube"),TEXT("/Game/Shiomori/HarvestWatch/M_Cloth"),true);
    auto* Straw=Group(TEXT("/Engine/BasicShapes/Sphere"),TEXT("/Game/Shiomori/HarvestWatch/M_Straw"),true);
    auto* Hat=Group(TEXT("/Engine/BasicShapes/Cone"),TEXT("/Game/Shiomori/HarvestWatch/M_Straw"),true);
    auto Rod=[&](FVector A,FVector B,double Radius)
    {Wood->AddInstance(FTransform(FQuat::FindBetweenNormals(FVector::UpVector,(B-A).GetSafeNormal()),(A+B)*.5,FVector(Radius/50,Radius/50,(B-A).Size()/100)),true);};
    for(int I=0;I<3;++I)
    {
        const FVector P=Plot(I)-FVector(0,0,50);
        Rod(P,P+FVector(0,0,245),5);
        Rod(P+FVector(0,-90,175),P+FVector(0,90,175),4);
        Shirt->AddInstance(FTransform(FRotator(0,7,4),P+FVector(0,0,147),FVector(.22,.62,.76)),true);
        for(int Side:{-1,1})
        {
            Shirt->AddInstance(FTransform(FRotator(0,0,Side*8),P+FVector(0,Side*57,174),FVector(.22,.65,.25)),true);
            Straw->AddInstance(FTransform(FRotator::ZeroRotator,P+FVector(0,Side*87,174),FVector(.2,.27,.2)),true);
            Shirt->AddInstance(FTransform(FRotator(0,0,Side*10),P+FVector(0,Side*18,90),FVector(.23,.23,.6)),true);
        }
        Straw->AddInstance(FTransform(FRotator::ZeroRotator,P+FVector(0,0,214),FVector(.38,.36,.45)),true);
        Hat->AddInstance(FTransform(FRotator::ZeroRotator,P+FVector(0,0,246),FVector(.74,.74,.22)),true);
        // Birds sit on this crossbar, leaving the hat and empty face readable.
        Rod(P+FVector(-40,0,245),P+FVector(40,0,245),3);
        auto* Sign=NewObject<UTextRenderComponent>(this);Sign->SetupAttachment(RootComponent);Sign->RegisterComponent();
        Sign->SetWorldLocationAndRotation(P+FVector(45,0,130),FRotator::ZeroRotator);
        Sign->SetText(FText::FromString(FString::Printf(TEXT("SATSUMAIMO\nPLOT %d"),I+1)));Sign->SetWorldSize(13);
        Sign->SetTextRenderColor(FColor(232,211,156));
    }
}
void ABorn2FlapHarvestWatch::PoseBirds()
{
    auto Put=[](UInstancedStaticMeshComponent* C,int I,const FVector& P,const FQuat& Q,const FVector& Size)
    {C->UpdateInstanceTransform(I,FTransform(Q,P,Size/100),true,false,true);};
    for(int I=0;I<Round.Birds.Num();++I)
    {
        const auto& B=Round.Birds[I];const bool Air=Airborne(B.State);
        const float Phase=AnimationTime*(Air?24:3.8)+B.Phase;
        const float Peck=B.State==EState::Feeding?FMath::Max(0.f,FMath::Sin(Phase))*23:0;
        const FQuat Q=FRotator(Air?FMath::Clamp(B.Velocity.Z*.012,-15.,15.):-14, B.Yaw,B.Bank).Quaternion();
        const float Bob=Air?FMath::Sin(Phase)*2:FMath::Sin(Phase*.7)*2.5;
        auto P=[&](FVector V){return B.Position+FVector(0,0,Bob)+Q.RotateVector(V);};
        Put(Bodies,I,P({0,0,7}),Q,{47,24,29});
        const FQuat Head=Q*FRotator(Peck*.3,B.HeadYaw,Air?0:FMath::Sin(Phase*.5)*12).Quaternion();
        const FVector HeadPosition=P({22,0,20-Peck});
        Put(Heads,I,HeadPosition,Head,{23,20,23});
        Put(Beaks,I,HeadPosition+Head.RotateVector(FVector(15,0,-2)),Head*FRotator(-90+Peck,0,0).Quaternion(),{9,9,22});
        Put(Tails,I,P({-29,0,6}),Q*FRotator(-12,0,0).Quaternion(),{32,24,4});
        for(int Side:{-1,1})
        {
            const int Offset=Side<0?0:6;
            const float Flap=Air?FMath::Sin(Phase)*42:78;
            const FQuat W=Q*FRotator(0,Air?Side*12:Side*65,Side*Flap).Quaternion();
            for(int F=0;F<6;++F)
            {
                if(!Air)
                {
                    Put(Wings,I*12+Offset+F,P({-7.-F*2,Side*(10.+F*.8),9.-F*.7}),Q*FRotator(-10,Side*8,0).Quaternion(),{40.-F*2,8,4});
                    continue;
                }
                const FVector Local(-F*4.,Side*(24.+F*4),0);
                const FVector WingRoot=P({2,Side*10.,10});
                Put(Wings,I*12+Offset+F,WingRoot+W.RotateVector(Local),W,{17.,49.-F*3,3.5});
            }
            const int L=I*4+(Side<0?0:2);
            Put(Legs,L,P({0,Side*7.,Air?-1.:-12.}),Q,{2.2,2.2,Air?8.:18.});
            Put(Legs,L+1,P({5,Side*7.,Air?-5.:-21.}),Q*FRotator(90,0,0).Quaternion(),{2.4,2.4,14});
        }
    }
    for(auto* C:{Bodies.Get(),Heads.Get(),Beaks.Get(),Tails.Get(),Wings.Get(),Legs.Get()})C->MarkRenderStateDirty();
}
FString ABorn2FlapHarvestWatch::Status() const
{
    if(Round.Finished)return FString::Printf(TEXT("HARVEST WATCH | %s | %.0f / 1000 points | ENTER: retry"),Round.Score>0?TEXT("HARVEST SAVED"):TEXT("CROP LOST"),Round.Score);
    if(!Round.Running)return TEXT("HARVEST WATCH | Launch the falcon to begin a 3-minute patrol");
    return FString::Printf(TEXT("HARVEST WATCH | %02d:%02d | %.0f / 1000 crop points | %d scattered"),FMath::CeilToInt(Round.Remaining)/60,FMath::CeilToInt(Round.Remaining)%60,Round.Score,Round.Scares);
}
FString ABorn2FlapHarvestWatch::PlotStatus() const
{
    int32 Decoys=0;for(const auto& B:Round.Birds)if(B.State==EState::Feinting)++Decoys;
    return FString::Printf(TEXT("Feeding: WEST %d | CENTRE %d | EAST %d   -   %s"),Round.Feeding(0),Round.Feeding(1),Round.Feeding(2),
        Decoys?TEXT("Decoy pass! Watch the other plots."):TEXT("Ravens watch your approach and raid unguarded plots."));
}
void ABorn2FlapHarvestWatch::Tick(float Dt)
{
    Super::Tick(Dt);
    auto* GM=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    auto* Bird=Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!GM || !Bird || GM->IsMenuOpen() || Bird->IsFlightSettingsOpen() || Bird->IsPoiOverlayOpen())return;
    if(!bValidated && FParse::Param(FCommandLine::Get(),TEXT("B2FHarvestTest")))
    {bValidated=true;Validate();return;}
    const bool Active=GM->IsHarvestWatch();
    const bool Threat=Bird->GetBirdModel()==2 && Bird->GetSpeed()>3 && Bird->GetAltitude()>.6;
    if(Active && !Round.Running && !Round.Finished && Threat)Round.Running=true;
    if(Active && Round.Finished)
        if(auto* PC=Cast<APlayerController>(Bird->GetController());PC && PC->WasInputKeyJustPressed(EKeys::Enter))
        {Round.Reset();Bird->SelectPoi(0);}
    // Ambient/ready/result states all animate; only an active patrol scores.
    AnimationTime+=Dt;Accumulator+=Dt;
    while(Accumulator>=.05f){Round.Step(.05f,Bird->GetActorLocation(),Threat,Bird->GetVelocity());Accumulator-=.05f;}
    PoseBirds();
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FHarvestCapture")))
    {
        CaptureTime+=Dt;
        if(!CaptureCamera)
        {CaptureCamera=GetWorld()->SpawnActor<ACameraActor>();UGameplayStatics::GetPlayerController(this,0)->SetViewTarget(CaptureCamera);}
        // First two captures share a camera so the flock's unprompted movement
        // can be checked directly before launch. Last two inspect individual poses.
        const FVector Eyes[]={FVector(-1500,-14800,950),FVector(-1500,-14800,950),FVector(5500,-13000,5000),FVector(0,-15500,1200)};
        const FVector Targets[]={FVector(0,-16400,450),FVector(0,-16400,450),FVector(0,-16400,350),FVector(700,-16400,650)};
        const int I=FMath::Min(CaptureIndex,3);
        CaptureCamera->SetActorLocationAndRotation(Eyes[I],(Targets[I]-Eyes[I]).Rotation());
        CaptureCamera->GetCameraComponent()->SetFieldOfView(70);
        if(CaptureIndex==3 && !bCaptureScare)
        {bCaptureScare=true;Round.Running=true;Round.Step(.05f,Round.Birds[6].Position+FVector(0,0,200),true);}
        if(CaptureIndex==2 || CaptureIndex==3)
        {
            const FVector Raven=Round.Birds[CaptureIndex==2?7:6].Position;
            const FVector Eye=Raven+FVector(-260,-340,120);
            CaptureCamera->SetActorLocationAndRotation(Eye,(Raven-Eye).Rotation());
        }
        if(CaptureTime>5+CaptureIndex*4 && CaptureIndex<4)
        {FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("HARVEST_WATCH_%d.png"),CaptureIndex),false,false);++CaptureIndex;}
        if(CaptureTime>23){UE_LOG(LogTemp,Display,TEXT("HarvestWatchCapture PASS"));FPlatformMisc::RequestExitWithStatus(false,0);}
    }
}
void ABorn2FlapHarvestWatch::Validate()
{
    bool Pass=Bodies->GetStaticMesh()!=nullptr && Bodies->GetMaterial(0)!=nullptr;
    const auto* GM=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    const auto* Player=Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this,0));
    Pass &= GM && GM->IsHarvestWatch() && Player && Player->GetBirdModel()==2;
    Pass &= Player && FVector::Dist2D(Player->GetActorLocation(),FVector(0,-19400,480))<500;
    auto Check=[&](bool OK,const TCHAR* Name)
    {Pass &= OK;UE_LOG(LogTemp,Display,TEXT("HarvestBehaviour %s: %s"),OK?TEXT("PASS"):TEXT("FAIL"),Name);};
    const FVector Far(100000,100000,100000);
    FRound Test;Test.Reset();const auto Initial=Test.Birds;
    for(int I=0;I<200;++I)Test.Step(.05f,Far,false);
    int Moving=0;for(int I=0;I<18;++I)Moving+=FVector::Dist(Initial[I].Position,Test.Birds[I].Position)>50;
    Check(Test.Score==1000 && Test.Remaining==180 && Moving>=15 && Test.Hops>15,TEXT("flock moves before launch without charging crop points"));
    Test.Reset();Test.Running=true;Test.Step(.05f,Far,false);
    Check(Test.Score<1000 && Test.Scares==0,TEXT("feeding damage"));
    Test.Reset();const FVector Near=Test.Birds[0].Position;
    Test.Step(.05f,Near,false);Check(Test.Scares==0,TEXT("parked falcon does not clear crops"));
    Test.Step(.05f,Near,true);Check(Test.Birds[0].State==EState::Fleeing && Test.Scares>0,TEXT("takeoff before score starts"));
    const int Scares=Test.Scares;Test.Step(.05f,Near,true);Check(Test.Scares==Scares,TEXT("no repeated scare points during escape"));
    Test.Birds.SetNum(1);Test.Birds[0].Position=Test.Birds[0].Target;Test.Birds[0].Timer=10;
    float Travel=0;for(int I=0;I<80;++I){const FVector P=Test.Birds[0].Position;Test.Step(.05f,Far,false);Travel+=FVector::Dist(P,Test.Birds[0].Position);}
    Check(Test.Birds[0].State==EState::Circling && Travel>1500,TEXT("escape waypoint becomes continuous circling, never hovering"));
    bool Landed=false;
    for(int I=0;I<2400;++I){Test.Step(.05f,Far,false);Landed|=Test.Birds[0].State==EState::Feeding;}
    Check(Test.Returns>=1 && Landed,TEXT("escape returns to feeding despite continued ambient motion"));
    Test.Reset();Test.Pressure[0]=1;Test.Pressure[1]=1;Test.Pressure[2]=0;
    Check(Test.SafestPlot(Test.Birds[0],Plot(0),FVector(900,0,0),true)==2,TEXT("choose unguarded far plot using patrol memory"));
    Test.Reset();Test.Birds.SetNum(1);auto& Decoy=Test.Birds[0];Decoy.Decoy=true;
    Decoy.State=EState::Circling;Decoy.Position=FVector(0,-16400,1800);Decoy.Target=Decoy.Position;Decoy.Timer=0;
    Test.Step(.05f,FVector(4000,-16400,700),true,FVector(0,800,0));
    Check(Test.Feints==1 && Decoy.State==EState::Feinting,TEXT("bold decoy makes a lateral teasing pass"));
    for(int I=0;I<60;++I)Test.Step(.05f,Far,false);
    Check(Decoy.State!=EState::Feinting,TEXT("teasing pass is bounded and returns to crops"));
    Test.Reset();Test.Birds.SetNum(2);Test.Birds[0].Position=FVector(0,-16400,565);
    Test.Birds[1].Position=FVector(1500,-16400,365);Test.Birds[1].State=EState::Feeding;
    Test.Step(.05f,FVector(-1500,-16400,565),true);
    Check(Test.Birds[1].State==EState::Fleeing && Test.Alarms>0,TEXT("nearby lookout warns a bird outside direct scare range"));
    Test.Reset();Test.Running=true;Test.Remaining=.025;Test.Score=100;Test.Step(.05f,Far,false);
    Check(Test.Finished && Test.Remaining==0 && Test.Score>0,TEXT("timer ends exactly with remaining crop"));
    const double Final=Test.Score;const auto EndPose=Test.Birds;
    for(int I=0;I<160;++I)Test.Step(.05f,Far,false);
    Moving=0;for(int I=0;I<18;++I)Moving+=FVector::Dist(EndPose[I].Position,Test.Birds[I].Position)>50;
    Check(Test.Score==Final && Moving>=15,TEXT("result freezes points, not the flock"));
    Test.Reset();Test.Running=true;Test.Score=.001;Test.Step(.05f,Far,false);
    Check(Test.Finished && Test.Score==0,TEXT("crop loss clamps to zero"));
    Test.Reset();Check(Test.Score==1000 && Test.Scares==0 && Test.Returns==0 && !Test.Finished && !Test.Running,TEXT("clean restart"));
    // Verify the component transform itself follows the simulated bird. This
    // catches renderer/instance-update regressions that logic-only tests miss.
    const auto SavedRound=Round;Round=Test;PoseBirds();FTransform Before,After;
    Bodies->GetInstanceTransform(1,Before,true);
    for(int I=0;I<120;++I)Round.Step(.05f,Far,false);
    PoseBirds();Bodies->GetInstanceTransform(1,After,true);
    Check(FVector::Dist(Before.GetLocation(),After.GetLocation())>50,TEXT("rendered bird instance moves"));
    Round=SavedRound;PoseBirds();
    FHitResult Ground;Pass &= GetWorld()->LineTraceSingleByChannel(Ground,FVector(200,-16000,1000),FVector(200,-16000,0),ECC_Visibility);
    UE_LOG(LogTemp,Display,TEXT("HarvestWatchTest %s: ambient movement, hops, lookout alarms, guarded plots, decoys, flight, scoring and rendered instances"),Pass?TEXT("PASS"):TEXT("FAIL"));
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
