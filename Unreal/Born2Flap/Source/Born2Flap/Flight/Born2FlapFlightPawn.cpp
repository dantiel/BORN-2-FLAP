#include "Flight/Born2FlapFlightPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Math/Born2FlapMathBridge.h"
#include "UObject/ConstructorHelpers.h"

ABorn2FlapFlightPawn::ABorn2FlapFlightPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    // Physics root: the fuselage collision body. Visual detail is layered on
    // as child components in BuildGeometry().
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    SetRootComponent(Body);
    Body->SetSimulatePhysics(true);
    Body->SetLinearDamping(0.05f);
    // Keep the first prototype controllable while the aerodynamic model is
    // still being tuned.  The math core supplies the flight moments; Chaos
    // damping prevents a transient from turning into an unbounded spin.
    Body->SetAngularDamping(1.20f);
    Body->SetMassOverrideInKg(NAME_None, 1.2f, true);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (BodyMesh.Succeeded())
    {
        Body->SetStaticMesh(BodyMesh.Object);
        Body->SetRelativeScale3D(FVector(1.6f, 0.34f, 0.26f));
    }

    BuildGeometry();

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(Body);
    CameraBoom->TargetArmLength = 500.0f;
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 7.0f;
    CameraBoom->bInheritPitch = false;
    CameraBoom->bInheritRoll = false;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom);

    AutoPossessPlayer = EAutoReceiveInput::Player0;
}

ABorn2FlapFlightPawn::~ABorn2FlapFlightPawn() = default;

void ABorn2FlapFlightPawn::BuildGeometry()
{
    // Nose cone: UE's cone points along +Z by default; pitch -90° lays its
    // apex forward (+X) so it reads as a beak/nose.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
    if (ConeMesh.Succeeded())
    {
        Nose = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Nose"));
        Nose->SetupAttachment(Body);
        Nose->SetStaticMesh(ConeMesh.Object);
        Nose->SetRelativeLocation(FVector(0.95f, 0.0f, 0.0f));
        Nose->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
        Nose->SetRelativeScale3D(FVector(0.34f, 0.34f, 0.6f));
        Nose->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // Tail: horizontal stabiliser (elevator) + vertical fin (rudder).
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (CubeMesh.Succeeded())
    {
        TailHorizontal = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TailHorizontal"));
        TailHorizontal->SetupAttachment(Body);
        TailHorizontal->SetStaticMesh(CubeMesh.Object);
        TailHorizontal->SetRelativeLocation(FVector(-1.15f, 0.0f, 0.02f));
        TailHorizontal->SetRelativeScale3D(FVector(0.45f, 0.55f, 0.03f));
        TailHorizontal->SetCollisionEnabled(ECollisionEnabled::NoCollision);

        TailVertical = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TailVertical"));
        TailVertical->SetupAttachment(Body);
        TailVertical->SetStaticMesh(CubeMesh.Object);
        TailVertical->SetRelativeLocation(FVector(-1.15f, 0.0f, 0.20f));
        TailVertical->SetRelativeScale3D(FVector(0.40f, 0.03f, 0.35f));
        TailVertical->SetCollisionEnabled(ECollisionEnabled::NoCollision);

        // Wings hinge on shoulder pivots so the flap rotation happens around
        // the root chord, not the wing's centre. Wing extends outward from the
        // shoulder; the shoulder scene component carries the flap roll.
        RightShoulder = CreateDefaultSubobject<USceneComponent>(TEXT("RightShoulder"));
        RightShoulder->SetupAttachment(Body);
        RightShoulder->SetRelativeLocation(FVector(0.0f, 0.18f, 0.05f));

        LeftShoulder = CreateDefaultSubobject<USceneComponent>(TEXT("LeftShoulder"));
        LeftShoulder->SetupAttachment(Body);
        LeftShoulder->SetRelativeLocation(FVector(0.0f, -0.18f, 0.05f));

        RightWing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightWing"));
        RightWing->SetupAttachment(RightShoulder);
        RightWing->SetStaticMesh(CubeMesh.Object);
        RightWing->SetRelativeLocation(FVector(0.0f, 0.45f, 0.0f));
        RightWing->SetRelativeScale3D(FVector(0.50f, 0.90f, 0.04f));
        RightWing->SetCollisionEnabled(ECollisionEnabled::NoCollision);

        LeftWing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftWing"));
        LeftWing->SetupAttachment(LeftShoulder);
        LeftWing->SetStaticMesh(CubeMesh.Object);
        LeftWing->SetRelativeLocation(FVector(0.0f, -0.45f, 0.0f));
        LeftWing->SetRelativeScale3D(FVector(0.50f, 0.90f, 0.04f));
        LeftWing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void ABorn2FlapFlightPawn::BeginPlay()
{
    Super::BeginPlay();
    MathBridge = MakeUnique<FBorn2FlapMathBridge>();
    MathBridge->Load();
}

void ABorn2FlapFlightPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    TelemetryAccumulatorSeconds += DeltaSeconds;

    // Direct keyboard fallback so the prototype is flyable without an
    // Enhanced Input action asset.
    if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
    {
        const float KeyboardThrottle =
            (PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f) -
            (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f);
        const float KeyboardRoll =
            (PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f) -
            (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f);
        const float KeyboardPitch =
            (PlayerController->IsInputKeyDown(EKeys::Up) ? 1.0f : 0.0f) -
            (PlayerController->IsInputKeyDown(EKeys::Down) ? 1.0f : 0.0f);
        const float KeyboardYaw =
            (PlayerController->IsInputKeyDown(EKeys::Right) ? 1.0f : 0.0f) -
            (PlayerController->IsInputKeyDown(EKeys::Left) ? 1.0f : 0.0f);
        // Assign every frame, including zero.  Leaving the previous non-zero
        // value in place made a short key press apply forever.
        ThrottleInput = KeyboardThrottle;
        RollInput = KeyboardRoll;
        PitchInput = KeyboardPitch;
        YawInput = KeyboardYaw;
    }

    AccumulatorSeconds = FMath::Min(AccumulatorSeconds + DeltaSeconds, 0.1);
    while (AccumulatorSeconds >= MathStepSeconds)
    {
        StepMath(MathStepSeconds);
        AccumulatorSeconds -= MathStepSeconds;
    }

    if (TelemetryAccumulatorSeconds >= 1.0)
    {
        TelemetryAccumulatorSeconds = 0.0;
        const FVector Position = Body->GetComponentLocation();
        const FVector Velocity = Body->GetPhysicsLinearVelocity();
        const FVector AngularVelocity = Body->GetPhysicsAngularVelocityInRadians();
        UE_LOG(LogTemp, Display,
            TEXT("FlightTelemetry pos=(%.1f,%.1f,%.1f)cm vel=(%.1f,%.1f,%.1f)cm/s ang=(%.2f,%.2f,%.2f)rad/s input=(%.2f,%.2f,%.2f,%.2f)"),
            Position.X, Position.Y, Position.Z,
            Velocity.X, Velocity.Y, Velocity.Z,
            AngularVelocity.X, AngularVelocity.Y, AngularVelocity.Z,
            ThrottleInput, RollInput, PitchInput, YawInput);
    }

    if (MathBridge && !MathBridge->IsReady())
    {
        DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 100),
            MathBridge->GetStatus(), nullptr, FColor::Yellow, 0.0f, true);
    }
    else if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(2, 0.0f, FColor::White,
            FString::Printf(TEXT("flap L=%+.1f R=%+.1f deg | flapping=%d | SoC=%.1f%%"),
                LastLeftFlapDeg, LastRightFlapDeg, bIsFlapping ? 1 : 0, LastSoc * 100.0f));
    }
}

void ABorn2FlapFlightPawn::StepMath(double DeltaTimeSeconds)
{
    if (!MathBridge || !MathBridge->IsReady())
    {
        return;
    }

    const FTransform BodyTransform = Body->GetComponentTransform();
    const FVector WorldPosition = BodyTransform.GetLocation();
    const FVector WorldVelocity = Body->GetPhysicsLinearVelocity();
    const FVector WorldAngularVelocity = Body->GetPhysicsAngularVelocityInRadians();
    const auto IsFiniteVector = [](const FVector& Value)
    {
        return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
    };
    const bool bNumericalEscape =
        !IsFiniteVector(WorldPosition) || !IsFiniteVector(WorldVelocity) || !IsFiniteVector(WorldAngularVelocity) ||
        WorldPosition.SizeSquared() > FMath::Square(500000.0) ||
        WorldVelocity.SizeSquared() > FMath::Square(50000.0) ||
        WorldAngularVelocity.SizeSquared() > FMath::Square(25.0);
    if (bNumericalEscape)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("FlightSafetyReset pos=%s vel=%s ang=%s"),
            *WorldPosition.ToString(), *WorldVelocity.ToString(), *WorldAngularVelocity.ToString());
        Body->SetWorldLocationAndRotation(FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
        Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        return;
    }
    const FVector LinearVelocity = BodyTransform.InverseTransformVectorNoScale(
        WorldVelocity / 100.0);
    const FVector AngularVelocity = BodyTransform.InverseTransformVectorNoScale(
        WorldAngularVelocity);

    B2F_PilotInput Pilot{};
    Pilot.throttle = ThrottleInput;
    Pilot.roll = RollInput;
    Pilot.pitch = PitchInput;
    Pilot.yaw = YawInput;

    B2F_BodyState BodyState{};
    BodyState.delta_time_s = DeltaTimeSeconds;
    BodyState.linear_velocity_m_s[0] = LinearVelocity.X;
    BodyState.linear_velocity_m_s[1] = LinearVelocity.Y;
    BodyState.linear_velocity_m_s[2] = LinearVelocity.Z;
    BodyState.angular_velocity_rad_s[0] = AngularVelocity.X;
    BodyState.angular_velocity_rad_s[1] = AngularVelocity.Y;
    BodyState.angular_velocity_rad_s[2] = AngularVelocity.Z;

    B2F_FirmwareOutput Output{};
    if (!MathBridge->Step(Pilot, BodyState, Output))
    {
        return;
    }

    const FVector BodyForceN(Output.force_n[0], Output.force_n[1], Output.force_n[2]);
    const FVector BodyMomentNm = FVector(Output.moment_n_m[0], Output.moment_n_m[1], Output.moment_n_m[2])
        .GetClampedToMaxSize(25.0);
    const FVector ForceN = BodyTransform.TransformVectorNoScale(BodyForceN).GetClampedToMaxSize(500.0);
    const FVector MomentNm = BodyTransform.TransformVectorNoScale(BodyMomentNm);
    // Convert each fixed-step load to an impulse so multiple math steps in one
    // rendered frame do not multiply a frame-scoped force.
    Body->AddImpulse(ForceN * (100.0 * DeltaTimeSeconds), NAME_None, false);
    Body->AddAngularImpulseInRadians(MomentNm * (10000.0 * DeltaTimeSeconds), NAME_None, false);

    LastLeftFlapDeg = static_cast<float>(Output.left_flap_deg);
    LastRightFlapDeg = static_cast<float>(Output.right_flap_deg);
    LastSoc = static_cast<float>(Output.battery_soc);
    bIsFlapping = (Output.flags & 1u) != 0;

    ApplyFlapAngles(LastLeftFlapDeg, LastRightFlapDeg);

    const FVector Start = Body->GetComponentLocation();
    DrawDebugDirectionalArrow(GetWorld(), Start, Start + ForceN * 30.0,
        20.0f, FColor::Cyan, false, 0.0f, 0, 2.0f);
}

void ABorn2FlapFlightPawn::ApplyFlapAngles(float LeftDeg, float RightDeg)
{
    const float LeftRoll = LeftDeg * FlapVisualGain;
    const float RightRoll = -RightDeg * FlapVisualGain;
    if (LeftShoulder)
    {
        LeftShoulder->SetRelativeRotation(FRotator(0.0f, 0.0f, LeftRoll));
    }
    if (RightShoulder)
    {
        RightShoulder->SetRelativeRotation(FRotator(0.0f, 0.0f, RightRoll));
    }
}

void ABorn2FlapFlightPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    PlayerInputComponent->BindAxis(TEXT("Throttle"), this, &ABorn2FlapFlightPawn::SetThrottle);
    PlayerInputComponent->BindAxis(TEXT("Roll"), this, &ABorn2FlapFlightPawn::SetRoll);
    PlayerInputComponent->BindAxis(TEXT("Pitch"), this, &ABorn2FlapFlightPawn::SetPitch);
    PlayerInputComponent->BindAxis(TEXT("Yaw"), this, &ABorn2FlapFlightPawn::SetYaw);
}
