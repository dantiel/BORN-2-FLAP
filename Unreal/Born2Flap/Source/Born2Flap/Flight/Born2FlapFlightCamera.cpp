#include "Flight/Born2FlapFlightPawn.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Game/Born2FlapGameMode.h"
#include "CollisionShape.h"

namespace
{
// Ground-observer eye height in cm (1.8 m). Shared by the landing anchor and the
// walker snap so the ground perspective always sits at a human eye line.
constexpr float GroundEyeHeightCm = 180.f;
}

void ABorn2FlapFlightPawn::RememberLanding(FVector Position)
{
    LastLanding=Position;
    bPilotWalked=false;
    const FVector Behind=FRotator(0,Body->GetComponentRotation().Yaw,0).Vector();
    GroundAnchor=Position-Behind*160;
    const auto* Mode=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    double Ground=Mode ? Mode->GroundHeight(GroundAnchor.X,GroundAnchor.Y) : 0;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(GroundCameraAnchor),false,this);
    if(GetWorld()->LineTraceSingleByChannel(Hit,GroundAnchor+FVector(0,0,150),GroundAnchor-FVector(0,0,5000),ECC_Visibility,Query))
        Ground=Hit.ImpactPoint.Z;
    GroundAnchor.Z=Ground+GroundEyeHeightCm; // Eye-height observer (1.8 m) beside the latest landing.
    bGroundGazeReady=false;
    if(bGroundView)
        if(auto* PC=Cast<APlayerController>(GetController()); PC && PC->PlayerCameraManager)
            PC->PlayerCameraManager->SetGameCameraCutThisFrame();
    UE_LOG(LogTemp,Display,TEXT("FlightCamera landing=%s groundAnchor=%s"),*Position.ToString(),*GroundAnchor.ToString());
}
void ABorn2FlapFlightPawn::MoveWalker(const FVector& Delta)
{
    if (Delta.IsNearlyZero())
        return;
    bPilotWalked=true;
    UWorld* World = GetWorld();
    if (!World)
        return;
    const auto* Mode = Cast<ABorn2FlapGameMode>(World->GetAuthGameMode());
    // Horizontal capsule sweep so the pilot cannot walk through rocks/terrain.
    const FVector From = GroundAnchor;
    FVector To = GroundAnchor + Delta;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(GroundWalker), false, this);
    Query.AddIgnoredActor(this);
    const FCollisionShape Capsule = FCollisionShape::MakeCapsule(30.f, 90.f);
    FHitResult Hit;
    if (World->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Visibility, Capsule, Query))
        To = From + Delta.GetSafeNormal() * FMath::Max(0.f, Hit.Distance - 1.f);
    // Snap to the terrain surface (mirrors RememberLanding's ground probe).
    double Ground = Mode ? Mode->GroundHeight(To.X, To.Y) : 0.0;
    FHitResult GHit;
    FCollisionQueryParams GQuery(SCENE_QUERY_STAT(GroundWalkerSnap), false, this);
    GQuery.AddIgnoredActor(this);
    if (World->LineTraceSingleByChannel(GHit, To + FVector(0, 0, 400), To - FVector(0, 0, 5000), ECC_Visibility, GQuery))
        Ground = GHit.ImpactPoint.Z;
    To.Z = Ground + GroundEyeHeightCm;   // eye height (1.8 m)
    GroundAnchor = To;
    // The pilot walks alone: the bird stays exactly where it landed so the
    // player can circle and inspect it (e.g. where it came down at the sea).
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
        // Only a genuine thrown flight re-anchors the observer to the landing
        // spot. A bird that merely settles on the ground (placed at spawn or
        // nudged by wind) must not teleport the pilot around.
        if(bThrown)
        {
            if(!bCameraGrounded || FVector::DistSquared(LastLanding,Body->GetComponentLocation())>10000)
                RememberLanding(Body->GetComponentLocation());
            bThrown=false;   // consumed this throw's landing
        }
        bCameraGrounded=true;
        bFlying=false;
    }
    else if(!Supported) bCameraGrounded=false;
}
void ABorn2FlapFlightPawn::SyncCameraState()
{
    // Two orthogonal axes: focus (bird vs on-foot pilot) and distance
    // (first vs third person) expand into the four exclusive views.
    bFpvAirView       = !bFocusActor && bFirstPerson;   // onboard bird (1st)
    bGroundView       = bFocusActor && bFirstPerson;    // pilot's eyes   (1st)
    bThirdPersonActor = bFocusActor && !bFirstPerson;   // follow pilot   (3rd)
}
void ABorn2FlapFlightPawn::ToggleGroundView()
{
    // F6: switch focus between the bird and the on-foot pilot (actor).
    bFocusActor = !bFocusActor;
    bActorZoomFirstPerson = false;
    SyncCameraState();
    bGroundGazeReady=false;
    if(auto* PC=Cast<APlayerController>(GetController())) { PC->SetViewTarget(this); if(PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame(); }
    if(!bFlightTest && !bDesktopInputTest) SaveFlightPreferences();
}
void ABorn2FlapFlightPawn::ToggleFpvView()
{
    // V: switch between first- and third-person within the current focus.
    bFirstPerson = !bFirstPerson;
    bActorZoomFirstPerson = false;
    SyncCameraState();
    if(auto* PC=Cast<APlayerController>(GetController()); PC && PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
    if(!bFlightTest && !bDesktopInputTest) SaveFlightPreferences();
}
void ABorn2FlapFlightPawn::CycleCamera()
{
    // BIRD-3RD → BIRD-1ST → PILOT-3RD → PILOT-1ST → BIRD-3RD. One affordance
    // for the mobile camera button; desktop uses the two toggles (V / F6).
    if (!bFocusActor && !bFirstPerson)      { bFirstPerson = true;  }
    else if (!bFocusActor && bFirstPerson)  { bFocusActor = true; bFirstPerson = false; }
    else if (bFocusActor && !bFirstPerson)  { bFirstPerson = true;  }
    else                                    { bFocusActor = false; bFirstPerson = false; }
    SyncCameraState();
    bGroundGazeReady = false;
    if (auto* PC = Cast<APlayerController>(GetController()))
    {
        PC->SetViewTarget(this);
        if (PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
    }
    if (!bFlightTest && !bDesktopInputTest) SaveFlightPreferences();
}
FString ABorn2FlapFlightPawn::GetCameraLabel() const
{
    return bGroundView        ? TEXT("PILOT / 1ST PERSON")
         : bFpvAirView        ? TEXT("BIRD / ONBOARD 1ST")
         : bThirdPersonActor  ? TEXT("PILOT / 3RD PERSON")
                              : TEXT("BIRD / FOLLOW 3RD");
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
        const FRotator Gaze=GroundGaze+FRotator(GroundLookPitch,GroundLookYaw,0);
        GroundCamera->SetWorldLocationAndRotation(GroundAnchor+Drift,Gaze+Tremor);
        const float Distance=FVector::Distance(BirdPosition,GroundAnchor);
        const float Fov=FMath::Clamp(75.f/(1.f+Distance/5000.f),28.f,75.f) * GroundZoom;
        GroundCamera->SetFieldOfView(FMath::FInterpTo(GroundCamera->FieldOfView,FMath::Clamp(Fov,6.f,120.f),Dt,2.f));
        GroundCamera->GetCameraView(Dt,Out);
    }
    else if(bFpvAirView)
    {
        // Mounted to the rigid body: all real banking and pitching reaches the lens.
        FpvCamera->SetRelativeLocation(FVector(76,0,14+(LeftFlap+RightFlap)*.006));
        // Fixed camera angle (FpvCameraAngleDeg) tilts the whole lens; the small
        // flap-coupled terms are only a subtle hand-held wobble on top.
        FpvCamera->SetRelativeRotation(FRotator(FpvCameraAngleDeg + (LeftFlap+RightFlap)*.004, 0, (LeftFlap-RightFlap)*.005));
        FpvCamera->GetCameraView(Dt,Out);
    }
    else if(bThirdPersonActor)
    {
        // Third-person follow of the on-foot pilot: an over-the-shoulder rig
        // behind the avatar. MMB-drag rotates the pilot (ActorOrbitYaw) and tilts
        // the lens up/down (ActorOrbitPitch); the wheel dollies the arm length
        // (ActorArmLength, zoom — close enough switches to first person).
        FVector ToBird = Body->GetComponentLocation() - GroundAnchor;
        ToBird.Z = 0.f;
        const float BaseYaw = ToBird.IsNearlyZero() ? float(GroundGaze.Yaw) : ToBird.Rotation().Yaw;
        const float FaceYaw = BaseYaw + ActorOrbitYaw;
        const FVector Fwd = FRotator(0.f, FaceYaw, 0.f).Vector();
        const FVector CamPos = GroundAnchor - Fwd * ActorArmLength + FVector(0.f, 0.f, 70.f);
        // Look along the pilot's gaze, tilted by the orbit pitch (up/down look).
        const FRotator Look = FRotator(FMath::Clamp(ActorOrbitPitch, -80.f, 80.f), FaceYaw, 0.f);
        ActorCamera->SetWorldLocationAndRotation(CamPos, Look);
        ActorCamera->GetCameraView(Dt, Out);
    }
    else
    {
        // Chase (follower) view: rotate the spring arm around the bird (yaw
        // inherits the body, pitch/roll are fixed). Orbit + arm length persist
        // across MMB release; automated tests keep their own configured arm and
        // are left untouched.
        if (!bFlightTest && !bDesktopInputTest)
        {
            // The chase arm always honours the persisted orbit offsets and arm
            // length, so orbiting/zooming with MMB stays put after release (the
            // defaults 0/-12/480 are exactly the neutral follow). A clean MMB
            // click resets those values back to default in the pawn's Tick.
            CameraBoom->SetRelativeRotation(FRotator(ChaseOrbitPitch,ChaseOrbitYaw,0));
            CameraBoom->TargetArmLength = ChaseArmLength;
        }
        Camera->GetCameraView(Dt,Out);
    }
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
    bThrown=true;   // simulate a thrown flight so the landing teleport fires
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