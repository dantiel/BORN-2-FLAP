#include "Flight/Born2FlapFlightPawn.h"
#include "Flight/Born2FlapWingMesh.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "UObject/ConstructorHelpers.h"
#include "Input/Born2FlapRcController.h"
#include "Game/Born2FlapGameMode.h"
#include "World/Born2FlapValley.h"
#include "World/Born2FlapWind.h"
#include "Audio/Born2FlapAudioSynth.h"
#include "Audio/Born2FlapAeroAudio.h"
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
    // SetCollisionProfileName("PhysicsActor") and SetSimulatePhysics both resolve
    // the simple physical material via GEngine, which is null during native CDO
    // construction (the cook commandlet builds CDOs before GEngine exists).
    // Guarding them avoids a fatal "GetSimplePhysicalMaterial" error during
    // packaging; spawned instances still apply the physics profile + simulation.
    if (!HasAnyFlags(RF_ClassDefaultObject))
    {
        Body->SetCollisionProfileName(TEXT("PhysicsActor"));
        Body->SetSimulatePhysics(true);
    }
    Body->SetLinearDamping(0);  // Native body/wing drag already removes energy.
    Body->SetAngularDamping(0); // Aerodynamic wing/tail damping only.
    Body->BodyInstance.bUseCCD = true;
    // Chaos scales the mass geometry, not the three diagonal tensor entries.
    // (8,1.5,2) accidentally modelled a several-metre fuselage and ~1.7 kg m2
    // pitch inertia. This approximation distributes mass across the wings:
    // the resulting body tensor is about (.039,.046,.046) kg m2 at .45 kg.
    Body->BodyInstance.InertiaTensorScale = FVector(1, 3, 3);
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
    AudioSynth = CreateDefaultSubobject<UBorn2FlapAudioSynth>(TEXT("AeroAudioSynth"));
    AudioSynth->SetupAttachment(Body);
    AudioSynth->SetAutoActivate(true);
    GroundCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("GroundCamera"));
    GroundCamera->SetupAttachment(Body);
    GroundCamera->SetAbsolute(true,true,true);
    GroundCamera->SetAutoActivate(false);
    FpvCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FpvCamera"));
    FpvCamera->SetupAttachment(Body);
    FpvCamera->SetRelativeLocation(FVector(76,0,14));
    FpvCamera->SetFieldOfView(95);
    FpvCamera->SetAutoActivate(false);
    AutoPossessPlayer = EAutoReceiveInput::Player0;
    // Default hangar tuning = the prototype flight hardware (3S 1.3 Ah, 8 Nm
    // servo) plus the simulator's controller base. LoadFlightPreferences may
    // override these before the first ApplyTuning.
    Tuning.servo_no_load_speed_deg_s = 1200;
    Tuning.servo_stall_torque_nm = 8;
    Tuning.servo_backdrive_deg_s_nm = 20;
    Tuning.battery_voltage = 11.1;
    Tuning.battery_resistance_ohm = 0.08;
    Tuning.battery_capacity_ah = 1.3;
    Tuning.flap_base_freq_dhz = 32;
    Tuning.mount_angle_deg = 0;
    Tuning.glide_angle_deg = -4;
    Tuning.stroke_ferocity = 50;
    Tuning.aileron_scale = 60;
    Tuning.elevator_scale = 55;
}
ABorn2FlapFlightPawn::~ABorn2FlapFlightPawn() = default;
namespace
{
float TuningClamp(ETuningField Field, float Value)
{
    switch (Field)
    {
    case ETuningField::ServoSpeed:        return FMath::Clamp(Value, 100.f, 2400.f);
    case ETuningField::StallTorque:       return FMath::Clamp(Value, 0.5f, 20.f);
    case ETuningField::Backdrive:         return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::BatteryVoltage:    return FMath::Clamp(Value, 3.7f, 22.2f);
    case ETuningField::BatteryResistance: return FMath::Clamp(Value, 0.01f, 0.5f);
    case ETuningField::BatteryCapacity:   return FMath::Clamp(Value, 0.1f, 5.f);
    case ETuningField::FlapBaseFreq:      return FMath::Clamp(Value, 10.f, 200.f);
    case ETuningField::MountAngle:        return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::GlideAngle:        return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::StrokeFerocity:    return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::AileronScale:      return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::ElevatorScale:     return FMath::Clamp(Value, 0.f, 100.f);
    default: return 0.f;
    }
}
} // namespace
float ABorn2FlapFlightPawn::GetTuning(ETuningField Field) const
{
    switch (Field)
    {
    case ETuningField::ServoSpeed:        return float(Tuning.servo_no_load_speed_deg_s);
    case ETuningField::StallTorque:       return float(Tuning.servo_stall_torque_nm);
    case ETuningField::Backdrive:         return float(Tuning.servo_backdrive_deg_s_nm);
    case ETuningField::BatteryVoltage:    return float(Tuning.battery_voltage);
    case ETuningField::BatteryResistance: return float(Tuning.battery_resistance_ohm);
    case ETuningField::BatteryCapacity:   return float(Tuning.battery_capacity_ah);
    case ETuningField::FlapBaseFreq:      return float(Tuning.flap_base_freq_dhz);
    case ETuningField::MountAngle:        return float(Tuning.mount_angle_deg);
    case ETuningField::GlideAngle:        return float(Tuning.glide_angle_deg);
    case ETuningField::StrokeFerocity:    return float(Tuning.stroke_ferocity);
    case ETuningField::AileronScale:      return float(Tuning.aileron_scale);
    case ETuningField::ElevatorScale:     return float(Tuning.elevator_scale);
    default: return 0.f;
    }
}
void ABorn2FlapFlightPawn::SetTuning(ETuningField Field, float Value)
{
    Value = TuningClamp(Field, Value);
    switch (Field)
    {
    case ETuningField::ServoSpeed:        Tuning.servo_no_load_speed_deg_s = Value; break;
    case ETuningField::StallTorque:       Tuning.servo_stall_torque_nm = Value; break;
    case ETuningField::Backdrive:         Tuning.servo_backdrive_deg_s_nm = Value; break;
    case ETuningField::BatteryVoltage:    Tuning.battery_voltage = Value; break;
    case ETuningField::BatteryResistance: Tuning.battery_resistance_ohm = Value; break;
    case ETuningField::BatteryCapacity:   Tuning.battery_capacity_ah = Value; break;
    case ETuningField::FlapBaseFreq:      Tuning.flap_base_freq_dhz = Value; break;
    case ETuningField::MountAngle:        Tuning.mount_angle_deg = Value; break;
    case ETuningField::GlideAngle:        Tuning.glide_angle_deg = Value; break;
    case ETuningField::StrokeFerocity:    Tuning.stroke_ferocity = Value; break;
    case ETuningField::AileronScale:      Tuning.aileron_scale = Value; break;
    case ETuningField::ElevatorScale:     Tuning.elevator_scale = Value; break;
    default: return;
    }
    ApplyTuning();
}
void ABorn2FlapFlightPawn::ApplyTuning()
{
    if (MathBridge && MathBridge->IsReady())
        MathBridge->Reconfigure(Tuning);
}
void ABorn2FlapFlightPawn::BuildGeometry()
{
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Bird"));
    VisualRoot->SetupAttachment(Body);
    PrototypeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Prototype"));
    PrototypeRoot->SetupAttachment(VisualRoot);
    // Locations are centimetres; the root has unit scale.
    auto Part = [&](FName Name, USceneComponent *Parent, UStaticMesh *Mesh, FVector Loc, FVector Scale, FRotator Rot,
                    const TCHAR *Palette) {
        auto *C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        C->SetupAttachment(Parent);
        C->SetStaticMesh(Mesh);
        C->SetRelativeLocation(Loc);
        C->SetRelativeScale3D(Scale);
        C->SetRelativeRotation(Rot);
        // SetCollisionEnabled resolves the simple physical material via GEngine,
        // which is null during native CDO construction (the cook commandlet).
        // Skipping it on the template is safe: spawned instances re-run this
        // constructor without the ClassDefaultObject flag and disable collision.
        if (!HasAnyFlags(RF_ClassDefaultObject))
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->ComponentTags.Add(FName(Palette));
        return C;
    };
    Part(TEXT("Fuselage"), PrototypeRoot, Sphere.Object, FVector::ZeroVector, FVector(.95, .27, .29),
         FRotator::ZeroRotator, TEXT("Ivory"));
    Part(TEXT("Head"), PrototypeRoot, Sphere.Object, FVector(40, 0, 12), FVector(.32, .25, .27), FRotator::ZeroRotator,
         TEXT("Teal"));
    Part(TEXT("Beak"), PrototypeRoot, Cone.Object, FVector(59, 0, 10), FVector(.12, .12, .3), FRotator(-90, 0, 0),
         TEXT("Gold"));
    Part(TEXT("TailBoom"), PrototypeRoot, Sphere.Object, FVector(-59, 0, 0), FVector(.65, .07, .07),
         FRotator::ZeroRotator, TEXT("Teal"));
    for (int Side : {-1, 1})
    {
        Part(*FString::Printf(TEXT("Eye%d"), Side), PrototypeRoot, Sphere.Object, FVector(48, Side * 10, 17),
             FVector(.065, .04, .065), FRotator::ZeroRotator, TEXT("Ink"));
        auto *Shoulder = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("Shoulder%d"), Side));
        Shoulder->SetupAttachment(PrototypeRoot);
        Shoulder->SetRelativeLocation(FVector(3, Side * 12, 10));
        if (Side < 0)
            LeftShoulder = Shoulder;
        else
            RightShoulder = Shoulder;
        Part(*FString::Printf(TEXT("Tail%d"), Side), PrototypeRoot, Sphere.Object, FVector(-85, Side * 14, -3),
             FVector(.36, .42, .025), FRotator(0, 0, Side * 12), TEXT("Teal"));
    }
}
int32 ABorn2FlapFlightPawn::DefaultBirdModel() const
{
    // Every field owns a silhouette: the Peregrine hunts the Shiomori coast,
    // the RavenCrow rules the nature valley, and the Prototype trains in the
    // indoor arena. Loading a map therefore swaps the bird automatically.
    const auto* GameMode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    if (GameMode && GameMode->IsCoastLevel())
        return 2;                       // Shiomori Bay -> Peregrine (falcon)
    if (GameMode && !GameMode->IsNatureLevel())
        return 1;                       // Training -> Prototype
    return 0;                           // Ravenstonefield / nature -> RavenCrow
}
void ABorn2FlapFlightPawn::BeginPlay()
{
    Super::BeginPlay();
    if (AudioSynth)
        AudioSynth->Start();
    Body->SetMassOverrideInKg(NAME_None, .45f, true);
    UE_LOG(LogTemp, Display, TEXT("FlightBody massKg=%.4f inertiaKgM2=%s"), Body->GetMass(),
           *(Body->GetInertiaTensor() / 10000.0).ToString());
    // Fixed daylight exposure, compatible with either project luminance mode.
    // The legacy exposure range otherwise clips a physically lit sky to white.
    const auto *ExtendedRange =
        IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
    const auto *ExposureMode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    const bool bRaven = ExposureMode && ExposureMode->IsNatureLevel();
    const float DaylightExposure = ExtendedRange && ExtendedRange->GetInt() ? (bRaven ? 13.2f : 14.f) : 10000.f;
    Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
    Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
    Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
    Camera->PostProcessSettings.AutoExposureMinBrightness = DaylightExposure;
    Camera->PostProcessSettings.AutoExposureMaxBrightness = DaylightExposure;
    Camera->PostProcessSettings.AutoExposureBias = 0;
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
    Camera->PostProcessSettings.MotionBlurAmount = 0;
    GroundCamera->PostProcessSettings = Camera->PostProcessSettings;
    GroundCamera->PostProcessSettings.MotionBlurAmount = .6f;
    GroundCamera->PostProcessSettings.bOverride_MotionBlurMax = true;
    GroundCamera->PostProcessSettings.MotionBlurMax = 5.f;
    GroundCamera->PostProcessSettings.bOverride_MotionBlurTargetFPS = true;
    GroundCamera->PostProcessSettings.MotionBlurTargetFPS = 60;
    FpvCamera->PostProcessSettings = GroundCamera->PostProcessSettings;
    FpvCamera->PostProcessSettings.MotionBlurAmount = .2f;
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
    bRavenFlightTest = FParse::Param(FCommandLine::Get(), TEXT("B2FRavenFlightTest"));
    bHandlingTest = FParse::Param(FCommandLine::Get(), TEXT("B2FHandlingTest"));
    bFlightTest = bSoakTest || bRavenFlightTest || bHandlingTest || FParse::Param(FCommandLine::Get(), TEXT("B2FFlightTest"));
    bDesktopInputTest = FParse::Param(FCommandLine::Get(), TEXT("B2FDesktopInputTest"));
    if (!bFlightTest && !bDesktopInputTest)
        RcController = MakeUnique<FBorn2FlapRcController>();
    bBlind = FParse::Param(FCommandLine::Get(), TEXT("B2FBlind"));
    if (bBlind)
        UE_LOG(LogTemp, Display, TEXT("BlindFlight: bird hidden — fly by ear (F5 toggles)"));
    BuildRavenCrow();
    for (int32 Side : {-1,1})
    {
        auto* Wing=NewObject<UBorn2FlapWingMesh>(this,*FString::Printf(TEXT("PrototypeMembrane%d"),Side));
        AddInstanceComponent(Wing);
        Wing->SetupAttachment(Side<0 ? LeftShoulder.Get() : RightShoulder.Get());
        Wing->RegisterComponent();
        Wing->InitializeWing(Side,1);
    }
    SetWingPaint(WingPaint);
    LoadFlightPreferences();
    BirdModel = DefaultBirdModel();
    SelectBirdModel(BirdModel);
    ResetFlight();
    SetBlindFlight(bBlind);
    if (bDesktopInputTest && FParse::Param(FCommandLine::Get(), TEXT("B2FBirdPreview")))
    {
        CameraBoom->TargetArmLength = 360;
        CameraBoom->SetRelativeLocation(FVector(0,0,24));
        CameraBoom->bInheritYaw = false;
        CameraBoom->SetRelativeRotation(FRotator(-52,135,0));
        Camera->SetFieldOfView(65);
    }
    if (auto *PC = Cast<APlayerController>(GetController()))
    {
        PC->SetInputMode(FInputModeGameOnly());
        PC->bShowMouseCursor = false;
    }
}
void ABorn2FlapFlightPawn::SetWingPaint(UTexture2D* Texture)
{
    WingPaint=Texture;
    TInlineComponentArray<UBorn2FlapWingMesh*> Wings(this);
    for(auto* Wing : Wings) Wing->SetPaintTexture(Texture);
}

void ABorn2FlapFlightPawn::ResetFlight(bool bSafety)
{
    if (bSafety)
        ++SafetyResets;
    // Recreate all firmware/servo/aeroelastic/battery/history state. RTS stays pinned.
    bHealthy = MathBridge && MathBridge->Load();
    if (bHealthy)
        ApplyTuning();
    bFlying = bReturning = false;
    Desktop = {};
    Throttle = RollInput = YawInput = PitchInput = LeftFlap = RightFlap = 0;
    BatterySoc = 1;
    Accumulator = 0;
    AeroForce = AeroMoment = FVector::ZeroVector;
    Body->SetWorldLocationAndRotation(FVector(0, 0, 80), FRotator::ZeroRotator, false, nullptr,
                                      ETeleportType::TeleportPhysics);
    Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    Body->WakeAllRigidBodies();
    bCameraGrounded = false;
    GroundSettleTime = 0;
    RememberLanding(Body->GetComponentLocation());
    UE_LOG(LogTemp, Display, TEXT("FlightReset safety=%d backend=%d"), bSafety, bHealthy);
}
float ABorn2FlapFlightPawn::GetAltitude() const
{
    const FVector P = Body->GetComponentLocation();
    const auto *Mode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    const double Ground = Mode ? Mode->GroundHeight(P.X, P.Y) : 0;
    return FMath::Max(0.f, float((P.Z - Ground) / 100.0 - .12));
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
    if (!bHealthy || (!bCameraGrounded && GetAltitude() > .4f) || GetSpeed() > 1.f)
        return;
    // One explicit hand launch supplies initial momentum; it cannot repeat in
    // the air. Every subsequent acceleration comes from aero forces/gravity.
    FRotator Heading(0, Body->GetComponentRotation().Yaw, 0);
    RememberLanding(Body->GetComponentLocation());
    bCameraGrounded = false;
    GroundSettleTime = 0;
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
        return MathBridge && !MathBridge->IsReady() ? MathBridge->GetStatus()
                                                  : TEXT("Flight core stopped - press R to reset");
    if (bReturning)
        return TEXT("FIELD EDGE - turn back with the sticks");
    if (!bFlying)
        return TEXT("SPACE hand-launch / W throttle on ground");
    if (GetSpeed() < 4.5f)
        return TEXT("LOW AIRSPEED - lower the nose");
    if (Throttle < .08f)
        return TEXT("GLIDING - airspeed and height are being spent");
    return Throttle > .85f ? TEXT("POWER STROKES") : TEXT("FLAPPING");
}
void ABorn2FlapFlightPawn::SetBlindFlight(bool bOn)
{
    bBlind = bOn;
    if (VisualRoot)
        VisualRoot->SetVisibility(!bBlind, true);
    SelectBirdModel(BirdModel);
}
bool ABorn2FlapFlightPawn::StepMath(float DeltaSeconds)
{
    if (!bHealthy)
        return false;
    FQuat Rotation = Body->GetComponentQuat();
    FVector Velocity = Body->GetPhysicsLinearVelocity() / 100.0;
    FVector Omega = Body->GetPhysicsAngularVelocityInRadians();
    const FVector Inertia = Body->GetInertiaTensor() / 10000.0;
    // The atmospheric field: wind enters as air-relative velocity (aerodynamics)
    // and as phase noise (the resonance layer the servo must lock against).
    const FVector BodyPos = Body->GetComponentLocation();
    // No-slip ground shelter: a resting bird is below the free-stream wind.
    // Smoothly recover the atmospheric field over the first two metres.
    const double Shelter=FMath::SmoothStep(0.0,2.0,double(GetAltitude()));
    const auto* WindMode=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    const FVector Wind = bFlightTest ? FVector::ZeroVector : (WindMode?WindMode->WindAt(BodyPos,WorldTime):Born2FlapWind::Sample(BodyPos,WorldTime))*Shelter;
    MathBridge->InjectWindPhaseNoise(bFlightTest ? 0.0 : (WindMode && WindMode->IsCoastLevel() ? Wind.Size()*.15 : Born2FlapWind::PhaseNoise(BodyPos, WorldTime))*Shelter);
    B2F_PilotInput Pilot{};
    Pilot.throttle = Throttle;
    Pilot.roll = RollInput;
    Pilot.pitch = PitchInput;
    Pilot.yaw = YawInput;
    B2F_BodyState State{};
    State.delta_time_s = MathDt;
    Accumulator = FMath::Min(Accumulator + DeltaSeconds, .1);
    FVector ForceSum = FVector::ZeroVector, MomentSum = FVector::ZeroVector;
    int32 Steps = 0;
    while (Accumulator >= MathDt)
    {
        const FVector V = Rotation.UnrotateVector(Velocity - Wind);
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
            AeroForce = AeroMoment = FVector::ZeroVector;
            UE_LOG(LogTemp, Error, TEXT("FlightMathRejected: disarmed, R recreates firmware context"));
            return false;
        }
        const FVector WorldForce = Rotation.RotateVector(F);
        const FVector WorldMoment = Rotation.RotateVector(M);
        ForceSum += WorldForce;
        MomentSum += WorldMoment;
        LeftFlap = O.left_flap_deg;
        RightFlap = O.right_flap_deg;
        BatterySoc = O.battery_soc;
        LastMechanicalPower = O.mechanical_power_w;
        LastPhaseError = O.phase_error;
        LastKGainMod = O.k_gain_mod;
        // Predict BOTH linear and rotational feedback between wing evaluations.
        // Holding angular velocity for a whole 30 Hz frame made aerodynamic
        // roll damping overshoot. Chaos receives the mean loads once and stays
        // authoritative for the actual rigid body and contacts.
        Velocity += (WorldForce / Body->GetMass() + FVector(0, 0, -9.81)) * MathDt;
        Omega += Rotation.RotateVector(M / Inertia) * MathDt;
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
    }
    return true;
}

void ABorn2FlapFlightPawn::UpdateAeroAudio(float Dt)
{
    if (!AudioSynth || !bHealthy)
        return;

    const FVector P = Body->GetComponentLocation();
    const FVector V = Body->GetPhysicsLinearVelocity() / 100.0;
    const FVector Wind = Born2FlapWind::Sample(P, WorldTime)*FMath::SmoothStep(0.0,2.0,double(GetAltitude()));
    const FVector AirVel = V - Wind;

    born2flap::aeroaudio::FTelemetry Tel;
    Tel.airspeed = AirVel.Size();
    Tel.wingbeat_hz = 2.0 + 5.0 * Throttle;
    Tel.altitude = GetAltitude();
    Tel.thermal_strength = FMath::Clamp((double)Wind.Z / 6.0, 0.0, 1.0);
    const double Load = FMath::Clamp(LastMechanicalPower / 50.0, 0.0, 1.0);
    Tel.servo_load_l = Load;
    Tel.servo_load_r = Load;
    Tel.sweep_rate = Dt > 0.f ? FMath::Abs((double)(LeftFlap - PrevLeftFlap)) / Dt : 0.0;
    Tel.phase_error_rad = LastPhaseError;
    Tel.k_gain_mod = LastKGainMod;
    Tel.stall_margin = FMath::Clamp((Tel.airspeed - 3.5) / 6.0, 0.0, 1.0);

    // Listener-relative perspective from the camera.
    const UCameraComponent* Listener = bGroundView ? GroundCamera : (bFpvAirView ? FpvCamera : Camera);
    const FVector CamLoc = Listener->GetComponentLocation();
    const FVector ToBird = P - CamLoc;
    const double DistCm = ToBird.Size();
    Tel.listener_distance = FMath::Max(DistCm / 100.0, 0.5);
    const FVector ToBirdN = DistCm > 1.0 ? ToBird / DistCm : Listener->GetForwardVector();
    const FVector CamFwd = Listener->GetForwardVector();
    Tel.listener_bearing = FMath::Atan2(FVector::DotProduct(Listener->GetRightVector(), ToBirdN), FVector::DotProduct(CamFwd, ToBirdN));
    Tel.approach_speed = bGroundView ? -FVector::DotProduct(V, ToBirdN) : 0.0;

    born2flap::ui::FProps Voices[5];
    born2flap::aeroaudio::Mix(Tel, Voices);
    const char* const* Names = born2flap::aeroaudio::VoiceNames();
    // Headroom before the synth sums/clamps voices, especially at onboard distance.
    const double CameraGain=bGroundView ? 1.0 : (bFpvAirView ? .40 : .50);
    for (int32 I = 0; I < 5; ++I)
    {
        const double Gain=born2flap::aeroaudio::GetNum(Voices[I],"gain");
        born2flap::aeroaudio::SetNum(Voices[I],"gain",Gain*CameraGain);
        AudioSynth->SetVoiceParams(Names[I], Voices[I]);
    }

    PrevLeftFlap = LeftFlap;
}

void ABorn2FlapFlightPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (auto* PC = Cast<APlayerController>(GetController()); PC && PC->WasInputKeyJustPressed(EKeys::F8))
    { OpenFlightSettings(); return; }
    if (IsFlightSettingsOpen()) return;
    const float Dt = FMath::Clamp(DeltaSeconds, 0.f, .1f);
    WorldTime += Dt;
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FAudioTest")))
    {
        if(WorldTime>3 && WorldTime<3+Dt) LaunchFlight();
        if(WorldTime>15) {
            const bool Pass=AudioSynth && AudioSynth->CheckRenderedAudio();
            FPlatformMisc::RequestExitWithStatus(false,Pass ? 0 : 1);
        }
    }
    float Effort = 0, Steer = 0, Pitch = 0, Roll = 0;
    float MouseX = 0, MouseY = 0, Wheel = 0;
    bool WDown = false, MuteMouseYaw = false, MuteMouseRoll = false;
    bool ResetMouse = false;
    bool Launch = false, Reset = false;
    if (auto *PC = Cast<APlayerController>(GetController()))
    {
        if (PC->WasInputKeyJustPressed(EKeys::F6)) ToggleGroundView();
        if (PC->WasInputKeyJustPressed(EKeys::V)) ToggleFpvView();
        if (RcController)
            RcController->Tick(PC, Dt);
        WDown = PC->IsInputKeyDown(EKeys::W);
        Effort = born2flap::DesktopInput::KeyboardThrottle(
            WDown, PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl),
            PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
        // Signed device deltas, before legacy axis curves/FOV scaling. The desktop
        // transmitter owns sensitivity and keeps the negative half of every channel.
        if (PC->PlayerInput)
        {
            MouseX = PC->PlayerInput->GetRawKeyValue(EKeys::MouseX);
            MouseY = PC->PlayerInput->GetRawKeyValue(EKeys::MouseY);
        }
        Wheel = PC->GetInputAnalogKeyState(EKeys::MouseWheelAxis);
        MuteMouseYaw = PC->IsInputKeyDown(EKeys::LeftMouseButton);
        MuteMouseRoll = PC->IsInputKeyDown(EKeys::RightMouseButton);
        ResetMouse = PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) ||
                     PC->WasInputKeyJustPressed(EKeys::RightMouseButton);
        if (RcController && RcController->IsPanelOpen())
        {
            MouseX = MouseY = Wheel = 0;
            MuteMouseYaw = MuteMouseRoll = true;
        }
        Steer = (PC->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
        Roll = (PC->IsInputKeyDown(EKeys::Right) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::Left) ? 1.f : 0.f);
        Pitch = (PC->IsInputKeyDown(EKeys::Up) ? 1.f : 0.f) -
                (PC->IsInputKeyDown(EKeys::Down) || PC->IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
        Launch = PC->WasInputKeyJustPressed(EKeys::SpaceBar);
        Reset = PC->WasInputKeyJustPressed(EKeys::R);
        if (RcController)
        {
            Launch = (Launch || RcController->LaunchPressed()) && !RcController->IsPanelOpen();
            Reset |= RcController->ResetPressed();
        }
        if (PC->WasInputKeyJustPressed(EKeys::F1))
            bVectors = !bVectors;
        if (PC->WasInputKeyJustPressed(EKeys::F5))
            SetBlindFlight(!bBlind);
    }
    if (bFlightTest)
    {
        MouseX = MouseY = Wheel = 0;
        WDown = false;
        const double Previous = TestTime;
        TestTime += DeltaSeconds;
        Launch = (Previous < 3 && TestTime >= 3) || (Previous < 98 && TestTime >= 98);
        Effort = TestTime >= 3 && TestTime < 12                          ? .72f
                 : (TestTime >= 15 && TestTime < 40) || (TestTime >= 98) ? 1.f
                                                                         : 0.f;
        Steer = TestTime >= 32 && TestTime < 36 ? .6f : 0;
        Roll = TestTime >= 36 && TestTime < 36.5 ? .5f : 0;
        Pitch = TestTime >= 26 && TestTime < 28 ? .75f : 0;
        if (TestTime >= 12.8 && TestTime < 13.2)
        {
            Steer = .25f;
            Roll = -.25f;
            Pitch = .15f;
        }
        Reset = TestTime >= 96 && !bTestResetSent;
        if (Reset)
            bTestResetSent = true;
        if (bSoakTest || bHandlingTest)
        {
            Effort = TestTime >= 3 ? .78f : 0;
            Steer = 0;
            Roll = 0;
            Pitch = 0;
            Reset = false;
            Launch = Previous < 3 && TestTime >= 3;
        }
        if (bHandlingTest && ((Previous < 15 && TestTime >= 15) || (Previous < 35 && TestTime >= 35)))
        {
            FRotator Attitude = Body->GetComponentRotation();
            Attitude.Roll = TestTime < 25 ? 20 : -20;
            Body->SetWorldRotation(Attitude, false, nullptr, ETeleportType::TeleportPhysics);
            Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
            UE_LOG(LogTemp, Display, TEXT("HandlingTest bank perturbation %.1f degrees; sticks neutral"), Attitude.Roll);
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
    const auto *Mode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    if (!Finite(P) || !Finite(V) || !Finite(Omega) || V.Size() > 40 || Omega.Size() > 12 ||
        P.Z < (Mode ? Mode->GroundHeight(P.X, P.Y) : 0) - 300 || P.Z > 300000 || P.Size2D() > 480000)
    {
        UE_LOG(LogTemp, Warning, TEXT("FlightSafetyReset pos=%s vel=%s omega=%s"), *P.ToString(), *V.ToString(),
               *Omega.ToString());
        ResetFlight(true);
        return;
    }
    if (Mode && Mode->IsWater(P.X, P.Y) &&
        P.Z < Mode->WaterHeight() + 10)
    {
        UE_LOG(LogTemp, Display, TEXT("FlightWaterLanding: returning to the clearing"));
        ResetFlight();
        return;
    }
    if (GetAltitude() < .3f && GetSpeed() < 1.f)
        bFlying = false;
    else if (GetAltitude() > .5f && GetSpeed() > 1.f)
        bFlying = true;
    UpdateLandingCamera(Dt);
    const double FieldRadius = Mode && Mode->IsNatureLevel() ? 190000. : 30000.;
    if (bFlying && !bReturning && P.Size2D() > FieldRadius)
    {
        bReturning = true;
        ++BoundaryReturns;
        UE_LOG(LogTemp, Display, TEXT("FlightFieldEdge count=%d"), BoundaryReturns);
    }
    if (P.Size2D() < FieldRadius * .6 || !bFlying)
        bReturning = false;
    // Throttle commands the motor on the ground too, like an armed RC model.
    // Flying state is telemetry, never a hidden override of transmitter input.
    Desktop.Step(DeltaSeconds, Effort, WDown, Roll, Pitch, Steer, MouseX, MouseY, Wheel, MuteMouseYaw, MuteMouseRoll, ResetMouse, MouseGains.X, MouseGains.Y, MouseGains.Z, ControlExpo);
    Throttle = Desktop.throttle;
    RollInput = Desktop.roll;
    PitchInput = Desktop.pitch;
    YawInput = Desktop.yaw;
    if (RcController && (RcController->IsEnabled() || RcController->IsPanelOpen()))
    {
        const auto &Channels = RcController->GetChannels();
        Throttle = Channels[0];
        RollInput = Channels[1];
        PitchInput = Channels[2];
        YawInput = Channels[3];
        Desktop.keyboard = {};
        Desktop.mouseRoll = Desktop.mousePitch = Desktop.mouseYaw = 0;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FAudioTest"))) Throttle=WorldTime<3 ? 0.f : .72f;
    if (StepMath(Dt))
    {
        // There is deliberately no target speed/altitude, lift offset, or
        // compensation of these forces. Gravity is applied by Chaos.
        Body->AddForce(AeroForce * 100.0);
        Body->AddTorqueInRadians(AeroMoment * 10000.0);
    }
    VisualRoot->SetRelativeRotation(FRotator::ZeroRotator);
    // Real flapping servos only flap; twisting the wings oppositely to command
    // roll is an optional visual effect (off by default, see bRollWingTwist).
    const float RollTwist = bRollWingTwist ? 18.f * RollInput : 0.f;
    if (RavenLeftShoulder) RavenLeftShoulder->SetRelativeRotation(FRotator(24 * PitchInput + RollTwist, 0, LeftFlap + 16));
    if (RavenRightShoulder) RavenRightShoulder->SetRelativeRotation(FRotator(24 * PitchInput - RollTwist, 0, -RightFlap - 16));
    // Match the solver's geometric dihedral and commanded wing incidence.
    LeftShoulder->SetRelativeRotation(FRotator(24 * PitchInput + RollTwist, 0, LeftFlap + 16));
    RightShoulder->SetRelativeRotation(FRotator(24 * PitchInput - RollTwist, 0, -RightFlap - 16));
    if(MembraneLeftShoulder) MembraneLeftShoulder->SetRelativeRotation(LeftShoulder->GetRelativeRotation());
    if(MembraneRightShoulder) MembraneRightShoulder->SetRelativeRotation(RightShoulder->GetRelativeRotation());
    B2F_WingSection LeftShape[B2F_WING_STATIONS], RightShape[B2F_WING_STATIONS];
    if(MathBridge && MathBridge->ReadWingShape(LeftShape,RightShape))
    {
        for(USceneComponent* Shoulder : {LeftShoulder.Get(),RightShoulder.Get(),RavenLeftShoulder.Get(),RavenRightShoulder.Get(),MembraneLeftShoulder.Get(),MembraneRightShoulder.Get()})
            if(Shoulder)
                for(USceneComponent* Child : Shoulder->GetAttachChildren())
                    if(auto* Wing=Cast<UBorn2FlapWingMesh>(Child))
                        Wing->ApplyShape((Shoulder==LeftShoulder || Shoulder==RavenLeftShoulder || Shoulder==MembraneLeftShoulder) ? LeftShape : RightShape,Body->GetComponentTransform());
    }
    UpdateAeroAudio(Dt);
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
                    "pitch=%.2f roll=%.2f yaw=%.1f throttle=%.3f soc=%.3f aeroN=%s torqueNm=%s rc=(%.3f,%.3f,%.3f) "
                    "flap=(%.1f,%.1f) throttleSource=%s wheelMemory=%.3f"),
               bFlying, bHealthy, GetAltitude(), GetSpeed(), GetClimbRate(), Body->GetComponentRotation().Pitch,
               Body->GetComponentRotation().Roll, Body->GetComponentRotation().Yaw, Throttle, BatterySoc,
               *AeroForce.ToString(), *AeroMoment.ToString(), RollInput, PitchInput, YawInput, LeftFlap, RightFlap,
               RcController && RcController->IsEnabled() ? TEXT("RC") :
               Desktop.wheelOwnsThrottle ? TEXT("wheel") : TEXT("keyboard"), Desktop.wheelThrottle);
    }
    if (bFlightTest)
        CheckFlightTest();
    if (bDesktopInputTest)
        CheckDesktopInputTest(DeltaSeconds);
}

void ABorn2FlapFlightPawn::CheckFlightTest()
{
    if (bHandlingTest)
    {
        const double Bank = FMath::Abs(Body->GetComponentRotation().Roll);
        if (TestTime >= 23 && TestTime < 25) TestLeftRecovery = FMath::Max(TestLeftRecovery, Bank);
        if (TestTime >= 43 && TestTime < 45) TestRightRecovery = FMath::Max(TestRightRecovery, Bank);
        if (TestTime >= 45 && !bTestFinished)
        {
            bTestFinished = true;
            const bool Pass = bHealthy && bFlying && GetAltitude() > 2 && MathFailures == 0 && SafetyResets == 0 &&
                              TestLeftRecovery < 12 && TestRightRecovery < 12;
            UE_LOG(LogTemp, Display, TEXT("HandlingTest %s recoveredBank=(%.2f,%.2f) altitude=%.2f speed=%.2f safety=%d mathFailures=%d"),
                   Pass ? TEXT("PASS") : TEXT("FAIL"), TestLeftRecovery, TestRightRecovery, GetAltitude(), GetSpeed(), SafetyResets, MathFailures);
            FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
        }
        return;
    }
    if (bRavenFlightTest)
    {
        if (TestTime > 2 && TestTime < 3)
            bTestIdle = bHealthy && !bFlying && GetAltitude() < .2 && GetSpeed() < .2;
        if (TestTime > 4)
        {
            TestFlapMin = FMath::Min(TestFlapMin, double(LeftFlap));
            TestFlapMax = FMath::Max(TestFlapMax, double(LeftFlap));
        }
        if (TestTime >= 10 && !bTestFinished)
        {
            bTestFinished = true;
            const auto* Mode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
            const bool Pass = Mode && Mode->IsNatureLevel() && bHealthy && bTestIdle && bFlying &&
                              GetAltitude() > 1 && GetSpeed() > 5 && TestFlapMax - TestFlapMin > 30 &&
                              SafetyResets == 0 && MathFailures == 0;
            UE_LOG(LogTemp, Display,
                   TEXT("RavenFlightTest %s healthy=%d idle=%d flying=%d altitude=%.2f speed=%.2f flapTravel=%.2f safety=%d mathFailures=%d"),
                   Pass ? TEXT("PASS") : TEXT("FAIL"), bHealthy, bTestIdle, bFlying, GetAltitude(), GetSpeed(),
                   TestFlapMax - TestFlapMin, SafetyResets, MathFailures);
            FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
        }
        return;
    }
    TestPeakSpeed = FMath::Max(TestPeakSpeed, double(GetSpeed()));
    TestPeakAltitude = FMath::Max(TestPeakAltitude, double(GetAltitude()));
    if (TestTime > 2 && TestTime < 3)
        bTestIdle = !bFlying && GetAltitude() < .2 && GetSpeed() < .2;
    if (TestTime > 11.8 && TestTime < 12)
        TestPoweredAltitude = GetAltitude();
    if (TestTime > 14.8 && TestTime < 15)
        TestGlideAltitude = GetAltitude();
    if (TestTime > 13 && TestTime < 14)
        TestGlideWingDiff = FMath::Max(TestGlideWingDiff, double(FMath::Abs(LeftFlap - RightFlap)));
    if (TestTime > 36.3 && TestTime < 37)
        TestRollWingDiff = FMath::Max(TestRollWingDiff, double(FMath::Abs(LeftFlap - RightFlap)));
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
    if (TestTime > 31.8 && TestTime < 32)
        TestBeforeTurnYaw = Body->GetComponentRotation().Yaw;
    if (TestTime >= 32 && TestTime < 38)
    {
        const double Yaw = Body->GetComponentRotation().Yaw;
        TestTurnTravel += FMath::FindDeltaAngleDegrees(TestBeforeTurnYaw,Yaw);
        TestBeforeTurnYaw = Yaw;
    }
    if (TestTime > 37 && TestTime < 38)
        bTestTurn = TestTurnTravel > 20;
    // Stronger powered climb needs a longer unpowered descent before landing.
    if (TestTime > 94 && TestTime < 95)
        bTestLand = !bFlying && GetAltitude() < .3 && GetSpeed() < .3;
    if (TestTime > 97 && TestTime < 98)
        bTestReset = !bFlying && GetActorLocation().Size2D() < 100 && GetAltitude() < .3;
    if (TestTime > 108 && TestTime < 109)
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
    if (!bSoakTest && TestTime >= 110 && !bTestFinished)
    {
        bTestFinished = true;
        const bool Pass = bHealthy && SafetyResets == 0 && MathFailures == 0 && bTestIdle && bTestTurn && bTestLand &&
                          bTestReset && bTestSecondFlight && TestPoweredAltitude > 2 &&
                          TestGlideAltitude < TestPoweredAltitude - .5 && TestBoostAltitude > TestGlideAltitude + 2 &&
                          TestFlapMax - TestFlapMin > 30 && TestPeakSpeed < 25 &&
                          TestPullSpeed < TestBeforePullSpeed - .3 && TestPullHeight > TestBeforePullHeight + .5 &&
                          TestGlideWingDiff > .5 && TestRollWingDiff > 3;
        UE_LOG(
            LogTemp, Display,
            TEXT("FlightTest %s seconds=%.2f idle=%d turn=%d land=%d reset=%d relaunch=%d poweredAlt=%.3f "
                 "coastAlt=%.3f boostAlt=%.3f flapTravel=%.2f peakSpeed=%.3f peakAltitude=%.3f safety=%d "
                 "mathFailures=%d pullSpeed=(%.2f,%.2f) pullHeight=(%.2f,%.2f) glideWingDiff=%.2f rollWingDiff=%.2f"),
            Pass ? TEXT("PASS") : TEXT("FAIL"), TestTime, bTestIdle, bTestTurn, bTestLand, bTestReset,
            bTestSecondFlight, TestPoweredAltitude, TestGlideAltitude, TestBoostAltitude, TestFlapMax - TestFlapMin,
            TestPeakSpeed, TestPeakAltitude, SafetyResets, MathFailures, TestBeforePullSpeed, TestPullSpeed,
            TestBeforePullHeight, TestPullHeight, TestGlideWingDiff, TestRollWingDiff);
        FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
    }
}