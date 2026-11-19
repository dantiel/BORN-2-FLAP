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
    // A fresh landing re-faces the pilot toward the bird (and the next walk
    // re-seeds PilotFacingYaw from that).
    bPilotFacingValid=false;
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
void ABorn2FlapFlightPawn::AutoFramePilot()
{
    // Cut to a third-person view of the pilot so the throw wind-up / catch arc
    // is actually visible, whatever view the player had chosen.
    if (bFocusActor && !bFirstPerson)
        return;   // already framing the pilot
    bAutoFramePrevFocus = bFocusActor;
    bAutoFramePrevFirst = bFirstPerson;
    bAutoFramed = true;
    bFocusActor = true;
    bFirstPerson = false;
    bActorZoomFirstPerson = false;
    SyncCameraState();
    if (auto* PC = Cast<APlayerController>(GetController()); PC && PC->PlayerCameraManager)
        PC->PlayerCameraManager->SetGameCameraCutThisFrame();
}
void ABorn2FlapFlightPawn::RestoreAutoFrame()
{
    if (!bAutoFramed)
        return;
    bAutoFramed = false;
    bFocusActor = bAutoFramePrevFocus;
    bFirstPerson = bAutoFramePrevFirst;
    SyncCameraState();
    if (auto* PC = Cast<APlayerController>(GetController()); PC && PC->PlayerCameraManager)
        PC->PlayerCameraManager->SetGameCameraCutThisFrame();
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
    bAutoFramed = false;   // explicit user choice overrides the auto-frame
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
    bAutoFramed = false;   // explicit user choice overrides the auto-frame
    bFirstPerson = !bFirstPerson;
    bActorZoomFirstPerson = false;
    SyncCameraState();
    if(auto* PC=Cast<APlayerController>(GetController()); PC && PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
    if(!bFlightTest && !bDesktopInputTest) SaveFlightPreferences();
}
void ABorn2FlapFlightPawn::CycleCamera()
{
    // BIRD-3RD → BIRD-1ST → PILOT-3RD → PILOT-1ST. One affordance
    // for the mobile camera button; desktop uses the two toggles (V / F6).
    bAutoFramed = false;   // explicit user choice overrides the auto-frame
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
    return bGroundView        ? FString(TEXT("PILOT / 1ST PERSON")) + (bAutoFollowBird ? TEXT("  | FOLLOW") : TEXT("  | FREE"))
         : bFpvAirView        ? TEXT("BIRD / ONBOARD 1ST")
         : bThirdPersonActor  ? FString(TEXT("PILOT / 3RD PERSON")) + (bAutoFollowBird ? TEXT("  | FOLLOW") : TEXT("  | FREE"))
                              : TEXT("BIRD / FOLLOW 3RD");
}

FString ABorn2FlapFlightPawn::GetCameraToastText() const
{
    if (FollowToastEnd <= 0.0 || !GetWorld())
        return FString();
    if (GetWorld()->GetTimeSeconds() >= FollowToastEnd)
        return FString();
    return bFollowToastAuto ? FString(TEXT("AUTO CAMERA")) : FString(TEXT("STATIC CAMERA"));
}

float ABorn2FlapFlightPawn::GetCameraToastAlpha() const
{
    if (FollowToastEnd <= 0.0 || !GetWorld())
        return 0.f;
    return FMath::Clamp(float(FollowToastEnd - GetWorld()->GetTimeSeconds()) / 0.75f, 0.f, 1.f);
}

FString ABorn2FlapFlightPawn::GetZoomToastText() const
{
    if (ZoomToastEnd <= 0.0 || !GetWorld())
        return FString();
    if (GetWorld()->GetTimeSeconds() >= ZoomToastEnd)
        return FString();
    return ZoomToastText;
}

float ABorn2FlapFlightPawn::GetZoomToastAlpha() const
{
    if (ZoomToastEnd <= 0.0 || !GetWorld())
        return 0.f;
    return FMath::Clamp(float(ZoomToastEnd - GetWorld()->GetTimeSeconds()) / 0.75f, 0.f, 1.f);
}

void ABorn2FlapFlightPawn::AnnounceZoom()
{
    // Corner toast: show the live lens factor for ~3 s (fade in the last
    // 0.75 s) whenever the RMB zoom engages or its factor changes.
    if (!GetWorld())
        return;
    ZoomToastText = FString::Printf(TEXT("ZOOM %.1fx"), RmbZoomFactor);
    ZoomToastEnd = GetWorld()->GetTimeSeconds() + 3.0;
}
void ABorn2FlapFlightPawn::CalcCamera(float Dt,FMinimalViewInfo& Out)
{
    auto* PC=Cast<APlayerController>(GetController());
    if(!PC || !PC->IsPaused()) CameraTime+=Dt;
    if(bGroundView)
    {
        const FVector BirdPosition=Body->GetComponentLocation();
        // A held/carried bird sits in (or right by) the pilot's hand, so the
        // anchor→bird vector is a tiny hand-local vector that swings with the
        // avatar's own facing. Auto-aiming GroundGaze at it chases the pilot's
        // hand → the view spins and the throw launches in a random direction.
        // Freeze GroundGaze at its last stable aim while held/carried; the mouse
        // still re-aims via GroundLookYaw/Pitch during the throw wind-up.
        if(!bBirdHeld && !bBirdCarried)
        {
            if(bAutoFollowBird)
            {
                const FRotator Aim=(BirdPosition-GroundAnchor).Rotation();
                GroundGaze=bGroundGazeReady ? FMath::RInterpTo(GroundGaze,Aim,Dt,18.f) : Aim;
                bGroundGazeReady=true;
            }
            else if(!bGroundGazeReady)
            {
                // Follow was disabled before the gaze ever seeded: seed it from
                // the current aim so the frozen base is sane.
                GroundGaze=(BirdPosition-GroundAnchor).Rotation();
                bGroundGazeReady=true;
            }
            // Follow off: the gaze is frozen and the GroundLook offsets are
            // body-relative — an effectively free first-person camera.
        }
        else if(!bGroundGazeReady)
        {
            // First-person entered while already holding/carrying: seed the gaze
            // from the stable aim base so the view isn't stale/zero.
            const float SeedYaw = bBirdHeld ? ThrowBaseYaw : CarryBaseYaw;
            GroundGaze = FRotator(0.f, SeedYaw, 0.f);
            bGroundGazeReady = true;
        }
        auto Noise=[this](float Rate,float Seed) { return FMath::PerlinNoise1D(float(CameraTime)*Rate+Seed); };
        const FVector Drift(Noise(.8f,31)*1.1,Noise(.6f,93)*1.1,Noise(1.3f,17)*.5);
        const FRotator Tremor(Noise(2.2f,51)*.22,Noise(1.8f,12)*.28,Noise(.7f,85)*.15);
        // Follow ON: tracking gaze + free-look offsets. Follow OFF: the view
        // rides the pilot's own facing + head offsets (A/D turns the pilot).
        FRotator Gaze = bAutoFollowBird
            ? GroundGaze + FRotator(GroundLookPitch,GroundLookYaw,0)
            : FRotator(GroundLookPitch, PilotFacingYaw + GroundLookYaw, 0);
        float Zoom = GroundZoom;
        if(bRmbZoom)
        {
            // RMB held: temporarily aim straight at the bird and zoom the lens
            // by the wheel-tunable RMB factor; the snapshot restores the chosen
            // view on release.
            Gaze = (BirdPosition-GroundAnchor).Rotation();
            Zoom = 1.f / FMath::Max(RmbZoomFactor, 1.f);
        }
        GroundCamera->SetWorldLocationAndRotation(GroundAnchor+Drift,Gaze+Tremor);
        const float Distance=FVector::Distance(BirdPosition,GroundAnchor);
        const float Fov=FMath::Clamp(75.f/(1.f+Distance/5000.f),28.f,75.f) * Zoom;
        // RMB zoom is a deliberate lens action: the wheel-tuned factor applies
        // instantly. The slow interp only eases the passive distance auto-FOV —
        // through it, a quick scroll during a short zoom hold reads as "the
        // wheel does nothing" in the pilot view.
        if (bRmbZoom)
            GroundCamera->SetFieldOfView(FMath::Clamp(Fov,1.2f,120.f));
        else
            GroundCamera->SetFieldOfView(FMath::FInterpTo(GroundCamera->FieldOfView,FMath::Clamp(Fov,6.f,120.f),Dt,2.f));
        GroundCamera->GetCameraView(Dt,Out);
    }
    else if(bFpvAirView)
    {
        // Mounted to the rigid body: all real banking and pitching reaches the lens.
        FpvCamera->SetRelativeLocation(FVector(76,0,14+(LeftFlap+RightFlap)*.006));
        // Fixed camera angle (FpvCameraAngleDeg) tilts the whole lens; the Z/X
        // head-turn yaw pans it sideways; the small flap-coupled terms are only
        // a subtle hand-held wobble on top.
        FpvCamera->SetRelativeRotation(FRotator(FpvCameraAngleDeg + (LeftFlap+RightFlap)*.004, FpvCameraYawDeg, (LeftFlap-RightFlap)*.005));
        FpvCamera->GetCameraView(Dt,Out);
    }
    else if(bThirdPersonActor)
    {
        if(bRmbZoom)
        {
            // RMB held: snap the rig behind the pilot toward the bird, dolly in
            // and let the lens carry the rest. The old pure dolly saturated once
            // the arm hit its floor — the wheel read dead above ~6x in the pilot
            // view. With the FOV term the full 2..20 wheel range always shows.
            const FVector BirdPos = Body->GetComponentLocation();
            const float AimYaw = float((BirdPos - GroundAnchor).Rotation().Yaw);
            const FVector CamPos = GroundAnchor - FRotator(0.f, AimYaw, 0.f).Vector() * FMath::Max(ActorArmLength / RmbZoomFactor, 60.f) + FVector(0.f, 0.f, 70.f);
            FRotator Look = (BirdPos - CamPos).Rotation();
            Look.Pitch = FMath::Clamp(Look.Pitch, -80.f, 80.f);
            Look.Roll = 0.f;
            ActorCamera->SetWorldLocationAndRotation(CamPos, Look);
            ActorCamera->SetFieldOfView(FMath::Clamp(90.f / RmbZoomFactor, 4.f, 90.f));
        }
        else
        {
            FVector CamPos; FRotator Look;
            ComputePilotOrbit(CamPos, Look);
            ActorCamera->SetWorldLocationAndRotation(CamPos, Look);
            ActorCamera->SetFieldOfView(90.f);
        }
        ActorCamera->GetCameraView(Dt, Out);
    }
    else
    {
        // Chase (follower) view: the orbit offsets rotate the spring arm around
        // the bird (the yaw
        // rides on the body heading via bInheritYaw) and persist across MMB
        // release. Applied unconditionally — the desktop input test asserts
        // exactly this coupling. Without it MMB-drag, Z/X and touch gestures
        // would update ChaseOrbit but never move the actual chase camera.
        CameraBoom->SetRelativeRotation(FRotator(ChaseOrbitPitch, ChaseOrbitYaw, 0));
        if (!bFlightTest && !bDesktopInputTest)
        {
            // The chase arm honours the persisted arm length, so zooming with
            // MMB stays put after release (the defaults 0/-12/480 are the
            // neutral follow; a clean MMB click resets them in the pawn Tick).
            // RMB held temporarily dollies in toward the bird (temporary zoom),
            // scaled by the wheel-tunable RMB factor.
            CameraBoom->TargetArmLength = bRmbZoom ? FMath::Max(ChaseArmLength / RmbZoomFactor, 24.f) : ChaseArmLength;
        }
        Camera->GetCameraView(Dt,Out);
    }
}

void ABorn2FlapFlightPawn::ComputePilotOrbit(FVector& OutPos, FRotator& OutRot) const
{
    // Over-the-shoulder rig behind the pilot. Follow ON: the camera yaw rides
    // the pilot→bird base plus the orbit offset, and the pitch aims at the
    // bird (up/down tracking) plus the pitch offset. Follow OFF: the orbit
    // offsets are the absolute camera yaw/pitch. While carrying the base is
    // the pilot's own facing — the bird sits in the hand, so the anchor→bird
    // direction is degenerate.
    FVector ToBird = Body->GetComponentLocation() - GroundAnchor;
    ToBird.Z = 0.f;
    const float BaseYaw = bBirdHeld
        ? ThrowBaseYaw
        : bBirdCarried
            ? (bPilotFacingValid ? PilotFacingYaw : CarryBaseYaw)
            : (ToBird.IsNearlyZero() ? float(GroundGaze.Yaw) : ToBird.Rotation().Yaw);
    const FVector CamBase = GroundAnchor + FVector(0.f, 0.f, 70.f);
    const float CamYaw = bAutoFollowBird ? BaseYaw + ActorOrbitYaw : ActorOrbitYaw;
    const FVector CamPos = CamBase - FRotator(0.f, CamYaw, 0.f).Vector() * ActorArmLength;
    float Pitch;
    if (bAutoFollowBird)
        Pitch = float((Body->GetComponentLocation() - CamPos).Rotation().Pitch) + ActorOrbitPitch;
    else
        Pitch = ActorOrbitPitch;
    OutPos = CamPos;
    OutRot = FRotator(FMath::Clamp(Pitch, -80.f, 80.f), CamYaw, 0.f);
}

bool ABorn2FlapFlightPawn::IsBirdOutOfView(const FRotator& CamRotation, const FVector& CamPosition) const
{
    const FVector ToBird = (Body->GetComponentLocation() - CamPosition).GetSafeNormal();
    if (ToBird.IsNearlyZero())
        return false;
    // 60° cone: once the bird leaves this comfortable view wedge while the
    // camera is being rotated, the auto-follow releases (the smart switch-off).
    return FVector::DotProduct(CamRotation.Vector(), ToBird) < 0.5f;
}

void ABorn2FlapFlightPawn::ApplyPilotOrbit(float DeltaYaw, float DeltaPitch)
{
    if (bGroundView)
    {
        GroundLookYaw += DeltaYaw;
        GroundLookPitch = FMath::Clamp(GroundLookPitch + DeltaPitch, -80.f, 89.f);
        if (bAutoFollowBird && !bBirdHeld && !bBirdCarried)
        {
            const float CamYaw = float(GroundGaze.Yaw + GroundLookYaw);
            const float CamPitch = float(GroundGaze.Pitch + GroundLookPitch);
            if (IsBirdOutOfView(FRotator(CamPitch, CamYaw, 0.f), GroundAnchor))
            {
                // Smart release: stop tracking, keep the camera where it is —
                // expressed body-relative so A/D keeps swinging the view.
                bAutoFollowBird = false;
                GroundLookYaw = CamYaw - PilotFacingYaw;
                GroundLookPitch = CamPitch;
                FollowToastEnd = GetWorld()->GetTimeSeconds() + 3.0;
                bFollowToastAuto = false;
            }
        }
    }
    else if (bThirdPersonActor)
    {
        if (bAutoFollowBird)
        {
            ActorOrbitYaw += DeltaYaw;
            ActorOrbitPitch = FMath::Clamp(ActorOrbitPitch + DeltaPitch, -160.f, 160.f);
            FVector Pos; FRotator Rot;
            ComputePilotOrbit(Pos, Rot);
            if (IsBirdOutOfView(Rot, Pos))
            {
                // Smart release: convert the offsets to the current absolute
                // pose so the camera does not jump.
                bAutoFollowBird = false;
                ActorOrbitYaw = Rot.Yaw;
                ActorOrbitPitch = Rot.Pitch;
                FollowToastEnd = GetWorld()->GetTimeSeconds() + 3.0;
                bFollowToastAuto = false;
            }
        }
        else
        {
            ActorOrbitYaw += DeltaYaw;
            ActorOrbitPitch = FMath::Clamp(ActorOrbitPitch + DeltaPitch, -80.f, 80.f);
        }
    }
}

void ABorn2FlapFlightPawn::ToggleAutoFollow()
{
    if (bAutoFollowBird)
    {
        // Freeze the camera exactly where it is and stop tracking.
        if (bThirdPersonActor)
        {
            FVector Pos; FRotator Rot;
            ComputePilotOrbit(Pos, Rot);
            ActorOrbitYaw = Rot.Yaw;
            ActorOrbitPitch = Rot.Pitch;
        }
        else if (bGroundView)
        {
            const float CamYaw = float(GroundGaze.Yaw + GroundLookYaw);
            const float CamPitch = float(GroundGaze.Pitch + GroundLookPitch);
            GroundLookYaw = CamYaw - PilotFacingYaw;
            GroundLookPitch = CamPitch;
        }
        bAutoFollowBird = false;
    }
    else
    {
        bAutoFollowBird = true;
        GroundLookYaw = 0.f; GroundLookPitch = 0.f;
        ActorOrbitYaw = 0.f; ActorOrbitPitch = 0.f;
        bGroundGazeReady = false;   // re-seed the gaze at the bird
    }
    // Corner toast: announce the new mode for a few seconds so the hotkey
    // always gives visible feedback, even while the bird sits in the hand.
    FollowToastEnd = GetWorld()->GetTimeSeconds() + 3.0;
    bFollowToastAuto = bAutoFollowBird;
    if (auto* PC = Cast<APlayerController>(GetController()); PC && PC->PlayerCameraManager)
        PC->PlayerCameraManager->SetGameCameraCutThisFrame();
}

void ABorn2FlapFlightPawn::ResetCameraView()
{
    ChaseOrbitYaw = 0.f; ChaseOrbitPitch = -12.f; ChaseArmLength = 480.f;
    GroundZoom = 1.f; GroundLookYaw = 0.f; GroundLookPitch = 0.f;
    ActorOrbitYaw = 0.f; ActorOrbitPitch = 0.f; ActorArmLength = 240.f;
    FpvCameraAngleDeg = 0.f; FpvCameraYawDeg = 0.f;
    bActorZoomFirstPerson = false;
    bRmbZoom = false;
    RmbZoomFactor = RmbZoomDefault;
    // J only re-centers the view — it must NOT re-enable auto-follow. The user
    // may want a steady static camera and use J purely to reset it. K and the
    // smart view-cone release are the only owners of the follow state.
    bGroundGazeReady = false;
    if (auto* PC = Cast<APlayerController>(GetController()); PC && PC->PlayerCameraManager)
        PC->PlayerCameraManager->SetGameCameraCutThisFrame();
}

void ABorn2FlapFlightPawn::SaveViewSnapshot()
{
    RmbViewSnapshot.ChaseOrbitYaw = ChaseOrbitYaw;
    RmbViewSnapshot.ChaseOrbitPitch = ChaseOrbitPitch;
    RmbViewSnapshot.ChaseArmLength = ChaseArmLength;
    RmbViewSnapshot.GroundZoom = GroundZoom;
    RmbViewSnapshot.GroundLookYaw = GroundLookYaw;
    RmbViewSnapshot.GroundLookPitch = GroundLookPitch;
    RmbViewSnapshot.ActorOrbitYaw = ActorOrbitYaw;
    RmbViewSnapshot.ActorOrbitPitch = ActorOrbitPitch;
    RmbViewSnapshot.ActorArmLength = ActorArmLength;
    RmbViewSnapshot.bAutoFollowBird = bAutoFollowBird;
    RmbViewSnapshot.bActorZoomFirstPerson = bActorZoomFirstPerson;
}

void ABorn2FlapFlightPawn::RestoreViewSnapshot()
{
    ChaseOrbitYaw = RmbViewSnapshot.ChaseOrbitYaw;
    ChaseOrbitPitch = RmbViewSnapshot.ChaseOrbitPitch;
    ChaseArmLength = RmbViewSnapshot.ChaseArmLength;
    GroundZoom = RmbViewSnapshot.GroundZoom;
    GroundLookYaw = RmbViewSnapshot.GroundLookYaw;
    GroundLookPitch = RmbViewSnapshot.GroundLookPitch;
    ActorOrbitYaw = RmbViewSnapshot.ActorOrbitYaw;
    ActorOrbitPitch = RmbViewSnapshot.ActorOrbitPitch;
    ActorArmLength = RmbViewSnapshot.ActorArmLength;
    bAutoFollowBird = RmbViewSnapshot.bAutoFollowBird;
    bActorZoomFirstPerson = RmbViewSnapshot.bActorZoomFirstPerson;
}

bool ABorn2FlapFlightPawn::CheckFlightCameras()
{
    // Exercise actual transforms, ground support, landing and hand-launch paths.
    // Own the follow state: a K-toggle earlier in the session (desktop input
    // stages) must not fail the gaze-tracking assertions below.
    bAutoFollowBird = true;
    bGroundGazeReady = false;
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