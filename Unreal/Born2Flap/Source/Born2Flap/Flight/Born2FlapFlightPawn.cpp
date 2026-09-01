#include "Flight/Born2FlapFlightPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/SpringArmComponent.h"
#include "Math/Born2FlapMathBridge.h"
#include "UObject/ConstructorHelpers.h"

ABorn2FlapFlightPawn::ABorn2FlapFlightPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    SetRootComponent(Body);
    Body->SetSimulatePhysics(true);
    Body->SetLinearDamping(0.05f);
    Body->SetAngularDamping(0.10f);
    Body->SetMassOverrideInKg(NAME_None, 1.2f, true);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (BodyMesh.Succeeded())
    {
        Body->SetStaticMesh(BodyMesh.Object);
        Body->SetRelativeScale3D(FVector(1.5, 0.35, 0.25));
    }

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(Body);
    CameraBoom->TargetArmLength = 500.0f;
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 7.0f;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom);

    AutoPossessPlayer = EAutoReceiveInput::Player0;
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
    AccumulatorSeconds = FMath::Min(AccumulatorSeconds + DeltaSeconds, 0.1);
    while (AccumulatorSeconds >= MathStepSeconds)
    {
        StepMath(MathStepSeconds);
        AccumulatorSeconds -= MathStepSeconds;
    }

    if (MathBridge && !MathBridge->IsReady())
    {
        DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 100),
            MathBridge->GetStatus(), nullptr, FColor::Yellow, 0.0f, true);
    }
}

void ABorn2FlapFlightPawn::StepMath(double DeltaTimeSeconds)
{
    if (!MathBridge || !MathBridge->IsReady())
    {
        return;
    }

    const FVector LinearVelocity = Body->GetPhysicsLinearVelocity() / 100.0;
    const FVector AngularVelocity = Body->GetPhysicsAngularVelocityInRadians();
    B2F_VehicleInput Input{};
    Input.delta_time_s = DeltaTimeSeconds;
    Input.linear_velocity_m_s[0] = LinearVelocity.X;
    Input.linear_velocity_m_s[1] = LinearVelocity.Y;
    Input.linear_velocity_m_s[2] = LinearVelocity.Z;
    Input.angular_velocity_rad_s[0] = AngularVelocity.X;
    Input.angular_velocity_rad_s[1] = AngularVelocity.Y;
    Input.angular_velocity_rad_s[2] = AngularVelocity.Z;
    Input.throttle = ThrottleInput;
    Input.roll = RollInput;
    Input.pitch = PitchInput;
    Input.yaw = YawInput;

    B2F_VehicleOutput Output{};
    if (!MathBridge->Step(Input, Output))
    {
        return;
    }

    const FVector ForceN(Output.force_n[0], Output.force_n[1], Output.force_n[2]);
    const FVector MomentNm(Output.moment_n_m[0], Output.moment_n_m[1], Output.moment_n_m[2]);
    // Convert each fixed-step load to an impulse so multiple math steps in one
    // rendered frame do not accidentally multiply a frame-scoped force.
    Body->AddImpulse(ForceN * (100.0 * DeltaTimeSeconds), NAME_None, false);
    Body->AddAngularImpulseInRadians(
        MomentNm * (10000.0 * DeltaTimeSeconds), NAME_None, false);

    const FVector Start = Body->GetComponentLocation();
    DrawDebugDirectionalArrow(GetWorld(), Start, Start + ForceN * 30.0,
        20.0f, FColor::Cyan, false, 0.0f, 0, 2.0f);
}

void ABorn2FlapFlightPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    PlayerInputComponent->BindAxis(TEXT("Throttle"), this, &ABorn2FlapFlightPawn::SetThrottle);
    PlayerInputComponent->BindAxis(TEXT("Roll"), this, &ABorn2FlapFlightPawn::SetRoll);
    PlayerInputComponent->BindAxis(TEXT("Pitch"), this, &ABorn2FlapFlightPawn::SetPitch);
    PlayerInputComponent->BindAxis(TEXT("Yaw"), this, &ABorn2FlapFlightPawn::SetYaw);
}
