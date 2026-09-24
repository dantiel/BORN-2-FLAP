#include "Flight/Born2FlapFlightPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "UObject/ConstructorHelpers.h"
namespace
{
constexpr double MathDt = 1.0 / 240.0;
bool Finite(const FVector &V)
{
    return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
}
} // namespace
ABorn2FlapFlightPawn::ABorn2FlapFlightPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    Body = CreateDefaultSubobject<UBoxComponent>(TEXT("FlightBody"));
    SetRootComponent(Body);
    Body->SetBoxExtent(FVector(42, 12, 12));
    Body->SetCollisionProfileName(TEXT("PhysicsActor"));
    Body->SetSimulatePhysics(true);
    Body->SetLinearDamping(.12f);
    Body->SetAngularDamping(.8f);
    Body->BodyInstance.bUseCCD = true;
    // Solver constraints replace per-step teleportation into ground contacts.
    Body->BodyInstance.bLockXRotation = true;
    Body->BodyInstance.bLockYRotation = true;
    BuildGeometry();
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(Body);
    CameraBoom->TargetArmLength = 480;
    CameraBoom->SetRelativeLocation(FVector(0, 0, 100));
    CameraBoom->SetRelativeRotation(FRotator(-12, 0, 0));
    CameraBoom->bInheritPitch = false;
    CameraBoom->bInheritRoll = false;
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 5;
    CameraBoom->CameraLagMaxDistance = 80;
    CameraBoom->bEnableCameraRotationLag = true;
    CameraBoom->CameraRotationLagSpeed = 5;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom);
    Camera->SetFieldOfView(85);
    AutoPossessPlayer = EAutoReceiveInput::Player0;
}
ABorn2FlapFlightPawn::~ABorn2FlapFlightPawn() = default;
void ABorn2FlapFlightPawn::BuildGeometry()
{
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Bird"));
    VisualRoot->SetupAttachment(Body);
    // Locations are centimetres; the root has unit scale.
    auto Part = [&](FName Name, USceneComponent *Parent, UStaticMesh *Mesh, FVector Loc, FVector Scale, FRotator Rot,
                    const TCHAR *Palette) {
        auto *C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        C->SetupAttachment(Parent);
        C->SetStaticMesh(Mesh);
        C->SetRelativeLocation(Loc);
        C->SetRelativeScale3D(Scale);
        C->SetRelativeRotation(Rot);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->ComponentTags.Add(FName(Palette));
        return C;
    };
    Part(TEXT("Fuselage"), VisualRoot, Sphere.Object, FVector::ZeroVector, FVector(.95, .27, .29),
         FRotator::ZeroRotator, TEXT("Ivory"));
    Part(TEXT("Head"), VisualRoot, Sphere.Object, FVector(40, 0, 12), FVector(.32, .25, .27), FRotator::ZeroRotator,
         TEXT("Teal"));
    Part(TEXT("Beak"), VisualRoot, Cone.Object, FVector(59, 0, 10), FVector(.12, .12, .3), FRotator(-90, 0, 0),
         TEXT("Gold"));
    for (int Side : {-1, 1})
    {
        Part(*FString::Printf(TEXT("Eye%d"), Side), VisualRoot, Sphere.Object, FVector(48, Side * 10, 17),
             FVector(.065, .04, .065), FRotator::ZeroRotator, TEXT("Ink"));
        auto *Shoulder = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("Shoulder%d"), Side));
        Shoulder->SetupAttachment(VisualRoot);
        Shoulder->SetRelativeLocation(FVector(3, Side * 12, 6));
        if (Side < 0)
            LeftShoulder = Shoulder;
        else
            RightShoulder = Shoulder;
        Part(*FString::Printf(TEXT("Wing%d"), Side), Shoulder, Sphere.Object, FVector(-4, Side * 38, 0),
             FVector(.45, .85, .045), FRotator(0, Side * 8, 0), TEXT("Teal"));
        for (int I = 0; I < 5; ++I)
            Part(*FString::Printf(TEXT("Feather%d_%d"), Side, I), Shoulder, Sphere.Object,
                 FVector(-16 - I * 3, Side * (49 + I * 9), -1), FVector(.43 - I * .035, .20, .035),
                 FRotator(0, Side * (15 + I * 7), 0), I % 2 ? TEXT("Ivory") : TEXT("Teal"));
        Part(*FString::Printf(TEXT("Tail%d"), Side), VisualRoot, Sphere.Object, FVector(-56, Side * 12, 3),
             FVector(.45, .20, .035), FRotator(0, Side * 24, 0), TEXT("Teal"));
    }
}
void ABorn2FlapFlightPawn::BeginPlay()
{
    Super::BeginPlay();
    Body->SetMassOverrideInKg(NAME_None, .45f, true);
    Body->BodyInstance.SetDOFLock(EDOFMode::SixDOF);
    // Fixed daylight exposure, compatible with either project luminance mode.
    // The legacy exposure range otherwise clips a physically lit sky to white.
    const auto* ExtendedRange = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
    const float DaylightExposure = ExtendedRange && ExtendedRange->GetInt() ? 14.f : 10000.f;
    Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
    Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
    Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
    Camera->PostProcessSettings.AutoExposureMinBrightness = DaylightExposure;
    Camera->PostProcessSettings.AutoExposureMaxBrightness = DaylightExposure;
    Camera->PostProcessSettings.AutoExposureBias = 0;
    auto *Contact = NewObject<UPhysicalMaterial>(this);
    Contact->Friction = .8f;
    Contact->Restitution = 0;
    Contact->bOverrideRestitutionCombineMode = true;
    Contact->RestitutionCombineMode = EFrictionCombineMode::Min;
    Body->SetPhysMaterialOverride(Contact);
    TArray<UStaticMeshComponent *> Parts;
    GetComponents(Parts);
    for (auto *Part : Parts)
        if (!Part->ComponentTags.IsEmpty())
        {
            const FString Path = TEXT("/Game/Training/M_") + Part->ComponentTags[0].ToString();
            if (auto *Material = LoadObject<UMaterialInterface>(nullptr, *Path))
                Part->SetMaterial(0, Material);
        }
    MathBridge = MakeUnique<FBorn2FlapMathBridge>();
    bSoakTest = FParse::Param(FCommandLine::Get(), TEXT("B2FSoakTest"));
    bFlightTest = bSoakTest || FParse::Param(FCommandLine::Get(), TEXT("B2FFlightTest"));
    ResetFlight();
    if (auto *PC = Cast<APlayerController>(GetController()))
    {
        PC->SetInputMode(FInputModeGameOnly());
        PC->bShowMouseCursor = false;
    }
}
void ABorn2FlapFlightPawn::ResetFlight(bool bSafety)
{
    if (bSafety)
        ++SafetyResets;
    // Recreate all firmware/servo/aeroelastic/battery/history state. RTS stays pinned.
    bHealthy = MathBridge && MathBridge->Load();
    bFlying = bLanding = bReturning = false;
    Throttle = Turn = Bank = LeftFlap = RightFlap = 0;
    TargetAltitude = 7;
    Accumulator = 0;
    AeroForce = AeroMoment = AssistForce = FVector::ZeroVector;
    Body->SetWorldLocationAndRotation(FVector(0, 0, 80), FRotator::ZeroRotator, false, nullptr,
                                      ETeleportType::TeleportPhysics);
    Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    Body->WakeAllRigidBodies();
    UE_LOG(LogTemp, Display, TEXT("FlightReset safety=%d backend=%d"), bSafety, bHealthy);
}
float ABorn2FlapFlightPawn::GetAltitude() const
{
    return FMath::Max(0.f, float(Body->GetComponentLocation().Z / 100.0 - .12));
}
float ABorn2FlapFlightPawn::GetSpeed() const
{
    return Body->GetPhysicsLinearVelocity().Size() / 100.0;
}
FString ABorn2FlapFlightPawn::GetFlightStatus() const
{
    if (!bHealthy)
        return TEXT("Flight core stopped - press R to reload");
    if (bLanding)
        return TEXT("LANDING");
    if (bReturning)
        return TEXT("RETURNING TO FIELD");
    if (!bFlying)
        return TEXT("READY - hold W to take off");
    return TEXT("CRUISING - release W to hold altitude");
}
bool ABorn2FlapFlightPawn::StepMath(float DeltaSeconds)
{
    if (!bHealthy)
        return false;
    const FTransform Transform = Body->GetComponentTransform();
    const FVector V = Transform.InverseTransformVectorNoScale(Body->GetPhysicsLinearVelocity() / 100.0);
    const FVector W = Transform.InverseTransformVectorNoScale(Body->GetPhysicsAngularVelocityInRadians());
    B2F_PilotInput Pilot{};
    Pilot.throttle = Throttle;
    Pilot.roll = Turn * .2;
    B2F_BodyState State{};
    State.delta_time_s = MathDt;
    for (int I = 0; I < 3; ++I)
    {
        State.linear_velocity_m_s[I] = V[I];
        State.angular_velocity_rad_s[I] = W[I];
    }
    Accumulator = FMath::Min(Accumulator + DeltaSeconds, .1);
    FVector ForceSum = FVector::ZeroVector, MomentSum = FVector::ZeroVector;
    int32 Steps = 0;
    while (Accumulator >= MathDt)
    {
        B2F_FirmwareOutput O{};
        bool Valid = MathBridge->Step(Pilot, State, O);
        const FVector F(O.force_n[0], O.force_n[1], O.force_n[2]);
        const FVector M(O.moment_n_m[0], O.moment_n_m[1], O.moment_n_m[2]);
        Valid = Valid && Finite(F) && Finite(M) && F.Size() < 100 && M.Size() < 50 &&
                FMath::IsFinite(O.left_flap_deg) && FMath::IsFinite(O.right_flap_deg) &&
                FMath::Abs(O.left_flap_deg) <= 85 && FMath::Abs(O.right_flap_deg) <= 85;
        if (!Valid)
        {
            ++MathFailures;
            bHealthy = bFlying = false;
            Accumulator = 0;
            AeroForce = AeroMoment = AssistForce = FVector::ZeroVector;
            UE_LOG(LogTemp, Error, TEXT("FlightMathRejected: disarmed, R recreates firmware context"));
            return false;
        }
        ForceSum += Transform.TransformVectorNoScale(F);
        MomentSum += Transform.TransformVectorNoScale(M);
        LeftFlap = O.left_flap_deg;
        RightFlap = O.right_flap_deg;
        ++Steps;
        Accumulator -= MathDt;
    }
    if (Steps)
    {
        AeroForce = ForceSum / Steps;
        AeroMoment = MomentSum / Steps;
    }
    return true;
}
void ABorn2FlapFlightPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const float Dt = FMath::Clamp(DeltaSeconds, 0.f, .1f);
    float Climb = 0, Steer = 0;
    bool Land = false, Reset = false;
    if (auto *PC = Cast<APlayerController>(GetController()))
    {
        Climb = PC->IsInputKeyDown(EKeys::W) ? 1.f : 0.f;
        Land = PC->IsInputKeyDown(EKeys::S);
        Steer = (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right) ? 1.f : 0.f) -
                (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left) ? 1.f : 0.f);
        Reset = PC->WasInputKeyJustPressed(EKeys::R);
        if (PC->WasInputKeyJustPressed(EKeys::F1))
            bVectors = !bVectors;
    }
    if (bFlightTest)
    {
        const double PreviousTestTime = TestTime;
        TestTime += DeltaSeconds;
        Climb = (TestTime >= 5 && TestTime < 8) || (TestTime >= 63 && TestTime < 65) ? 1.f : 0.f;
        Steer = TestTime >= 28 && TestTime < 32 ? 1.f : 0.f;
        Land = TestTime >= 40 && TestTime < 60;
        Reset = TestTime >= 61 && !bTestResetSent;
        if (Reset)
            bTestResetSent = true;
        if (bSoakTest)
        {
            Climb = TestTime >= 5 && TestTime < 8 ? 1.f : 0.f;
            Steer = TestTime >= 100 ? .35f : 0.f;
            Land = Reset = false;
        }
        if (PreviousTestTime < 18 && TestTime >= 18 && FParse::Param(FCommandLine::Get(), TEXT("B2FCapture")))
            FScreenshotRequest::RequestScreenshot(TEXT("TrainingFlight.png"), true, false);
    }
    if (Reset)
        ResetFlight();
    const FVector P = Body->GetComponentLocation(), V = Body->GetPhysicsLinearVelocity() / 100.0,
                  Omega = Body->GetPhysicsAngularVelocityInRadians();
    if (!Finite(P) || !Finite(V) || !Finite(Omega) || V.Size() > 35 || Omega.Size() > 6 || P.Z < -300 || P.Z > 7000 ||
        P.Size2D() > 45000)
    {
        UE_LOG(LogTemp, Warning, TEXT("FlightSafetyReset pos=%s vel=%s"), *P.ToString(), *V.ToString());
        ResetFlight(true);
        return;
    }
    if (bHealthy && Climb > 0 && !bFlying)
    {
        bFlying = true;
        TargetAltitude = 7;
    }
    bLanding = Land && bFlying;
    if (bFlying)
    {
        TargetAltitude = FMath::Clamp(TargetAltitude + Dt * (Climb * 1.8f - (Land ? 2.5f : 0.f)), .12f, 35.f);
        if (Land && P.Z < 23 && FMath::Abs(V.Z) < .8)
            bFlying = bLanding = false;
    }
    // Turn gently back before reaching the edge of the training area.
    // Ordinary cruising must never depend on a safety teleport to stay playable.
    if (bFlying && !bReturning && P.Size2D() > 30000)
    {
        bReturning = true;
        ++BoundaryReturns;
        UE_LOG(LogTemp, Display, TEXT("FlightBoundaryReturn count=%d"), BoundaryReturns);
    }
    if (P.Size2D() < 18000 || !bFlying)
        bReturning = false;
    if (bReturning)
    {
        const double HomeYaw = FMath::RadiansToDegrees(FMath::Atan2(-P.Y, -P.X));
        const double Error = FMath::FindDeltaAngleDegrees(double(Body->GetComponentRotation().Yaw), HomeYaw);
        Steer = FMath::Clamp(Error / 35.0, -1.0, 1.0);
    }
    Throttle = bFlying ? (Land ? .3f : (Climb > 0 ? .85f : .65f)) : 0;
    Turn = bFlying ? Steer : 0;
    if (StepMath(Dt))
    {
        AssistForce = FVector::ZeroVector;
        if (bFlying)
        {
            const double ClimbSpeed = FMath::Clamp((TargetAltitude - P.Z / 100.0) * 1.3, -2.5, 3.0);
            const double Cruise = Land ? FMath::Clamp((P.Z / 100.0 - .2) * 2.0, 0.0, 6.0) : 8.0;
            FVector DesiredV = Body->GetForwardVector() * Cruise;
            DesiredV.Z = ClimbSpeed;
            const FVector Accel = ((DesiredV - V) * 2.5).GetClampedToMaxSize(8.0);
            const FVector DesiredForce = Body->GetMass() * (Accel + FVector(0, 0, 9.81) + V * .12);
            AssistForce = DesiredForce - AeroForce;
        }
        // A single frame force is distributed by Chaos over its substeps.
        Body->AddForce((AeroForce + AssistForce) * 100.0);
        const double YawAccel = FMath::Clamp((Turn * .75 - Omega.Z) * 5.0, -2.0, 2.0);
        Body->AddTorqueInRadians(FVector(0, 0, YawAccel), NAME_None, true);
    }
    Bank = FMath::FInterpTo(Bank, Turn * 22.f, Dt, 3.f);
    VisualRoot->SetRelativeRotation(FRotator(bFlying ? FMath::Clamp(float(V.Z) * 2.f, -8.f, 8.f) : 0, 0, Bank));
    LeftShoulder->SetRelativeRotation(FRotator(0, 0, LeftFlap));
    RightShoulder->SetRelativeRotation(FRotator(0, 0, -RightFlap));
    if (bVectors)
    {
        DrawDebugDirectionalArrow(GetWorld(), P, P + AeroForce * 40, 20, FColor::Cyan, false, 0, 0, 2);
        DrawDebugDirectionalArrow(GetWorld(), P, P + AssistForce * 40, 20, FColor::Green, false, 0, 0, 2);
    }
    LogTime += DeltaSeconds;
    if (LogTime >= 1)
    {
        LogTime = 0;
        UE_LOG(LogTemp, Display,
               TEXT("FlightTelemetry mode=training flying=%d healthy=%d altitude=%.3fm speed=%.3fm/s target=%.2f "
                    "yaw=%.1f throttle=%.2f aeroN=%s assistN=%s flap=(%.1f,%.1f)"),
               bFlying, bHealthy, GetAltitude(), GetSpeed(), TargetAltitude, Body->GetComponentRotation().Yaw, Throttle,
               *AeroForce.ToString(), *AssistForce.ToString(), LeftFlap, RightFlap);
    }
    if (bFlightTest)
        CheckFlightTest();
}
void ABorn2FlapFlightPawn::CheckFlightTest()
{
    TestPeakSpeed = FMath::Max(TestPeakSpeed, double(GetSpeed()));
    TestPeakAltitude = FMath::Max(TestPeakAltitude, double(GetAltitude()));
    if (TestTime > 4 && TestTime < 5)
        bTestIdle = !bFlying && GetAltitude() < .1 && GetSpeed() < .15;
    if (TestTime > 15 && (TestTime < 27 || bSoakTest))
    {
        TestHoldError = FMath::Max(TestHoldError, FMath::Abs(Body->GetComponentLocation().Z / 100.0 - TargetAltitude));
        ++TestHoldSamples;
    }
    if (TestTime > 33 && TestTime < 34)
        bTestTurn = FMath::Abs(Body->GetComponentRotation().Yaw) > 60;
    if (TestTime > 59 && TestTime < 60)
        bTestLand = !bFlying && GetAltitude() < .1 && GetSpeed() < .2;
    if (TestTime > 62 && TestTime < 63)
        bTestReset = !bFlying && GetActorLocation().Size2D() < 10 && GetAltitude() < .1;
    if (TestTime > 73 && TestTime < 74)
        bTestSecondFlight = bFlying && GetAltitude() > 5 && GetSpeed() > 6;
    if (bSoakTest && TestTime >= 300 && !bTestFinished)
    {
        bTestFinished = true;
        const bool Pass = bHealthy && bFlying && SafetyResets == 0 && MathFailures == 0 && BoundaryReturns > 0 && bTestIdle &&
                          TestHoldError < .5 && TestPeakSpeed < 12 && TestPeakAltitude < 15 && GetSpeed() > 6;
        UE_LOG(LogTemp, Display,
               TEXT("FlightSoakTest %s seconds=%.2f holdError=%.4fm peakSpeed=%.3f peakAltitude=%.3f safety=%d "
                    "mathFailures=%d"),
               Pass ? TEXT("PASS") : TEXT("FAIL"), TestTime, TestHoldError, TestPeakSpeed, TestPeakAltitude,
               SafetyResets, MathFailures);
        FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
    }
    if (!bSoakTest && TestTime >= 75 && !bTestFinished)
    {
        bTestFinished = true;
        const bool Pass = bHealthy && SafetyResets == 0 && MathFailures == 0 && bTestIdle && bTestTurn && bTestLand &&
                          bTestReset && bTestSecondFlight && TestHoldSamples > 100 && TestHoldError < .5 &&
                          TestPeakSpeed < 12 && TestPeakAltitude < 15;
        UE_LOG(LogTemp, Display,
               TEXT("FlightTest %s seconds=%.2f idle=%d turn=%d land=%d reset=%d relaunch=%d holdError=%.4fm "
                    "peakSpeed=%.3f peakAltitude=%.3f safety=%d mathFailures=%d"),
               Pass ? TEXT("PASS") : TEXT("FAIL"), TestTime, bTestIdle, bTestTurn, bTestLand, bTestReset,
               bTestSecondFlight, TestHoldError, TestPeakSpeed, TestPeakAltitude, SafetyResets, MathFailures);
        FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
    }
}
