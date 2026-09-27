#include "Flight/Born2FlapFlightPawn.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "Game/Born2FlapGameMode.h"

void ABorn2FlapFlightPawn::RememberLanding(FVector Position)
{
    LastLanding=Position;
    const FVector Behind=FRotator(0,Body->GetComponentRotation().Yaw,0).Vector();
    GroundAnchor=Position-Behind*160;
    const auto* Mode=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    double Ground=Mode ? Mode->GroundHeight(GroundAnchor.X,GroundAnchor.Y) : 0;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(GroundCameraAnchor),false,this);
    if(GetWorld()->LineTraceSingleByChannel(Hit,GroundAnchor+FVector(0,0,150),GroundAnchor-FVector(0,0,5000),ECC_Visibility,Query))
        Ground=Hit.ImpactPoint.Z;
    GroundAnchor.Z=Ground+35; // Frog-height observer beside the latest landing.
    bGroundGazeReady=false;
    if(bGroundView)
        if(auto* PC=Cast<APlayerController>(GetController()); PC && PC->PlayerCameraManager)
            PC->PlayerCameraManager->SetGameCameraCutThisFrame();
    UE_LOG(LogTemp,Display,TEXT("FlightCamera landing=%s groundAnchor=%s"),*Position.ToString(),*GroundAnchor.ToString());
}
void ABorn2FlapFlightPawn::UpdateLandingCamera(float Dt)
{
    bool Supported=false;
    if(GetSpeed()<1.5f)
    {
        FHitResult Hit;
        const FVector P=Body->GetComponentLocation();
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BirdLandingSupport),false,this);
        Supported=GetWorld()->LineTraceSingleByChannel(Hit,P,P-FVector(0,0,22),ECC_Visibility,Query) && Hit.ImpactNormal.Z>.45;
    }
    GroundSettleTime=Supported ? GroundSettleTime+Dt : 0;
    if(GroundSettleTime>.3)
    {
        if(!bCameraGrounded || FVector::DistSquared(LastLanding,Body->GetComponentLocation())>10000)
            RememberLanding(Body->GetComponentLocation());
        bCameraGrounded=true;
        bFlying=false;
    }
    else if(!Supported) bCameraGrounded=false;
}
void ABorn2FlapFlightPawn::ToggleGroundView()
{
    bGroundView=!bGroundView;
    bGroundGazeReady=false;
    if(auto* PC=Cast<APlayerController>(GetController())) { PC->SetViewTarget(this); if(PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame(); }
}
void ABorn2FlapFlightPawn::ToggleFpvView()
{
    bFpvAirView=!bFpvAirView;
    if(auto* PC=Cast<APlayerController>(GetController()); PC && PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
    if(!bFlightTest && !bDesktopInputTest) SaveFlightPreferences();
}
FString ABorn2FlapFlightPawn::GetCameraLabel() const
{
    return bGroundView ? TEXT("GROUND / LAST LANDING") : bFpvAirView ? TEXT("FPV / ONBOARD") : TEXT("CHASE / FOLLOW");
}
void ABorn2FlapFlightPawn::CalcCamera(float Dt,FMinimalViewInfo& Out)
{
    auto* PC=Cast<APlayerController>(GetController());
    if(!PC || !PC->IsPaused()) CameraTime+=Dt;
    if(bGroundView)
    {
        const FVector BirdPosition=Body->GetComponentLocation();
        const FRotator Aim=(BirdPosition-GroundAnchor).Rotation();
        GroundGaze=bGroundGazeReady ? FMath::RInterpTo(GroundGaze,Aim,Dt,18.f) : Aim;
        bGroundGazeReady=true;
        auto Noise=[this](float Rate,float Seed) { return FMath::PerlinNoise1D(float(CameraTime)*Rate+Seed); };
        const FVector Drift(Noise(.8f,31)*1.1,Noise(.6f,93)*1.1,Noise(1.3f,17)*.5);
        const FRotator Tremor(Noise(2.2f,51)*.22,Noise(1.8f,12)*.28,Noise(.7f,85)*.15);
        GroundCamera->SetWorldLocationAndRotation(GroundAnchor+Drift,GroundGaze+Tremor);
        const float Distance=FVector::Distance(BirdPosition,GroundAnchor);
        const float Fov=FMath::Clamp(75.f/(1.f+Distance/5000.f),28.f,75.f);
        GroundCamera->SetFieldOfView(FMath::FInterpTo(GroundCamera->FieldOfView,Fov,Dt,2.f));
        GroundCamera->GetCameraView(Dt,Out);
    }
    else if(bFpvAirView)
    {
        // Mounted to the rigid body: all real banking and pitching reaches the lens.
        FpvCamera->SetRelativeLocation(FVector(76,0,14+(LeftFlap+RightFlap)*.006));
        FpvCamera->SetRelativeRotation(FRotator((LeftFlap+RightFlap)*.004,0,(LeftFlap-RightFlap)*.005));
        FpvCamera->GetCameraView(Dt,Out);
    }
    else Camera->GetCameraView(Dt,Out);
}

bool ABorn2FlapFlightPawn::CheckFlightCameras()
{
    // Exercise actual transforms, ground support, landing and hand-launch paths.
    const auto* Mode=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    RememberLanding(FVector(2500,0,Mode ? Mode->GroundHeight(2500,0)+12 : 12));
    const FVector Anchor=GroundAnchor;
    Body->SetWorldLocation(FVector(8000,1000,5000),false,nullptr,ETeleportType::TeleportPhysics);
    bGroundView=true;
    FMinimalViewInfo View;
    CalcCamera(1.f/60,View);
    bool Pass=FVector::Distance(View.Location,Anchor)<2 &&
        FVector::DotProduct(View.Rotation.Vector(),(Body->GetComponentLocation()-View.Location).GetSafeNormal())>.99 &&
        View.PostProcessSettings.MotionBlurAmount>.4;
    Body->SetWorldLocation(FVector(10000,-2000,6000),false,nullptr,ETeleportType::TeleportPhysics);
    for(int I=0;I<30;++I) CalcCamera(1.f/60,View);
    Pass &= FVector::Distance(View.Location,Anchor)<2 &&
        FVector::DotProduct(View.Rotation.Vector(),(Body->GetComponentLocation()-View.Location).GetSafeNormal())>.99;
    bGroundView=false; bFpvAirView=true;
    Body->SetWorldRotation(FRotator(17,45,33),false,nullptr,ETeleportType::TeleportPhysics);
    CalcCamera(1.f/60,View);
    Pass &= FVector::DotProduct(View.Rotation.Vector(),Body->GetForwardVector())>.999 &&
            FMath::Abs(FMath::FindDeltaAngleDegrees(View.Rotation.Roll,33.))<1;
    const FVector Land(3500,500,Mode ? Mode->GroundHeight(3500,500)+12 : 12);
    Body->SetWorldLocationAndRotation(Land,FRotator(0,45,0),false,nullptr,ETeleportType::TeleportPhysics);
    Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    bCameraGrounded=false;
    UpdateLandingCamera(.2f); UpdateLandingCamera(.2f);
    Pass &= bCameraGrounded && FVector::Distance(LastLanding,Land)<1;
    const FVector NewAnchor=GroundAnchor;
    LaunchFlight();
    Pass &= bFlying && FVector::Dist2D(Body->GetComponentLocation(),Land)<1 &&
            FVector::Distance(NewAnchor,GroundAnchor)<1 && Body->GetComponentLocation().Z>Land.Z+100;
    bGroundView=bFpvAirView=false;
    ResetFlight();
    UE_LOG(LogTemp,Display,TEXT("FlightCameraTest %s: fixed last-landing anchor, gaze tracking, motion blur, body-mounted FPV and launch at new landing"),Pass ? TEXT("PASS") : TEXT("FAIL"));
    return Pass;
}
