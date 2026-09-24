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
    Body->SetLinearDamping(0); // Native body/wing drag already removes energy.
    Body->SetAngularDamping(.8f);
    Body->BodyInstance.bUseCCD = true;
    // Approximate wing-distributed rotational mass, instead of cube inertia.
    Body->BodyInstance.InertiaTensorScale = FVector(8, 1.5, 2);
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
    // Fixed daylight exposure, compatible with either project luminance mode.
    // The legacy exposure range otherwise clips a physically lit sky to white.
    const auto *ExtendedRange =
        IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
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
    Body->SetNotifyRigidBodyCollision(true);
    Body->OnComponentHit.AddDynamic(this, &ABorn2FlapFlightPawn::OnBodyHit);
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
    bFlying = bReturning = false;
    Throttle = Turn = PitchInput = TargetBank = TargetPitch = LeftFlap = RightFlap = 0;
    BatterySoc = 1;
    Accumulator = 0;
    AeroForce = AeroMoment = AssistTorque = FVector::ZeroVector;
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
float ABorn2FlapFlightPawn::GetClimbRate() const
{
    return Body->GetPhysicsLinearVelocity().Z / 100.0;
}
void ABorn2FlapFlightPawn::OnBodyHit(UPrimitiveComponent *HitComponent, AActor *OtherActor,
                                     UPrimitiveComponent *OtherComponent, FVector NormalImpulse, const FHitResult &Hit)
{
    if (NormalImpulse.Size() > 100 && GetAltitude() > .5)
        UE_LOG(LogTemp, Display, TEXT("FlightImpact other=%s component=%s impulse=%s point=%s"),
               *GetNameSafe(OtherActor), *GetNameSafe(OtherComponent), *NormalImpulse.ToString(),
               *Hit.ImpactPoint.ToString());
}
void ABorn2FlapFlightPawn::LaunchFlight()
{
    if (!bHealthy || GetAltitude() > .4f || GetSpeed() > 1.f)
        return;
    // One explicit hand launch supplies initial momentum; it cannot repeat in
    // the air. Every subsequent acceleration comes from aero forces/gravity.
    FRotator Heading(0, Body->GetComponentRotation().Yaw, 0);
    Body->SetWorldLocationAndRotation(Body->GetComponentLocation() + FVector(0, 0, 140), Heading, false, nullptr,
                                      ETeleportType::TeleportPhysics);
    Body->SetPhysicsLinearVelocity(Heading.Vector() * 850 + FVector(0, 0, 220));
    Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    bFlying = true;
    UE_LOG(LogTemp, Display, TEXT("FlightHandLaunch speed=8.5m/s vertical=2.2m/s"));
}
FString ABorn2FlapFlightPawn::GetFlightStatus() const
{
    if (!bHealthy)
        return TEXT("Flight core stopped - press R to reload");
    if (bReturning)
        return TEXT("RETURNING TO FIELD");
    if (!bFlying)
        return TEXT("SPACE to hand-launch, then hold W to flap");
    if (GetSpeed() < 4.5f)
        return TEXT("LOW AIRSPEED - lower the nose");
    if (Throttle < .08f)
        return TEXT("GLIDING - airspeed and height are being spent");
    return Throttle > .85f ? TEXT("POWER STROKES") : TEXT("FLAPPING");
}
bool ABorn2FlapFlightPawn::StepMath(float DeltaSeconds)
{
    if (!bHealthy)
        return false;
    FQuat Rotation = Body->GetComponentQuat();
    FVector Velocity = Body->GetPhysicsLinearVelocity() / 100.0;
    FVector Omega = Body->GetPhysicsAngularVelocityInRadians();
    const FVector Inertia = Body->GetInertiaTensor() / 10000.0;
    B2F_PilotInput Pilot{};
    Pilot.throttle = Throttle;
    Pilot.roll = Turn * .1;
    Pilot.pitch = PitchInput * .25;
    Pilot.yaw = Turn * .1;
    B2F_BodyState State{};
    State.delta_time_s = MathDt;
    Accumulator = FMath::Min(Accumulator + DeltaSeconds, .1);
    FVector ForceSum = FVector::ZeroVector, MomentSum = FVector::ZeroVector, AssistSum = FVector::ZeroVector;
    int32 Steps = 0;
    while (Accumulator >= MathDt)
    {
        const FVector V = Rotation.UnrotateVector(Velocity);
        const FVector W = Rotation.UnrotateVector(Omega);
        for (int I = 0; I < 3; ++I)
        {
            State.linear_velocity_m_s[I] = V[I];
            State.angular_velocity_rad_s[I] = W[I];
        }
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
            AeroForce = AeroMoment = AssistTorque = FVector::ZeroVector;
            UE_LOG(LogTemp, Error, TEXT("FlightMathRejected: disarmed, R recreates firmware context"));
            return false;
        }
        const FVector WorldForce = Rotation.RotateVector(F);
        const FVector WorldMoment = Rotation.RotateVector(M);
        const FVector Attitude = AttitudeTorque(Rotation, Omega, Velocity);
        ForceSum += WorldForce;
        MomentSum += WorldMoment;
        AssistSum += Attitude;
        LeftFlap = O.left_flap_deg;
        RightFlap = O.right_flap_deg;
        BatterySoc = O.battery_soc;
        // Predict BOTH linear and rotational feedback between wing evaluations.
        // Holding angular velocity for a whole 30 Hz frame made aerodynamic
        // roll damping overshoot. Chaos receives the mean loads once and stays
        // authoritative for the actual rigid body and contacts.
        Velocity += (WorldForce / Body->GetMass() + FVector(0, 0, -9.81)) * MathDt;
        Omega += Rotation.RotateVector((M + Rotation.UnrotateVector(Attitude)) / Inertia) * MathDt;
        Omega *= FMath::Exp(-.8 * MathDt);
        const double Rate = Omega.Size();
        if (Rate > UE_SMALL_NUMBER)
            Rotation = (FQuat(Omega / Rate, Rate * MathDt) * Rotation).GetNormalized();
        ++Steps;
        Accumulator -= MathDt;
    }
    if (Steps)
    {
        AeroForce = ForceSum / Steps;
        AeroMoment = MomentSum / Steps;
        AssistTorque = AssistSum / Steps;
    }
    return true;
}
FVector ABorn2FlapFlightPawn::AttitudeTorque(const FQuat &Rotation, const FVector &Omega, const FVector &Velocity) const
{
    if (!bAttitudeAssist || !bFlying)
        return FVector::ZeroVector;
    const FQuat Desired = FRotator(TargetPitch, Rotation.Rotator().Yaw, TargetBank).Quaternion();
    FQuat Error = Desired * Rotation.Inverse();
    if (Error.W < 0)
        Error = FQuat(-Error.X, -Error.Y, -Error.Z, -Error.W);
    FVector Axis;
    double Angle;
    Error.ToAxisAndAngle(Axis, Angle);
    FVector Accel = Axis * Angle * 35.0 - Omega * 10.0;
    const double CoordinatedRate =
        9.81 * FMath::Tan(FMath::DegreesToRadians(TargetBank)) / FMath::Max(Velocity.Size2D(), 4.0);
    Accel.Z = (CoordinatedRate - Omega.Z) * 6.0;
    Accel = Accel.GetClampedToMaxSize(15.0);
    // A bounded torque actuator stabilizes attitude only, with the actual
    // inertia tensor. It supplies no translational force or altitude control.
    return Rotation.RotateVector(Rotation.UnrotateVector(Accel) * Body->GetInertiaTensor() / 10000.0)
        .GetClampedToMaxSize(1.2);
}
void ABorn2FlapFlightPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const float Dt = FMath::Clamp(DeltaSeconds, 0.f, .1f);
    float Effort = 0, Steer = 0, Pitch = 0;
    bool Launch = false, Reset = false;
    if (auto *PC = Cast<APlayerController>(GetController()))
    {
        Effort = PC->IsInputKeyDown(EKeys::W) ? (PC->IsInputKeyDown(EKeys::LeftShift) ? 1.f : .72f) : 0;
        Steer = (PC->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
        Pitch = (PC->IsInputKeyDown(EKeys::Up) ? 1.f : 0.f) -
                (PC->IsInputKeyDown(EKeys::Down) || PC->IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
        Launch = PC->WasInputKeyJustPressed(EKeys::SpaceBar);
        Reset = PC->WasInputKeyJustPressed(EKeys::R);
        if (PC->WasInputKeyJustPressed(EKeys::F1))
            bVectors = !bVectors;
        if (PC->WasInputKeyJustPressed(EKeys::F2))
            bAttitudeAssist = !bAttitudeAssist;
    }
    if (bFlightTest)
    {
        const double Previous = TestTime;
        TestTime += DeltaSeconds;
        Launch = (Previous < 3 && TestTime >= 3) || (Previous < 63 && TestTime >= 63);
        Effort = TestTime >= 3 && TestTime < 12                          ? .72f
                 : (TestTime >= 15 && TestTime < 40) || (TestTime >= 63) ? 1.f
                                                                         : 0.f;
        Steer = TestTime >= 32 && TestTime < 36 ? .6f : 0;
        Pitch = TestTime >= 26 && TestTime < 28 ? .75f : 0;
        Reset = TestTime >= 61 && !bTestResetSent;
        if (Reset)
            bTestResetSent = true;
        if (bSoakTest)
        {
            Effort = TestTime >= 3 ? .78f : 0;
            Steer = TestTime >= 30 ? .35f : 0;
            Pitch = 0;
            Reset = false;
            Launch = Previous < 3 && TestTime >= 3;
        }
        if (Previous < 18 && TestTime >= 18 && FParse::Param(FCommandLine::Get(), TEXT("B2FCapture")))
            FScreenshotRequest::RequestScreenshot(TEXT("AerodynamicFlight.png"), true, false);
    }
    if (Reset)
        ResetFlight();
    if (Launch)
        LaunchFlight();
    const FVector P = Body->GetComponentLocation(), V = Body->GetPhysicsLinearVelocity() / 100.0;
    const FVector Omega = Body->GetPhysicsAngularVelocityInRadians();
    if (!Finite(P) || !Finite(V) || !Finite(Omega) || V.Size() > 40 || Omega.Size() > 12 || P.Z < -300 ||
        P.Z > 300000 || P.Size2D() > 48000)
    {
        UE_LOG(LogTemp, Warning, TEXT("FlightSafetyReset pos=%s vel=%s omega=%s"), *P.ToString(), *V.ToString(),
               *Omega.ToString());
        ResetFlight(true);
        return;
    }
    if (GetAltitude() < .3f && GetSpeed() < 1.f)
        bFlying = false;
    if (bFlying && !bReturning && P.Size2D() > 30000)
    {
        bReturning = true;
        ++BoundaryReturns;
        UE_LOG(LogTemp, Display, TEXT("FlightBoundaryReturn count=%d"), BoundaryReturns);
    }
    if (P.Size2D() < 18000 || !bFlying)
        bReturning = false;
    if (bReturning && bAttitudeAssist)
    {
        const double HomeYaw = FMath::RadiansToDegrees(FMath::Atan2(-P.Y, -P.X));
        Steer = FMath::Clamp(FMath::FindDeltaAngleDegrees(double(Body->GetComponentRotation().Yaw), HomeYaw) / 50.0,
                             -1.0, 1.0);
    }
    Throttle = FMath::FInterpTo(Throttle, bFlying ? Effort : 0.f, Dt, 6.f);
    Turn = bFlying ? Steer : 0;
    PitchInput = bFlying ? Pitch : 0;
    TargetBank = FMath::FInterpTo(TargetBank, Turn * 28.f, Dt, 3.f);
    TargetPitch = FMath::FInterpTo(TargetPitch, PitchInput * 16.f, Dt, 2.f);
    AssistTorque = FVector::ZeroVector;
    if (StepMath(Dt))
    {
        // There is deliberately no target speed/altitude, lift offset, or
        // compensation of these forces. Gravity is applied by Chaos.
        Body->AddForce(AeroForce * 100.0);
        Body->AddTorqueInRadians(AeroMoment * 10000.0);
        Body->AddTorqueInRadians(AssistTorque * 10000.0);
    }
    VisualRoot->SetRelativeRotation(FRotator::ZeroRotator);
    LeftShoulder->SetRelativeRotation(FRotator(0, 0, LeftFlap));
    RightShoulder->SetRelativeRotation(FRotator(0, 0, -RightFlap));
    if (bVectors)
    {
        DrawDebugDirectionalArrow(GetWorld(), P, P + AeroForce * 25, 15, FColor::Cyan, false, 0, 0, 2);
        DrawDebugDirectionalArrow(GetWorld(), P, P + FVector(0, 0, -Body->GetMass() * 9.81) * 25, 15, FColor::Red,
                                  false, 0, 0, 2);
    }
    LogTime += DeltaSeconds;
    if (LogTime >= 1)
    {
        LogTime = 0;
        UE_LOG(LogTemp, Display,
               TEXT("FlightTelemetry mode=aerodynamic flying=%d healthy=%d altitude=%.3fm speed=%.3fm/s climb=%.3fm/s "
                    "pitch=%.2f roll=%.2f yaw=%.1f throttle=%.3f soc=%.3f aeroN=%s torqueNm=%s attitudeNm=%s "
                    "flap=(%.1f,%.1f)"),
               bFlying, bHealthy, GetAltitude(), GetSpeed(), GetClimbRate(), Body->GetComponentRotation().Pitch,
               Body->GetComponentRotation().Roll, Body->GetComponentRotation().Yaw, Throttle, BatterySoc,
               *AeroForce.ToString(), *AeroMoment.ToString(), *AssistTorque.ToString(), LeftFlap, RightFlap);
    }
    if (bFlightTest)
        CheckFlightTest();
}

void ABorn2FlapFlightPawn::CheckFlightTest()
{
    TestPeakSpeed = FMath::Max(TestPeakSpeed, double(GetSpeed()));
    TestPeakAltitude = FMath::Max(TestPeakAltitude, double(GetAltitude()));
    if (TestTime > 2 && TestTime < 3)
        bTestIdle = !bFlying && GetAltitude() < .2 && GetSpeed() < .2;
    if (TestTime > 11.8 && TestTime < 12)
        TestPoweredAltitude = GetAltitude();
    if (TestTime > 14.8 && TestTime < 15)
        TestGlideAltitude = GetAltitude();
    if (TestTime > 29.8 && TestTime < 30)
        TestBoostAltitude = GetAltitude();
    if (TestTime > 25.8 && TestTime < 26)
    {
        TestBeforePullSpeed = GetSpeed();
        TestBeforePullHeight = GetAltitude();
    }
    if (TestTime > 27.8 && TestTime < 28)
    {
        TestPullSpeed = GetSpeed();
        TestPullHeight = GetAltitude();
    }
    if (TestTime > 20 && TestTime < 30)
    {
        TestFlapMin = FMath::Min(TestFlapMin, double(LeftFlap));
        TestFlapMax = FMath::Max(TestFlapMax, double(LeftFlap));
    }
    if (TestTime > 37 && TestTime < 38)
        bTestTurn = FMath::Abs(Body->GetComponentRotation().Yaw) > 20;
    if (TestTime > 59 && TestTime < 60)
        bTestLand = !bFlying && GetAltitude() < .3 && GetSpeed() < .3;
    if (TestTime > 62 && TestTime < 63)
        bTestReset = !bFlying && GetActorLocation().Size2D() < 100 && GetAltitude() < .3;
    if (TestTime > 73 && TestTime < 74)
        bTestSecondFlight = bFlying && GetAltitude() > 1 && GetSpeed() > 5;
    if (bSoakTest && TestTime >= 180 && !bTestFinished)
    {
        bTestFinished = true;
        const bool Pass = bHealthy && bFlying && SafetyResets == 0 && MathFailures == 0 && GetAltitude() > 2 &&
                          TestPeakSpeed < 25 && TestPeakAltitude < 250 && TestFlapMax - TestFlapMin > 30;
        UE_LOG(LogTemp, Display,
               TEXT("FlightSoakTest %s seconds=%.2f altitude=%.2f speed=%.2f peakAltitude=%.2f peakSpeed=%.2f "
                    "safety=%d mathFailures=%d"),
               Pass ? TEXT("PASS") : TEXT("FAIL"), TestTime, GetAltitude(), GetSpeed(), TestPeakAltitude, TestPeakSpeed,
               SafetyResets, MathFailures);
        FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
    }
    if (!bSoakTest && TestTime >= 75 && !bTestFinished)
    {
        bTestFinished = true;
        const bool Pass = bHealthy && SafetyResets == 0 && MathFailures == 0 && bTestIdle && bTestTurn && bTestLand &&
                          bTestReset && bTestSecondFlight && TestPoweredAltitude > 2 &&
                          TestGlideAltitude < TestPoweredAltitude - .5 && TestBoostAltitude > TestGlideAltitude + 2 &&
                          TestFlapMax - TestFlapMin > 30 && TestPeakSpeed < 25 &&
                          TestPullSpeed < TestBeforePullSpeed - .3 && TestPullHeight > TestBeforePullHeight + .5;
        UE_LOG(LogTemp, Display,
               TEXT("FlightTest %s seconds=%.2f idle=%d turn=%d land=%d reset=%d relaunch=%d poweredAlt=%.3f "
                    "coastAlt=%.3f boostAlt=%.3f flapTravel=%.2f peakSpeed=%.3f peakAltitude=%.3f safety=%d "
                    "mathFailures=%d pullSpeed=(%.2f,%.2f) pullHeight=(%.2f,%.2f)"),
               Pass ? TEXT("PASS") : TEXT("FAIL"), TestTime, bTestIdle, bTestTurn, bTestLand, bTestReset,
               bTestSecondFlight, TestPoweredAltitude, TestGlideAltitude, TestBoostAltitude, TestFlapMax - TestFlapMin,
               TestPeakSpeed, TestPeakAltitude, SafetyResets, MathFailures, TestBeforePullSpeed, TestPullSpeed,
               TestBeforePullHeight, TestPullHeight);
        FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
    }
}
