#include "Flight/Born2FlapFlightPawn.h"
#include "Flight/Born2FlapWingMesh.h"
#include "Avatar/Born2FlapAvatar.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Water/Born2FlapWaterDirector.h"
#include "Engine/StaticMesh.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UI/Born2FlapI18n.h"
#include "UI/Born2FlapLoading.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "UObject/ConstructorHelpers.h"
#include "Input/Born2FlapRcController.h"
#include "Input/Born2FlapKeybinds.h"
#include "Game/Born2FlapGameMode.h"
#include "World/Born2FlapValley.h"
#include "World/Born2FlapWind.h"
#include "World/Born2FlapWindLeaves.h"
#include "Kismet/GameplayStatics.h"
#include "Audio/Born2FlapAudioSynth.h"
#include "Audio/Born2FlapAeroAudio.h"
#include "Rendering/Born2FlapGraphics.h"
namespace
{
// Roll, hinges and elastic modes share one implicit solve. Pitch/yaw feed back
// through this host predictor; 240 Hz resolves their aerodynamic damping.
constexpr double MathDt = 1.0 / 240.0;
bool Finite(const FVector &V)
{
    return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
}
// Raw F8/Esc key capture for the flight-desk panel. While the panel is open the
// game runs in FInputModeGameAndUI, which routes keyboard input away from
// PlayerInput (so WasInputKeyJustPressed reports false for F8/Esc). A Slate
// input pre-processor sees every key-down before that routing, so the panel can
// still be toggled. The toggle is deferred to Tick, because changing the input
// mode during input processing would be unsafe.
class FPanelKeyInputProcessor : public IInputProcessor
{
public:
    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
    {
        if (InKeyEvent.IsRepeat()) return false;
        if (InKeyEvent.GetKey() == born2flap::keybinds::Get(TEXT("TogglePoi"))) bF8Pressed = true;
        else if (InKeyEvent.GetKey() == born2flap::keybinds::Get(TEXT("ClosePanel"))) bEscPressed = true;
        return false;
    }
    bool ConsumeF8()  { const bool v = bF8Pressed;  bF8Pressed  = false; return v; }
    bool ConsumeEsc() { const bool v = bEscPressed; bEscPressed = false; return v; }
private:
    bool bF8Pressed = false, bEscPressed = false;
};
} // namespace
ABorn2FlapFlightPawn::ABorn2FlapFlightPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    Body = CreateDefaultSubobject<UBoxComponent>(TEXT("FlightBody"));
    SetRootComponent(Body);
    Body->SetBoxExtent(FVector(21, 1.5, 1.5)); // narrow load-bearing fuselage, cm
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
    // pitch inertia.
    //
    // Dry locked-wing host inertia. MathCore receives the actual Chaos X
    // tensor each step and adds fluid inertia in its coupled roll/joint matrix.
    // This remains an approximate mass distribution; the collision box is not
    // a measured component-inertia model. Do not put added air into this scale
    // too, or it will be counted twice.
    Body->BodyInstance.InertiaTensorScale = FVector::OneVector;
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
    FpvCamera->SetFieldOfView(120); // wide FOV + barrel PP = fisheye FPV lens
    FpvCamera->SetAutoActivate(false);
    // Atmospheric parhelion: a huge unlit additive sphere that follows the
    // camera and renders halo/sun-dogs in WORLD space (depth-tested), so
    // terrain and clouds genuinely occlude it — not a screen-space overlay.
    SkyDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyParhelionDome"));
    SkyDome->SetupAttachment(Camera);
    SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SkyDome->SetCastShadow(false);
    // Wind motes live in WORLD space (absolute transform), anchored around the
    // camera each tick. Attaching to the moving body and marking the component
    // absolute keeps instance transforms == world transforms.
    WindMotes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WindMotes"));
    WindMotes->SetupAttachment(Body);
    WindMotes->SetAbsolute(true, true, true);
    WindMotes->SetMobility(EComponentMobility::Movable);
    WindMotes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WindMotes->SetCastShadow(false);
    // Wingtip water splash: a pool of expanding translucent rings on the water
    // surface plus a short Niagara mist burst. World-space, camera-independent.
    WaterRipples = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WaterRipples"));
    WaterRipples->SetupAttachment(Body);
    WaterRipples->SetAbsolute(true, true, true);
    WaterRipples->SetMobility(EComponentMobility::Movable);
    WaterRipples->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WaterRipples->SetCastShadow(false);
    WaterSplashFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("WaterSplashFX"));
    WaterSplashFX->SetupAttachment(Body);
    WaterSplashFX->SetAutoActivate(false);
    WaterSplashFX->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WaterSplashFX->SetCastShadow(false);
    // Negative X scale flips the sphere's winding so the camera (which sits at
    // the sphere's centre, i.e. INSIDE it) sees FRONT faces even if the
    // additive material's two-sided flag is not honoured in the translucency
    // pass. Unlit additive output is winding-agnostic, so this is safe.
    SkyDome->SetRelativeScale3D(FVector(-160, 160, 160)); // sphere r=50 -> 8000
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (SphereMesh.Succeeded())
    {
        SkyDome->SetStaticMesh(SphereMesh.Object);
    }
    AutoPossessPlayer = EAutoReceiveInput::Player0;
    // Default hangar tuning = the reference kestrel hardware (VOTIK PTK 7465
    // Plus flap servo, 3S 1.3 Ah) plus the simulator's controller base.
    // LoadFlightPreferences may override these before the first ApplyTuning.
    Tuning.servo_no_load_speed_deg_s = 857;
    Tuning.servo_stall_torque_nm = 0.735f;
    Tuning.servo_backdrive_deg_s_nm = 20;
    Tuning.battery_voltage = 7.4;
    Tuning.battery_resistance_ohm = 0.08;
    Tuning.battery_capacity_ah = 1.3;
    Tuning.battery_c_rating = 30;
    Tuning.battery_burst_c_rating = 60;
    Tuning.flap_base_freq_dhz = 32;
    Tuning.tail_elevator_angle_deg = -2.3;
    Tuning.glide_angle_deg = -4;
    Tuning.stroke_ferocity = 50;
    Tuning.aileron_scale = 40;
    Tuning.elevator_scale = 55;
    Tuning.flap_centre_deg = 0;
    Tuning.vertical_mount_angle_deg = 0;
    Tuning.return_ferocity = 50;
}
ABorn2FlapFlightPawn::~ABorn2FlapFlightPawn() = default;
namespace
{
float TuningClamp(ETuningField Field, float Value)
{
    switch (Field)
    {
    case ETuningField::ServoSpeed:        return FMath::Clamp(Value, 100.f, 2400.f);
    case ETuningField::StallTorque:       return FMath::Clamp(Value, 0.05f, 20.f);
    case ETuningField::Backdrive:         return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::BatteryVoltage:    return FMath::Clamp(Value, 3.7f, 22.2f);
    case ETuningField::BatteryResistance: return FMath::Clamp(Value, 0.01f, 0.5f);
    case ETuningField::BatteryCapacity:   return FMath::Clamp(Value, 0.1f, 5.f);
    case ETuningField::BatteryCRating:    return FMath::Clamp(Value, 1.f, 200.f);
    case ETuningField::BatteryBurstCRating: return FMath::Clamp(Value, 1.f, 400.f);
    case ETuningField::FlapBaseFreq:      return FMath::Clamp(Value, 10.f, 200.f);
    case ETuningField::TailElevatorAngle:    return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::GlideAngle:        return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::StrokeFerocity:    return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::AileronScale:      return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::ElevatorScale:     return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::FlapCentre:        return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::VerticalMountAngle: return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::ReturnFerocity:    return FMath::Clamp(Value, 0.f, 100.f);
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
    case ETuningField::BatteryCRating:    return float(Tuning.battery_c_rating);
    case ETuningField::BatteryBurstCRating: return float(Tuning.battery_burst_c_rating);
    case ETuningField::FlapBaseFreq:      return float(Tuning.flap_base_freq_dhz);
    case ETuningField::TailElevatorAngle:    return float(Tuning.tail_elevator_angle_deg);
    case ETuningField::GlideAngle:        return float(Tuning.glide_angle_deg);
    case ETuningField::StrokeFerocity:    return float(Tuning.stroke_ferocity);
    case ETuningField::AileronScale:      return float(Tuning.aileron_scale);
    case ETuningField::ElevatorScale:     return float(Tuning.elevator_scale);
    case ETuningField::FlapCentre:        return float(Tuning.flap_centre_deg);
    case ETuningField::VerticalMountAngle: return float(Tuning.vertical_mount_angle_deg);
    case ETuningField::ReturnFerocity:    return float(Tuning.return_ferocity);
    default: return 0.f;
    }
}
void ABorn2FlapFlightPawn::SetTuning(ETuningField Field, float Value)
{
    if (!FMath::IsFinite(Value)) return;
    Value = TuningClamp(Field, Value);
    switch (Field)
    {
    case ETuningField::ServoSpeed:        Tuning.servo_no_load_speed_deg_s = Value; break;
    case ETuningField::StallTorque:       Tuning.servo_stall_torque_nm = Value; break;
    case ETuningField::Backdrive:         Tuning.servo_backdrive_deg_s_nm = Value; break;
    case ETuningField::BatteryVoltage:    Tuning.battery_voltage = Value; break;
    case ETuningField::BatteryResistance: Tuning.battery_resistance_ohm = Value; break;
    case ETuningField::BatteryCapacity:   Tuning.battery_capacity_ah = Value; break;
    case ETuningField::BatteryCRating:    Tuning.battery_c_rating = Value; break;
    case ETuningField::BatteryBurstCRating: Tuning.battery_burst_c_rating = FMath::Max(Value, Tuning.battery_c_rating); break;
    case ETuningField::FlapBaseFreq:      Tuning.flap_base_freq_dhz = Value; break;
    case ETuningField::TailElevatorAngle:    Tuning.tail_elevator_angle_deg = Value; break;
    case ETuningField::GlideAngle:        Tuning.glide_angle_deg = Value; break;
    case ETuningField::StrokeFerocity:    Tuning.stroke_ferocity = Value; break;
    case ETuningField::AileronScale:      Tuning.aileron_scale = Value; break;
    case ETuningField::ElevatorScale:     Tuning.elevator_scale = Value; break;
    case ETuningField::FlapCentre:        Tuning.flap_centre_deg = Value; break;
    case ETuningField::VerticalMountAngle: Tuning.vertical_mount_angle_deg = Value; break;
    case ETuningField::ReturnFerocity:    Tuning.return_ferocity = Value; break;
    default: return;
    }
    ApplyTuning();
    SaveTuningPreference(Field);
}
void ABorn2FlapFlightPawn::ApplyTuning()
{
    // The airframe mass drives the physics wing scale (small bird → small wing,
    // so a micro servo's torque is enough to flap it).
    Tuning.body_mass_kg = BodyMassKg;
    if (MathBridge && MathBridge->IsReady())
    {
        MathBridge->Reconfigure(Tuning);
        B2F_MassProperties Props{};
        if (MathBridge->ReadMassProperties(-double(CgOffsetMm)/1000.0, Props))
        {
            const FVector Tensor(Props.dry_inertia_kg_m2[0], Props.dry_inertia_kg_m2[1], Props.dry_inertia_kg_m2[2]);
            DryRollInertiaKgM2 = Props.dry_inertia_kg_m2[0];
            MathBridge->SetMassFrame(-double(CgOffsetMm)/1000.0, Props.dry_inertia_kg_m2[1], Props.dry_inertia_kg_m2[2]);
            FPhysicsCommand::ExecuteWrite(Body->GetBodyInstance()->ActorHandle, [&](const FPhysicsActorHandle& Actor)
            {
                FPhysicsInterface::SetMassSpaceInertiaTensor_AssumesLocked(Actor, Tensor * 10000.0);
            });
            UE_LOG(LogTemp, Display, TEXT("FlightComponentMass total=%.4f fuselage=%.4f eachWing=%.4f tail=%.4f dryInertia=%s"),
                Props.total_mass_kg, Props.fuselage_mass_kg, Props.wing_mass_kg, Props.tail_mass_kg, *Tensor.ToString());
        }
    }
}
void ABorn2FlapFlightPawn::ApplyBodyMass()
{
    if (!Body)
        return;
    // Re-clamp to the current craft's CG travel (model-dependent) so a length
    // change on model switch never leaves a stale, out-of-range offset.
    CgOffsetMm = FMath::Clamp(CgOffsetMm, -GetCgRangeMm(), GetCgRangeMm());
    Body->SetMassOverrideInKg(NAME_None, BodyMassKg, true);
    // CG is millimetres; Chaos' centre-of-mass offset is centimetres. + = aft in
    // the UI, and the body's +X points forward, so aft is -X.
    Body->SetCenterOfMass(FVector(-CgOffsetMm * 0.1f, 0.f, 0.f));
    ApplyTuning();
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
    // Every field owns a silhouette: the common kestrel hunts the Shiomori coast,
    // the RavenCrow rules the nature valley, and the Prototype trains in the
    // indoor arena. Loading a map therefore swaps the bird automatically.
    const auto* GameMode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    if (GameMode && GameMode->IsDesertLevel())return 5;
    if (GameMode && GameMode->IsVoidLevel())
        return 3;                       // Neon Abyss -> the neon turtle
    if (GameMode && GameMode->IsCoastLevel())
        return 2;                       // Shiomori Bay -> common kestrel (falcon)
    if (GameMode && !GameMode->IsNatureLevel())
        return 1;                       // Training -> Prototype
    return 0;                           // Ravenstonefield / nature -> RavenCrow
}
void ABorn2FlapFlightPawn::BeginPlay()
{
    Super::BeginPlay();
    VectorOverlayHandle = UDebugDrawService::Register(TEXT("Game"),
        FDebugDrawDelegate::CreateUObject(this, &ABorn2FlapFlightPawn::DrawVectorOverlay));
    if (AudioSynth)
        AudioSynth->Start();
    PanelKeyProcessor = MakeShared<FPanelKeyInputProcessor>();
    if (FSlateApplication::IsInitialized())
        FSlateApplication::Get().RegisterInputPreProcessor(PanelKeyProcessor);
    ApplyBodyMass();
    // Neon Abyss is a weightless aether: no gravity, dense-medium drag. Neutral
    // buoyancy keeps the turtle suspended; drag makes flap-pulses coast instead
    // of gliding. Set here so ResetFlight teleports never re-enable gravity.
    if (const auto* GM = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
    {
        if (GM->IsVoidLevel())
        {
            Body->SetEnableGravity(false);
            Body->SetLinearDamping(1.6f);
            Body->SetAngularDamping(4.0f);
        }
    }
    UE_LOG(LogTemp, Display, TEXT("FlightBody massKg=%.4f inertiaKgM2=%s"), Body->GetMass(),
           *(Body->GetInertiaTensor() / 10000.0).ToString());
    // Weather owns sky optics and exposure for all cameras in the world.
    Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = false;
    Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = false;
    Camera->PostProcessSettings.bOverride_AutoExposureBias = false;
    if (SkyDome) SkyDome->SetVisibility(false);
    Camera->PostProcessSettings.AutoExposureBias = 0;
    MotionBlurAmount = Born2FlapGraphics::LoadMotionBlur();
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
    Camera->PostProcessSettings.MotionBlurAmount = MotionBlurAmount;
    GroundCamera->PostProcessSettings = Camera->PostProcessSettings;
    GroundCamera->PostProcessSettings.MotionBlurAmount = MotionBlurAmount * 1.2f;
    GroundCamera->PostProcessSettings.bOverride_MotionBlurMax = true;
    GroundCamera->PostProcessSettings.MotionBlurMax = 5.f;
    GroundCamera->PostProcessSettings.bOverride_MotionBlurTargetFPS = true;
    GroundCamera->PostProcessSettings.MotionBlurTargetFPS = 60;
    FpvCamera->PostProcessSettings = GroundCamera->PostProcessSettings;
    FpvCamera->PostProcessSettings.MotionBlurAmount = MotionBlurAmount * 0.4f;
    // Fisheye barrel-distortion lens for the onboard camera (created by the map
    // generator as /Game/UI/M_FpvFisheye). Absent asset -> fall back to a plain
    // perspective lens without breaking the FPV view.
    if (UMaterialInterface* Fisheye = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/UI/M_FpvFisheye")))
        FpvCamera->PostProcessSettings.AddBlendable(Fisheye, 1.0f);
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
    FParse::Value(FCommandLine::Get(), TEXT("B2FVectorView="), VectorView);
    VectorView = FMath::Clamp(VectorView, 0, 2);
    bVectors = VectorView != 0;
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
    // Kestrel wing graphics: the authored membrane from the Inkscape SVG, applied
    // to the membrane-wing model (Design 2). The greyed underside is done in the
    // material via TwoSidedSign, so only the top texture is needed here.
    if (auto* Membrane = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Birds/T_KestrelWingTop")))
    {
        TInlineComponentArray<UBorn2FlapWingMesh*> MembraneWings(this);
        for (auto* Wing : MembraneWings)
            if (Wing->GetWingDesign() == 2)
                Wing->SetPaintTexture(Membrane);
    }
    LoadFlightPreferences();
    if (!bFlightTest && !bDesktopInputTest)
        SpawnAvatar();
    if(const auto* Mode=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());Mode && Mode->IsHarvestWatch())
        BirdModel=2;
    if (BirdModel < 0)
        BirdModel = DefaultBirdModel();   // no explicit menu choice yet → level default
    if (bFlightTest)
    {
        int32 TestBirdModel = BirdModel;
        if (FParse::Value(FCommandLine::Get(), TEXT("B2FTestBirdModel="), TestBirdModel))
            BirdModel = FMath::Clamp(TestBirdModel, 0, 4);
    }
    SelectBirdModel(BirdModel);   // applies the loaded mass + CG for this model
    // Sparse wind motes: a few tiny specks advected by the atmospheric field so
    // the invisible current becomes perceivable. World-space instanced spheres,
    // kept as a loose cloud around the camera and recycled as they drift out.
    if (WindMotes)
    {
        if (UStaticMesh* MoteSphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
        {
            WindMotes->SetStaticMesh(MoteSphere);
            if (UMaterialInterface* MoteMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Weather/M_Snow")))
            {
                WindMotes->SetMaterial(0, MoteMat);
                constexpr int32 MoteCount = 28;
                MotePositions.SetNum(MoteCount);
                MoteScales.SetNum(MoteCount);
                MoteSpeed.SetNum(MoteCount);
                const FVector Cam = Camera->GetComponentLocation();
                const FVector Fwd = Camera->GetForwardVector();
                for (int32 I = 0; I < MoteCount; ++I)
                {
                    MotePositions[I] = Cam + Fwd * MoteRandom.FRandRange(150.f, 1800.f) +
                        FVector(MoteRandom.FRandRange(-2000.f, 2000.f), MoteRandom.FRandRange(-2000.f, 2000.f),
                                MoteRandom.FRandRange(-400.f, 900.f));
                    MoteScales[I] = MoteRandom.FRandRange(0.012f, 0.028f);
                    MoteSpeed[I] = MoteRandom.FRandRange(0.55f, 1.4f);
                    WindMotes->AddInstance(FTransform(FQuat::Identity, MotePositions[I], FVector(MoteScales[I])));
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("WindMotes: /Game/Weather/M_Snow not found — wind motes disabled"));
            }
        }
    }
    // Wingtip water splash pool: M_RainSplash ring on a world-space plane pool,
    // recycled round-robin as ripples expand and fade.
    if (WaterRipples)
    {
        if (UStaticMesh* RingPlane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
        {
            WaterRipples->SetStaticMesh(RingPlane);
            if (UMaterialInterface* RingMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Weather/M_RainSplash")))
            {
                WaterRipples->SetMaterial(0, RingMat);
                constexpr int32 RingCount = 32;
                WaterRipplePos.SetNum(RingCount);
                WaterRippleAge.Init(1.f, RingCount);
                for (int32 I = 0; I < RingCount; ++I)
                    WaterRipples->AddInstance(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector));
            }
            else
            {
                WaterRipples->SetVisibility(false);
            }
        }
    }
    if (WaterSplashFX)
    {
        if (auto* System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Weather/NS_SurfSpray")))
            WaterSplashFX->SetAsset(System);
        else
            WaterSplashFX->SetVisibility(false);
    }
    // Points of interest: the GameMode populated them during its own BeginPlay
    // (before the pawn spawned). Fall back to the origin if none arrived.
    if (auto* Mode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
    {
        POIs = Mode->GetPOIs();
        SelectedPoi = FMath::Clamp(Mode->GetInitialPoiIndex(), 0, FMath::Max(0, POIs.Num() - 1));
    }
    if (POIs.IsEmpty())
    {
        FBorn2FlapPoi Fallback;
        Fallback.Key = TEXT("poi.start_line");
        Fallback.Position = FVector(0, 0, 80);
        POIs.Add(Fallback);
    }
    ResetFlight();
    if (bDesktopInputTest && FParse::Param(FCommandLine::Get(), TEXT("B2FBirdPreview")))
    {
        CameraBoom->TargetArmLength = 360;
        CameraBoom->SetRelativeLocation(FVector(0,0,24));
        CameraBoom->bInheritYaw = false;
        float PreviewPitch=-52.f, PreviewYaw=135.f;
        FParse::Value(FCommandLine::Get(), TEXT("B2FBirdPreviewPitch="), PreviewPitch);
        FParse::Value(FCommandLine::Get(), TEXT("B2FBirdPreviewYaw="), PreviewYaw);
        CameraBoom->SetRelativeRotation(FRotator(PreviewPitch,PreviewYaw,0));
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

void ABorn2FlapFlightPawn::SetMotionBlurAmount(float Amount)
{
    if (!FMath::IsFinite(Amount)) return;
    MotionBlurAmount = FMath::Clamp(Amount, 0.f, 1.f);
    if (Camera)
    {
        Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
        Camera->PostProcessSettings.MotionBlurAmount = MotionBlurAmount;
    }
    if (GroundCamera)
    {
        GroundCamera->PostProcessSettings.bOverride_MotionBlurAmount = true;
        GroundCamera->PostProcessSettings.MotionBlurAmount = MotionBlurAmount * 1.2f;
    }
    if (FpvCamera)
    {
        FpvCamera->PostProcessSettings.bOverride_MotionBlurAmount = true;
        FpvCamera->PostProcessSettings.MotionBlurAmount = MotionBlurAmount * 0.4f;
    }
}

void ABorn2FlapFlightPawn::ResetFlight(bool bSafety)
{
    if (bSafety)
        ++SafetyResets;
    // Recreate all firmware/servo/aeroelastic/battery/history state. RTS stays pinned.
    // Surface the backend load on the loading screen (during a level load the
    // loader is active) so the flight-core phase reads there instead of as a
    // stuck "core stopped" status in the HUD once the loader has dropped.
    if (Born2Flap::Loading::State().IsActive())
        Born2Flap::Loading::Report(0.70f, TEXT("LOADING FLIGHT CORE"));
    bHaveMathRotation = false;
    bHealthy = MathBridge && MathBridge->Load();
    if (Born2Flap::Loading::State().IsActive())
        Born2Flap::Loading::Report(0.80f, TEXT("FLIGHT CORE READY"));
    if (bHealthy)
    {
        // A fresh context starts on the kestrel reference; select the bird's
        // own wing planform + carbon-rod inventory before the mass properties
        // and first wing loads are read.
        MathBridge->SetWingModel((uint32_t)PhysicsBirdModel());
        ApplyTuning();
    }
    bFlying = bReturning = false;
    const double SavedSpeedModifier = Desktop.speedModifier;
    Desktop = {};
    Desktop.speedModifier = SavedSpeedModifier;
    Throttle = RollInput = YawInput = PitchInput = LeftFlap = RightFlap = 0;
    bStripStatsValid = false;
    BatterySoc = 1;
    Accumulator = 0;
    AeroForce = AeroMoment = FVector::ZeroVector;
    // Re-place at the currently selected POI (launch meadow by default) rather
    // than the fixed world origin.
    FVector Spawn = FVector(0, 0, 80);
    FRotator SpawnRot = FRotator::ZeroRotator;
    if (POIs.IsValidIndex(SelectedPoi))
    {
        Spawn = POIs[SelectedPoi].Position;
        SpawnRot = FRotator(0, POIs[SelectedPoi].Yaw, 0);
    }
    Body->SetWorldLocationAndRotation(Spawn, SpawnRot, false, nullptr,
                                      ETeleportType::TeleportPhysics);
    Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    Body->WakeAllRigidBodies();
    bCameraGrounded = false;
    bThrown = false;
    GroundSettleTime = 0;
    // Neon Abyss has no ground to launch from: the turtle starts swimming the
    // moment it spawns — throttle pulses it forward, buoyancy holds it up.
    if (const auto* GM = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
        if (GM->IsVoidLevel())
            bFlying = true;
    ChaseOrbitYaw = 0.f; ChaseOrbitPitch = -12.f;
    GroundLookYaw = 0.f; GroundLookPitch = 0.f;
    GroundZoom = 1.f; ChaseArmLength = 480.f;
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
void ABorn2FlapFlightPawn::EmitSandBurst(const FVector& WorldPos, float Strength)
{
    if (!WindLeavesActor.IsValid())
        WindLeavesActor = Cast<ABorn2FlapWindLeaves>(
            UGameplayStatics::GetActorOfClass(GetWorld(), ABorn2FlapWindLeaves::StaticClass()));
    if (ABorn2FlapWindLeaves* WL = WindLeavesActor.Get())
        WL->EmitSandBurst(WorldPos, Strength);
}
void ABorn2FlapFlightPawn::EmitWaterSplash(const FVector& WorldPos, float Strength)
{
    // Ripple ring: recycle the oldest slot so a rapid wing-tip skim leaves a
    // train of expanding circles rather than a single frozen ring.
    if (WaterRipples && WaterRipplePos.Num() > 0)
    {
        WaterRippleCursor = (WaterRippleCursor + 1) % WaterRipplePos.Num();
        WaterRipplePos[WaterRippleCursor] = WorldPos;
        WaterRippleAge[WaterRippleCursor] = 0.f;
        WaterRipples->UpdateInstanceTransform(WaterRippleCursor,
            FTransform(FQuat::Identity, WorldPos, FVector(0.05f, 0.05f, 1.f)), true, false, false);
        WaterRipples->MarkRenderStateDirty();
    }
    // Mist burst: brief NS_SurfSpray activation at the contact point; the
    // countdown in UpdateWaterRipples deactivates it once the burst is thrown.
    if (WaterSplashFX && WaterSplashFX->GetAsset())
    {
        WaterSplashFX->SetWorldLocation(WorldPos);
        WaterSplashFX->Activate(true);
        WaterSplashTimeLeft = 0.4f;
    }
}
void ABorn2FlapFlightPawn::UpdateWaterRipples(float DeltaSeconds)
{
    if (WaterSplashFX && WaterSplashFX->IsActive())
    {
        WaterSplashTimeLeft -= DeltaSeconds;
        if (WaterSplashTimeLeft <= 0.f)
            WaterSplashFX->Deactivate();
    }
    if (!WaterRipples || WaterRipplePos.Num() == 0)
        return;
    for (int32 I = 0; I < WaterRipplePos.Num(); ++I)
    {
        if (WaterRippleAge[I] >= 1.f)
            continue; // parked ring: zero scale, invisible
        WaterRippleAge[I] = FMath::Min(1.f, WaterRippleAge[I] + DeltaSeconds / 0.9f);
        const float Age = WaterRippleAge[I];
        // Grow from a point, then shrink back out — an expanding, fading circle.
        const float Scale = (0.05f + 1.1f * Age) * (1.f - Age);
        WaterRipples->UpdateInstanceTransform(I,
            FTransform(FQuat::Identity, WaterRipplePos[I], FVector(Scale, Scale, 1.f)), true, false, false);
    }
    WaterRipples->MarkRenderStateDirty();
}
void ABorn2FlapFlightPawn::SweepWingColliders(float Dt)
{
    // Compute a leading-edge contact capsule for every VISIBLE wing each frame,
    // directly in world space from the wing component's own (already-flapped)
    // transform — no child collision components. The procedural wings are added
    // via AddInstanceComponent, so GetComponents does not reliably enumerate
    // them; the shoulder-child walk below is the same path Tick already uses to
    // apply the wing shape, and therefore always finds them. Detection uses a
    // blocking overlap test because overlap EVENTS never fire against BlockAll
    // world geometry (ground/rocks respond Block, not Overlap); a second sweep
    // catches tunnelling through thin objects during a fast flap or dive. Both
    // stamp LastWingTouchTime so the race gates can require a clean pass.
    UWorld* World = GetWorld();
    if (!World) return;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    TArray<UBorn2FlapWingMesh*> Wings;
    for (USceneComponent* Shoulder : { LeftShoulder.Get(), RightShoulder.Get(),
            RavenLeftShoulder.Get(), RavenRightShoulder.Get(),
            MembraneLeftShoulder.Get(), MembraneRightShoulder.Get(),
            TurtleLeftShoulder.Get(), TurtleRightShoulder.Get() })
    {
        if (!Shoulder) continue;
        for (USceneComponent* Child : Shoulder->GetAttachChildren())
            if (auto* Wing = Cast<UBorn2FlapWingMesh>(Child))
                if (Wing->IsVisible())
                    Wings.Add(Wing);
    }

    bool bTouched = false;
    FVector ContactPoint = FVector::ZeroVector;
    FVector ContactNormal = FVector::UpVector;
    for (UBorn2FlapWingMesh* Wing : Wings)
    {
        const FTransform T = Wing->GetComponentTransform();
        const FVector Root = T.TransformPosition(Wing->GetWingRootLocal());
        const FVector Tip  = T.TransformPosition(Wing->GetWingTipLocal());
        const FVector Dir  = Tip - Root;
        const float   Len  = Dir.Size();
        if (Len < 1.f) continue;
        // Wingtip water contact: the ocean surface is a translucent material (no
        // collision channel), so detect the crossing by comparing the tip against
        // the animated Gerstner height from the water director.
        if (const AShiomoriWaterDirector* Water = AShiomoriWaterDirector::Get(World))
        {
            const float WaterZ = Water->GetWaterHeightAt(Tip.X, Tip.Y);
            const float Depth  = WaterZ - Tip.Z; // > 0 => tip submerged
            float& Prev = WingWaterDepthPrev.FindOrAdd(Wing);
            if (Depth > 2.f && Prev <= 2.f)
                EmitWaterSplash(FVector(Tip.X, Tip.Y, WaterZ + 2.f), 1.f);
            Prev = Depth;
        }
        const FVector Mid = (Root + Tip) * 0.5f;
        const FQuat   Rot = FRotationMatrix::MakeFromZ(Dir / Len).ToQuat();
        const FCollisionShape Shape = FCollisionShape::MakeCapsule(6.f, Len * 0.5f);

        if (World->OverlapBlockingTestByChannel(Mid, Rot, ECC_WorldStatic, Shape, Params))
        {
            bTouched = true;
            LastWingTouchTime = World->GetTimeSeconds();
            ContactPoint = Tip;
            ContactNormal = FVector::UpVector;
            UE_LOG(LogTemp, Display, TEXT("WingTouch ground-contact at %s"), *Mid.ToString());
        }

        const FVector* PrevMid = WingSweepPrev.Find(Wing);
        if (PrevMid && !PrevMid->Equals(Mid, 0.01f))
        {
            FHitResult Hit;
            if (World->SweepSingleByChannel(Hit, *PrevMid, Mid, Rot, ECC_WorldStatic, Shape, Params))
            {
                bTouched = true;
                LastWingTouchTime = World->GetTimeSeconds();
                ContactPoint = Hit.ImpactPoint;
                ContactNormal = Hit.ImpactNormal;
                UE_LOG(LogTemp, Display, TEXT("WingSweepTouch other=%s component=%s point=%s"),
                       *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToString());
            }
        }
        WingSweepPrev.FindOrAdd(Wing) = Mid;
    }

        // Wingtip ground contact is a soft, energy-dissipating plant. The aeroelastic
        // wing flexes and drags — it does not spring back or lever the bird upward.
        // Both responses are applied AT the contact point, so the impulse levers the
        // body around it (the "correct lever arm" that turns forward momentum into
        // pitch and drag instead of a bounce):
        // (1) Normal: fully cancel inward velocity (restitution 0) so the tip settles
        //     onto the surface instead of rebounding.
        // (2) Tangential: ground friction bleeds off forward/sideways energy while the
        //     wing scrapes, SLOWING the bird — the lever-arm brake that replaces the
        //     old downstroke "catapult". No spar-bend model is needed: the rigid lever
        //     between contact point and CoM already produces the pitch/drag.
        if (bTouched && !bWingTouching)
        {
            if (GEngine)
                GEngine->AddOnScreenDebugMessage(42, 0.6f, FColor(255, 92, 64), TEXT("WING HIT"));
            EmitSandBurst(ContactPoint, 0.7f);
        }
        if (bTouched)
        {
            // (1) Plant: SOFT aeroelastic damping of the inward (normal) velocity.
            // The old hard-kill (restitution 0, full cancel up to 40 cm/s) was applied
            // AT the tip, and a grounded bird's tip inward velocity is dominated by
            // body ROLL (ω×r at the ~half-span lever arm) — fully cancelling it every
            // frame reversed the roll and violently threw the opposite wing down.
            // A pure exponential dashpot removes only a fraction (< 1) per frame, so
            // it bleeds approach (and roll) energy out gently, with no bounce and no
            // overshoot, exactly like the flexible aeroelastic wing actually behaves.
            const FVector ContactVel = Body->GetPhysicsLinearVelocityAtPoint(ContactPoint);
            const float   Approach   = FVector::DotProduct(ContactVel, ContactNormal); // - = into surface
            if (Approach < 0.f)
            {
                const float k    = 6.0f;   // soft plant rate (1/s)
                const float Kill = FMath::Min(-Approach * (1.f - FMath::Exp(-k * Dt)), 15.f);
                Body->AddImpulseAtLocation(ContactNormal * (Body->GetMass() * Kill), ContactPoint);
            }
    
            // (2) Friction: while the wing drags on the ground, drain tangential
            // energy — the lever-arm brake that SLOWS the bird instead of catapulting
            // it. Framerate-independent exponential bleed (k ≈ 2/s), capped per frame
            // so a fast scrape reads as drag, not a snap.
            const FVector Tangential = ContactVel - ContactNormal * Approach;
            const float   TangSpeed  = Tangential.Size();
            if (TangSpeed > 1.f)
            {
                const float k     = 2.0f;
                const float Brake = FMath::Min(TangSpeed * (1.f - FMath::Exp(-k * Dt)), 30.f);
                const FVector BrakeDir = Tangential / TangSpeed;
                Body->AddImpulseAtLocation(-BrakeDir * (Body->GetMass() * Brake), ContactPoint);
            }
        }
        bWingTouching = bTouched;
}
void ABorn2FlapFlightPawn::RequestHandLaunch()
{
    // With a visible avatar present, play the throw animation and release the
    // bird at the natural release moment (30% of the throw), instead of an
    // instantaneous teleport. OnThrowRelease is bound to LaunchFlight.
    if (Avatar && bCameraGrounded && !Avatar->IsThrowing())
    {
        Avatar->BeginThrow();
        return;
    }
    LaunchFlight();
}
void ABorn2FlapFlightPawn::LaunchFlight()
{
    if (!bHealthy || (!bCameraGrounded && GetAltitude() > .4f) || GetSpeed() > 1.f)
        return;
    // One explicit hand launch supplies initial momentum; it cannot repeat in
    // the air. Every subsequent acceleration comes from aero forces/gravity.
    // If the pilot walked away from the bird (ground walk mode), the throw
    // starts where the pilot stands and heads the way the pilot is looking;
    // otherwise the bird launches from its resting spot and facing.
    const bool bFromPilot = bPilotWalked;
    float LaunchYaw = Body->GetComponentRotation().Yaw;
    float LaunchPitch = 0.f;
    FVector LaunchPos = Body->GetComponentLocation() + FVector(0, 0, 140);
    if (bFromPilot)
    {
        LaunchYaw = bGroundView
                        ? float(GroundGaze.Yaw + GroundLookYaw)
                        : float(Body->GetComponentRotation().Yaw + ChaseOrbitYaw);
        // The pilot's vertical gaze has a small influence on the throw's loft:
        // looking up throws a little higher, looking down flattens it.
        LaunchPitch = bGroundView ? float(GroundLookPitch) : float(ChaseOrbitPitch);
        LaunchPos = GroundAnchor;   // the pilot is already at eye height
    }
    FRotator Heading(0, LaunchYaw, 0);
    ChaseOrbitYaw = 0.f; ChaseOrbitPitch = -12.f;
    GroundLookYaw = 0.f; GroundLookPitch = 0.f;
    GroundZoom = 1.f; ChaseArmLength = 480.f;
    Body->SetWorldLocationAndRotation(LaunchPos, Heading, false, nullptr,
                                      ETeleportType::TeleportPhysics);
    // Vertical loft: base 2.2 m/s upward plus a small vertical-gaze contribution.
    const float LaunchVertical = 220.f + FMath::Clamp(LaunchPitch, -45.f, 45.f) * 6.f;
    Body->SetPhysicsLinearVelocity(Heading.Vector() * 850 + FVector(0, 0, LaunchVertical));
    Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    // Fresh aeroelastic/servo/battery history for the throw: the resting bird
    // equilibrates its strips nose-down on the ground (pitch ≈ −85°), and
    // those stale camber/bend/twist states pitched the launch straight into
    // the ground. The pinned RTS stays; only the firmware context is recreated.
    if (MathBridge)
    {
        bHaveMathRotation = false;
        bHealthy = MathBridge->Load();
        if (bHealthy)
        {
            MathBridge->SetWingModel((uint32_t)PhysicsBirdModel());
            ApplyTuning();
        }
    }
    bCameraGrounded = false;
    bThrown = true;   // awaiting a genuine landing for the observer teleport
    GroundSettleTime = 0;
    bFlying = true;
    // Hard-reset the virtual mouse stick at the moment of the throw so a residual
    // look offset never yanks the bird off its launch heading (hard, immediate).
    Desktop.mouseRoll = Desktop.mousePitch = Desktop.mouseYaw = 0;
    RememberLanding(Body->GetComponentLocation());
    UE_LOG(LogTemp, Display, TEXT("FlightHandLaunch fromPilot=%d speed=8.5m/s vertical=%.1fm/s pitch=%.0f"), bFromPilot, LaunchVertical / 100.f, LaunchPitch);
}
void ABorn2FlapFlightPawn::SpawnAvatar()
{
    if (!bAvatarEnabled)
        return;
    FActorSpawnParameters Params;
    Params.Owner = this;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Avatar = GetWorld()->SpawnActor<ABorn2FlapAvatar>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
    if (!Avatar)
        return;
    Avatar->SetPerson(AvatarPerson);
    Avatar->OnThrowRelease.BindUObject(this, &ABorn2FlapFlightPawn::LaunchFlight);
    PrevAvatarFeet = GroundAnchor - FVector(0.f, 0.f, 180.f);
    Avatar->SetPlacement(PrevAvatarFeet, 0.f);
}
void ABorn2FlapFlightPawn::SetAvatarEnabled(bool bEnabled)
{
    bAvatarEnabled = bEnabled;
    if (!bEnabled)
    {
        // Hide/destroy the on-foot pilot immediately when disabled from the menu.
        if (Avatar)
        {
            Avatar->Destroy();
            Avatar = nullptr;
        }
        return;
    }
    // (Re)spawn only when already in a live world (menu opened in-level);
    // otherwise the regular BeginPlay spawn picks the flag up.
    if (!Avatar && HasActorBegunPlay() && !bFlightTest && !bDesktopInputTest)
        SpawnAvatar();
}
void ABorn2FlapFlightPawn::SetAvatarPerson(int32 Index)
{
    AvatarPerson = FMath::Clamp(Index, 0, ABorn2FlapAvatar::GetPersonCount() - 1);
    if (Avatar)
        Avatar->SetPerson(AvatarPerson);
    // Persistence is handled by SaveFlightPreferences() on settings close,
    // matching the other live setters (model, camera, mass, …).
}
void ABorn2FlapFlightPawn::UpdateAvatarState(float Dt)
{
    if (!Avatar)
        return;
    // The pilot's feet sit one eye-height below the ground camera anchor.
    const FVector Feet = GroundAnchor - FVector(0.f, 0.f, 180.f);
    const float Speed = Dt > 0.f ? (Feet - PrevAvatarFeet).Size2D() / Dt : 0.f;
    PrevAvatarFeet = Feet;
    Avatar->SetWalkSpeed(Speed);
    Avatar->SetPlacement(Feet, float(GroundGaze.Yaw + GroundLookYaw));
    // First-person ground view = the pilot IS the avatar: hide the own body.
    Avatar->SetFirstPerson(bGroundView);
}
FString ABorn2FlapFlightPawn::GetFlightStatus() const
{
    if (!bHealthy)
        return MathBridge && !MathBridge->IsReady() ? MathBridge->GetStatus()
                                                  : Born2Flap::I18n::T("status.core_stopped");
    if (bReturning)
        return Born2Flap::I18n::T("status.field_edge");
    if (!bFlying)
        return Born2Flap::I18n::T("status.hand_launch");
    if (GetSpeed() < 4.5f)
        return Born2Flap::I18n::T("status.low_airspeed");
    if (Throttle < .08f)
        return Born2Flap::I18n::T("status.gliding");
    return Throttle > .85f ? Born2Flap::I18n::T("status.power_strokes") : Born2Flap::I18n::T("status.flapping");
}

bool ABorn2FlapFlightPawn::StepMath(float DeltaSeconds)
{
    const double ProfileStart = FPlatformTime::Seconds();
    if (!bHealthy)
        return false;
    FQuat Rotation = Body->GetComponentQuat();
    if (bHaveMathRotation)
    {
        FQuat Correction = (Rotation.Inverse() * LastMathRotation).GetNormalized();
        if (Correction.W < 0) Correction = Correction * -1.0;
        FVector Axis;
        double Angle;
        Correction.ToAxisAndAngle(Axis, Angle);
        if (!MathBridge->TransportAirfields(Axis * Angle))
        {
            ++MathFailures;
            bHealthy = false;
            return false;
        }
    }
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
    CurrentWind = bFlightTest ? FVector::ZeroVector : (WindMode?WindMode->WindAt(BodyPos,WorldTime):Born2FlapWind::Sample(BodyPos,WorldTime));
    const FVector Wind = CurrentWind * Shelter;
    // Sample the atmospheric field at each deformed force application point.
    // The body input already subtracts origin wind; only the spatial increment
    // goes through the strip extension. Re-express it for every solver step.
    FVector StripWindWorld[2][B2F_WING_STATIONS]{};
    B2F_WingField WindFields[2][B2F_WING_STATIONS]{};
    if (!bFlightTest && MathBridge->ReadWingFields(WindFields[0], WindFields[1]))
    {
        for (int Side = 0; Side < 2; ++Side)
            for (uint32 I = 0; I < B2F_WING_STATIONS; ++I)
            {
                const double* P = WindFields[Side][I].position_m;
                const FVector Position = BodyPos + Rotation.RotateVector(FVector(P[0], P[1], P[2])) * 100.0;
                const FVector LocalWind = WindMode ? WindMode->WindAt(Position, WorldTime) : Born2FlapWind::Sample(Position, WorldTime);
                StripWindWorld[Side][I] = (LocalWind - CurrentWind) * Shelter;
            }
    }
    MathBridge->InjectWindPhaseNoise(bFlightTest ? 0.0 : FMath::Clamp(CurrentWind.Size()*.15+FMath::Abs(CurrentWind.Z)*.25,0.,4.)*Shelter);
    B2F_PilotInput Pilot{};
    Pilot.throttle = Throttle;
    Pilot.roll = RollInput;
    Pilot.pitch = PitchInput;
    Pilot.yaw = YawInput;
    Pilot.speed_mod = Desktop.speedModifier;
    Pilot.throttle_mode = bCoupledThrottle ? 1.0 : 0.0;
    B2F_BodyState State{};
    State.delta_time_s = MathDt;
    State.roll_inertia_kg_m2 = Inertia.X > 0.0 ? Inertia.X : DryRollInertiaKgM2;
    // Ground effect: body-origin height above the surface below (terrain +
    // rocks/buildings via GroundHeight), in metres. The math core normalises
    // this by wingspan and applies per-strip lift/drag/thrust corrections.
    State.ground_clearance_m = FMath::Max(0.0,
        WindMode ? (BodyPos.Z - WindMode->GroundHeight(BodyPos.X, BodyPos.Y)) / 100.0 : 1000.0);
    // Share Chaos's simulated time. At 12 fps the old solver advanced wings
    // ~83 ms while the rigid body advanced only 33 ms: the reaction impulse
    // was lost and the bird appeared to move its wings around a pinned body.
    const UPhysicsSettings* PhysicsSettings = UPhysicsSettings::Get();
    const double MaxPhysicsDt = PhysicsSettings->bSubstepping
        ? PhysicsSettings->MaxSubstepDeltaTime * PhysicsSettings->MaxSubsteps
        : PhysicsSettings->MaxPhysicsDeltaTime;
    const double PhysicsDt = MaxPhysicsDt > UE_SMALL_NUMBER
        ? FMath::Min(double(DeltaSeconds), MaxPhysicsDt) : double(DeltaSeconds);
    // Consume exactly this frame's physical time, including fractional steps.
    // Reusing the last force when no fixed step ran applied an impulse without
    // advancing the hinges; averaging a different duration did the converse.
    double TargetMathDt = MathDt;
    double TestHz = 240;
    if (bFlightTest && FParse::Value(FCommandLine::Get(), TEXT("B2FPhysicsHz="), TestHz) &&
        FMath::IsFinite(TestHz) && TestHz >= 240 && TestHz <= 3840)
        TargetMathDt = 1.0 / TestHz;
    const int32 StepCount = FMath::Max(1, FMath::CeilToInt(PhysicsDt / TargetMathDt - 1.e-6));
    const double StepDt = PhysicsDt / StepCount;
    if (StepDt <= 0) return false;
    State.delta_time_s = StepDt;
    FVector ForceSum = FVector::ZeroVector, MomentSum = FVector::ZeroVector;
    int32 Steps = 0;
    const bool bVoid = WindMode && WindMode->IsVoidLevel();
    const FVector Gravity = bVoid ? FVector::ZeroVector : FVector(0, 0, -9.81);
    const double CgM = double(CgOffsetMm) / 1000.0;
    const FVector CgBody(-CgM, 0, 0);
    while (Steps < StepCount)
    {
        double StripWindBody[2][B2F_WING_STATIONS * 3]{};
        for (int Side = 0; Side < 2; ++Side)
            for (uint32 I = 0; I < B2F_WING_STATIONS; ++I)
            {
                const FVector Local = Rotation.UnrotateVector(StripWindWorld[Side][I]);
                for (int Axis = 0; Axis < 3; ++Axis)
                    StripWindBody[Side][I * 3 + Axis] = Local[Axis];
            }
        if (!MathBridge->SetStripWind(StripWindBody[0], StripWindBody[1]))
        {
            ++MathFailures;
            bHealthy = bFlying = false;
            AeroForce = AeroMoment = FVector::ZeroVector;
            return false;
        }
        const FVector W = Rotation.UnrotateVector(Omega);
        // Chaos reports COM velocity; the strip positions use the body origin.
        // v_origin = v_COM - omega cross r_COM, so subsequent omega cross
        // position evaluates each surface relative to the actual CG.
        const FVector V = Rotation.UnrotateVector(Velocity - Wind) - FVector::CrossProduct(W, CgBody);
        for (int I = 0; I < 3; ++I)
        {
            State.linear_velocity_m_s[I] = V[I];
            State.angular_velocity_rad_s[I] = W[I];
        }
        // Gravity/ground reference only. Strip force directions come from
        // their deformed geometry, airflow and convected pressure history.
        // A zero reference disables gravity coupling in the gravity-free Void.
        const FVector UpB = Rotation.UnrotateVector(bVoid ? FVector::ZeroVector : FVector(0.0, 0.0, 1.0));
        State.body_up_body_frame[0] = UpB.X;
        State.body_up_body_frame[1] = UpB.Y;
        State.body_up_body_frame[2] = UpB.Z;
        B2F_FirmwareOutput O{};
        bool Valid = MathBridge->Step(Pilot, State, O);
        const bool BackendValid = Valid;
        const FVector F(O.force_n[0], O.force_n[1], O.force_n[2]);
        const FVector M(O.moment_n_m[0], O.moment_n_m[1], O.moment_n_m[2]);
        // Convert the backend's host-equivalent moment to the configured CG.
        // The spatial solve already contains joint recoil and added inertia;
        // the ABI retains the origin-to-CG shift using the raw surface force.
        // CG is aft-positive, so r_cg = (-cg, 0, 0) in the body frame.
        double SurfaceComponents[3]{};
        const bool HasSurfaceForce = MathBridge->ReadExternalForce(SurfaceComponents);
        const FVector SurfaceForce = HasSurfaceForce
            ? FVector(SurfaceComponents[0], SurfaceComponents[1], SurfaceComponents[2]) : F;
        // Shift the surface-force moment, not the reduced host force: the
        // latter contains inertial recoil and a gravity correction, which
        // previously created a spurious CG-dependent pitching moment.
        const FVector Mcg = M - FVector::CrossProduct(CgBody, SurfaceForce);
        CgPitchMomentCorrection = Mcg.Y - M.Y;
        if (bFlightTest && FParse::Param(FCommandLine::Get(), TEXT("B2FCgTest")) && TestTime >= 18)
        {
            const int32 CgSampleBit = TestTime < 19.4 ? 1 : 2;
            if (!(CgTestSamples & CgSampleBit))
            {
                const double ExpectedMm = CgSampleBit == 1 ? -20 : 20;
                const FVector ActualCg = Body->GetComponentQuat().UnrotateVector(Body->GetCenterOfMass() - BodyPos) / 100.;
                B2F_MassProperties CgProps{};
                const bool CgPass = HasSurfaceForce && MathBridge->ReadMassProperties(CgBody.X, CgProps) &&
                    FMath::IsNearlyEqual(double(CgOffsetMm), ExpectedMm, 1.e-5) &&
                    (ActualCg - CgBody).Size() < 1.e-5 &&
                    FMath::IsNearlyEqual(Inertia.Y, CgProps.dry_inertia_kg_m2[1], 1.e-6) &&
                    FMath::IsNearlyEqual(CgPitchMomentCorrection, -CgM * SurfaceForce.Z, 1.e-8);
                UE_LOG(LogTemp, Display, TEXT("CgTest %s settingMm=%.1f actualX=%.5f inertiaY=%.7f forceZ=%.5f hostZ=%.5f pitchCorrection=%.6f"),
                    CgPass ? TEXT("PASS") : TEXT("FAIL"), CgOffsetMm, ActualCg.X, Inertia.Y,
                    SurfaceForce.Z, F.Z, CgPitchMomentCorrection);
                CgTestSamples |= CgSampleBit;
                if (!CgPass) FPlatformMisc::RequestExitWithStatus(false, 1);
            }
        }
        Valid = Valid && Finite(F) && Finite(M) && Finite(Mcg) && F.Size() < 100 && M.Size() < 50 &&
                FMath::IsFinite(O.left_flap_deg) && FMath::IsFinite(O.right_flap_deg) &&
                FMath::Abs(O.left_flap_deg) <= 85 && FMath::Abs(O.right_flap_deg) <= 85;
        if (!Valid)
        {
            ++MathFailures;
            bHealthy = bFlying = false;
            Accumulator = 0;
            AeroForce = AeroMoment = FVector::ZeroVector;
            UE_LOG(LogTemp, Error, TEXT("FlightMathRejected: backend=%d force=%s moment=%s wings=(%.3f,%.3f) velocity=%s rates=%s; disarmed, R recreates firmware context"),
                   BackendValid, *F.ToString(), *M.ToString(), O.left_flap_deg, O.right_flap_deg, *V.ToString(), *W.ToString());
            return false;
        }
        const FVector WorldForce = Rotation.RotateVector(F);
        const FVector WorldMoment = Rotation.RotateVector(Mcg);
        ForceSum += WorldForce;
        MomentSum += WorldMoment;
        LeftFlap = O.left_flap_deg;
        RightFlap = O.right_flap_deg;
        BatterySoc = O.battery_soc;
        LastMechanicalPower = O.mechanical_power_w;
        LastPhaseError = O.phase_error;
        LastKGainMod = O.k_gain_mod;
        FlapHz = O.flap_hz;
        // Integrate diagnostic averages at physics rate: sampling only the
        // last strip evaluation of a rendered frame can alias the wingbeat.
        if (bVectors)
        {
            B2F_WingField StatsFields[2][B2F_WING_STATIONS];
            B2F_WingLoads StatsLoads[2][B2F_WING_STATIONS];
            if (MathBridge->ReadWingFields(StatsFields[0], StatsFields[1]) &&
                MathBridge->ReadWingLoads(StatsLoads[0], StatsLoads[1]))
            {
                const double Blend = bStripStatsValid ? 1.0 - FMath::Exp(-StepDt / .5) : 1.0;
                for (int Side = 0; Side < 2; ++Side)
                {
                    FVector Stats = FVector::ZeroVector;
                    for (uint32 I = 0; I < B2F_WING_STATIONS; ++I)
                    {
                        const auto& Field = StatsFields[Side][I];
                        Stats.X += Rotation.RotateVector(FVector(Field.lift_n[0], Field.lift_n[1], Field.lift_n[2])).Z;
                        Stats.Y += Field.air_velocity_m_s[0] / B2F_WING_STATIONS;
                        Stats.Z += StatsLoads[Side][I].moment_nm[0];
                    }
                    SmoothedStripStats[Side] += Blend * (Stats - SmoothedStripStats[Side]);
                }
                bStripStatsValid = true;
            }
        }
        else bStripStatsValid = false;
        // Predict BOTH linear and rotational feedback between wing evaluations.
        // Holding angular velocity for a whole 30 Hz frame made aerodynamic
        // roll damping overshoot. Chaos receives the mean loads once and stays
        // authoritative for the actual rigid body and contacts.
        Velocity += (WorldForce / Body->GetMass() + Gravity) * StepDt;
        Omega += Rotation.RotateVector(Mcg / Inertia) * StepDt;
        const double Rate = Omega.Size();
        if (Rate > UE_SMALL_NUMBER)
            Rotation = (FQuat(Omega / Rate, Rate * StepDt) * Rotation).GetNormalized();
        ++Steps;
    }
    if (Steps)
    {
        AeroForce = ForceSum / Steps;
        AeroMoment = MomentSum / Steps;
    }
    LastMathRotation = Rotation;
    bHaveMathRotation = true;
    MathProfileSeconds += FPlatformTime::Seconds() - ProfileStart;
    MathProfileSteps += Steps;
    if (++MathProfileFrames >= 120)
    {
        UE_LOG(LogTemp, Display, TEXT("FlightPhysicsTiming frameMs=%.3f stepMs=%.3f steps=%d frameDt=%.6f physicsDt=%.6f massKg=%.4f rollInertia=%.8f"),
            MathProfileSeconds * 1000 / MathProfileFrames,
            MathProfileSeconds * 1000 / FMath::Max(1, MathProfileSteps), Steps,
            DeltaSeconds, PhysicsDt, Body->GetMass(), Inertia.X);
        MathProfileSeconds = 0; MathProfileSteps = 0; MathProfileFrames = 0;
    }
    return true;
}

void ABorn2FlapFlightPawn::UpdateAeroAudio(float Dt)
{
    if (!AudioSynth || !bHealthy)
        return;

    const FVector P = Body->GetComponentLocation();
    const FVector V = Body->GetPhysicsLinearVelocity() / 100.0;
    const auto* WeatherMode=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    const FVector Wind = (WeatherMode ? WeatherMode->WindAt(P,WorldTime) : Born2FlapWind::Sample(P,WorldTime))*FMath::SmoothStep(0.0,2.0,double(GetAltitude()));
    const FVector AirVel = V - Wind;

    born2flap::aeroaudio::FTelemetry Tel;
    Tel.airspeed = AirVel.Size();
    Tel.altitude = GetAltitude();
    Tel.thermal_strength = FMath::Clamp((double)Wind.Z / 6.0, 0.0, 1.0);
    const double Load = FMath::Clamp(LastMechanicalPower / 50.0, 0.0, 1.0);
    Tel.servo_load_l = Load;
    Tel.servo_load_r = Load;
    const float FlapDelta = LeftFlap - PrevLeftFlap;                     // deg this tick; + = wing rising
    Tel.sweep_rate = Dt > 0.f ? FMath::Abs((double)FlapDelta) / Dt : 0.0;
    // Lock the audio beat to the REAL stroke instead of a 2+5·throttle guess
    // (which drifted from the servo-driven flap and sounded laggy/desynced).
    // Detect the top of the stroke where the flap velocity flips rising→falling
    // and measure the period. Falls back to the throttle estimate while gliding.
    const bool bFlapRisingNow = FlapDelta > 0.f;
    if (bFlapRising && !bFlapRisingNow)
    {
        const double Now = WorldTime;
        if (LastStrokeTopTime >= 0.0)
        {
            const double Period = Now - LastStrokeTopTime;
            if (Period > 0.03 && Period < 2.0)               // 0.5..33 Hz sanity window
                MeasuredWingbeatHz = 1.0 / Period;
        }
        LastStrokeTopTime = Now;
    }
    bFlapRising = bFlapRisingNow;
    // No stroke top for a while means the wings have stopped flapping: drop the
    // measured rate so the throttle estimate takes over again.
    if (LastStrokeTopTime >= 0.0 && (WorldTime - LastStrokeTopTime) > 1.5)
        MeasuredWingbeatHz = 0.0;
    // Amplitude must be quantified by the flap rate itself. The old "+2.0" floor
    // kept the wing voice "flapping" at 2 Hz even on a perched/gliding bird (no
    // measured stroke), which the synth read as a rhythmic thump at rest. No
    // measured flap → 0 Hz, so the wing voice goes silent at rest and the throttle
    // estimate (0..5 Hz) only ramps in while actually throttling up.
    Tel.wingbeat_hz = MeasuredWingbeatHz > 0.5 ? MeasuredWingbeatHz : (5.0 * Throttle);
    // Wing audio is FORCE-DRIVEN, not wingbeat-driven. Read the per-strip
    // aerodynamic force the physics computed last step and derive two signals:
    //   wing_force   → total |force| (steady stream + the natural flap transient)
    //   wing_flutter → |force − low-pass(force)| (the high-frequency separation)
    // There is no synthetic wingbeat clock, so a glide is a constant stream and
    // a flap is the real force transient.
    double WingStripForces[B2F_WING_STATIONS * 2];
    double ForceTotal = 0.0;
    if (MathBridge && MathBridge->ReadWingForces(&WingStripForces[0], &WingStripForces[B2F_WING_STATIONS]))
    {
        for (int32 I = 0; I < B2F_WING_STATIONS * 2; ++I)
            ForceTotal += FMath::Abs(WingStripForces[I]);
    }
    // Reference: ~2.5 g of body weight — the peak aero force a hard downstroke
    // reaches. Normalizing by weight keeps the flap pulse sized to the bird.
    const double WeightN = double(Body->GetMass()) * 9.81;
    const double WingForceRef = FMath::Max(2.0, WeightN * 2.5);
    const double ForceSmooth = FMath::Clamp(Dt * 6.0, 0.0, 1.0);   // ~0.17 s low-pass
    WingForceLp += (ForceTotal - WingForceLp) * ForceSmooth;
    Tel.wing_force = FMath::Clamp(ForceTotal / WingForceRef, 0.0, 1.0);
    Tel.wing_flutter = FMath::Clamp(FMath::Abs(ForceTotal - WingForceLp) / (WingForceRef * 0.5), 0.0, 1.0);
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
        double Gain=born2flap::aeroaudio::GetNum(Voices[I],"gain");
        // Voice 1 is "wing" — the wingbeat, and voices 2/3 are the servo whine
        // ("servo_l"/"servo_r"). The servos are the mechanical partner of the
        // flap, so they ride the SAME WINGBEAT VOLUME slider as the wing but at
        // 80% (the whine reads best as a texture slightly under the thump), and
        // are exempt from the onboard headroom cut (CameraGain) for the same
        // reason: that cut was what buried the flap and its servo texture in FPV.
        if (I == 1)            Gain *= WingbeatVolume;
        else if (I == 2 || I == 3) Gain *= WingbeatVolume * 0.80;
        else                   Gain *= CameraGain;
        born2flap::aeroaudio::SetNum(Voices[I],"gain",Gain);
        AudioSynth->SetVoiceParams(Names[I], Voices[I]);
    }

    PrevLeftFlap = LeftFlap;
}

void ABorn2FlapFlightPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // F8/Esc are owned here, in one place. The panel is NOT a paused menu any
    // more (no SetPause), so this tick runs while it is open and simply freezes
    // the simulation below. The keys are captured by a Slate input pre-processor
    // (registered in BeginPlay): while the panel is open FInputModeGameAndUI
    // keeps the mouse on the UMG tree but diverts keyboard away from PlayerInput,
    // so PlayerInput-level queries silently return false for F8/Esc. The
    // pre-processor sees every key-down before that routing and defers the
    // toggle to this tick.
    bool bF8Pressed = false, bEscPressed = false;
    if (const auto Proc = StaticCastSharedPtr<FPanelKeyInputProcessor>(PanelKeyProcessor))
    {
        bF8Pressed = Proc->ConsumeF8();
        bEscPressed = Proc->ConsumeEsc();
    }
    // Belt-and-braces fallback: in some input-mode sessions FInputModeGameAndUI
    // leaves keyboard on PlayerInput while the pre-processor never sees F8/Esc.
    // Query PlayerInput directly too, so the panel can never become un-closable.
    if (APlayerController* PanelPC = Cast<APlayerController>(GetController()))
    {
        bF8Pressed = bF8Pressed || PanelPC->WasInputKeyJustPressed(born2flap::keybinds::Get(TEXT("TogglePoi")));
        bEscPressed = bEscPressed || PanelPC->WasInputKeyJustPressed(born2flap::keybinds::Get(TEXT("ClosePanel")));
    }
    // The main menu (F10) is modal and owned by the GameMode. While it is open,
    // F8/Esc must not open or close the settings panel / POI overlay here.
    const bool bMenuOpen = [&]()
    {
        const ABorn2FlapGameMode* GM = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
        return GM && GM->IsMenuOpen();
    }();
    if (bF8Pressed && !bMenuOpen)
    {
        // F8 toggles the POI overlay (settings are reached from the F10 menu now).
        if (IsFlightSettingsOpen()) { /* F8 is inert while settings are open */ }
        else if (IsPoiOverlayOpen()) ClosePoiOverlay();
        else OpenPoiOverlay();
        return;
    }
    if (IsFlightSettingsOpen())
    {
        if (bEscPressed && !bMenuOpen) CloseFlightSettings();
        return;
    }
    if (IsPoiOverlayOpen())
    {
        if (bEscPressed && !bMenuOpen) ClosePoiOverlay();
        return;
    }
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
    bool WDown = false, MuteMouseYaw = false, MuteMouseRoll = false, MmbDown = false;
    bool MmbPressed = false, MmbReleased = false;
    bool ResetMouse = false;
    bool Launch = false, Reset = false;
    if (auto *PC = Cast<APlayerController>(GetController()))
    {
        using namespace born2flap::keybinds;
        if (PC->WasInputKeyJustPressed(Get(TEXT("GroundView")))) ToggleGroundView();
        if (PC->WasInputKeyJustPressed(Get(TEXT("FpvView")))) ToggleFpvView();
        if (bFpvAirView)
        {
            if (PC->WasInputKeyJustPressed(Get(TEXT("FpvAngleDown")))) FpvCameraAngleDeg = FMath::Clamp(FpvCameraAngleDeg - 5.f, -45.f, 45.f);
            if (PC->WasInputKeyJustPressed(Get(TEXT("FpvAngleUp")))) FpvCameraAngleDeg = FMath::Clamp(FpvCameraAngleDeg + 5.f, -45.f, 45.f);
        }
        if (RcController)
            RcController->Tick(PC, Dt);
        WDown = PC->IsInputKeyDown(Get(TEXT("Throttle")));
        MmbDown = PC->IsInputKeyDown(Get(TEXT("FreeCamera")));
        MmbPressed = PC->WasInputKeyJustPressed(Get(TEXT("FreeCamera")));
        MmbReleased = PC->WasInputKeyJustReleased(Get(TEXT("FreeCamera")));
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
                      PC->WasInputKeyJustPressed(EKeys::RightMouseButton) ||
                      PC->WasInputKeyJustPressed(EKeys::SpaceBar);
        if (RcController && RcController->IsPanelOpen())
        {
            MouseX = MouseY = Wheel = 0;
            MuteMouseYaw = MuteMouseRoll = true;
        }
        // A drag (any mouse move or scroll while MMB is held) marks the press as
        // an orbit gesture; a clean click (no move/scroll) resets on release.
        if (MmbPressed) bMmbOrbitDragged = false;
        if (MmbDown && (!FMath::IsNearlyZero(MouseX) || !FMath::IsNearlyZero(MouseY) || !FMath::IsNearlyZero(Wheel)))
            bMmbOrbitDragged = true;
        Steer = (PC->IsInputKeyDown(Get(TEXT("YawRight"))) ? 1.f : 0.f) - (PC->IsInputKeyDown(Get(TEXT("YawLeft"))) ? 1.f : 0.f);
        Roll = (PC->IsInputKeyDown(Get(TEXT("RollRight"))) ? 1.f : 0.f) - (PC->IsInputKeyDown(Get(TEXT("RollLeft"))) ? 1.f : 0.f);
        Pitch = (PC->IsInputKeyDown(Get(TEXT("PitchUp"))) ? 1.f : 0.f) -
                (PC->IsInputKeyDown(Get(TEXT("PitchDown"))) || PC->IsInputKeyDown(Get(TEXT("PitchDownAlt"))) ? 1.f : 0.f);
#if PLATFORM_IOS || PLATFORM_ANDROID
        // Mobile touch: the engine's DefaultVirtualJoysticks twin-stick overlay
        // injects Gamepad_LeftX/Y + Gamepad_RightX/Y analog axes (SVirtualJoystick
        // -> SceneViewport -> PlayerInput::InputKey). Drive the virtual Mode-2
        // transmitter exactly like the RC/keyboard path:
        //   left stick  = throttle (Y, push up) + yaw (X)
        //   right stick = pitch (Y, push up)  + roll (X)
        if (PC->PlayerInput)
        {
            const float LX = PC->PlayerInput->GetRawKeyValue(EKeys::Gamepad_LeftX);
            const float LY = PC->PlayerInput->GetRawKeyValue(EKeys::Gamepad_LeftY);
            const float RX = PC->PlayerInput->GetRawKeyValue(EKeys::Gamepad_RightX);
            const float RY = PC->PlayerInput->GetRawKeyValue(EKeys::Gamepad_RightY);
            const float Dead = 0.06f;
            const float LeftX  = FMath::Abs(LX) < Dead ? 0.f : LX;
            const float LeftY  = FMath::Abs(LY) < Dead ? 0.f : LY;
            const float RightX = FMath::Abs(RX) < Dead ? 0.f : RX;
            const float RightY = FMath::Abs(RY) < Dead ? 0.f : RY;
            // SceneViewport negates Gamepad_RightY before it reaches PlayerInput,
            // so a positive RightY means thumb down (LeftY stays thumb-up). Rebuild
            // both sticks as "up-positive" Y so the chosen throttle/pitch sticks
            // behave identically regardless of which physical stick they live on.
            const float LeftUpY  = LeftY;
            const float RightUpY = -RightY;

            // Configurable twin-stick layout (throttle stick, roll stick). Pitch
            // always lives on the non-throttle stick; yaw on the non-roll stick.
            const bool bThrottleRight = (MobileThrottleStick == 1);
            const bool bRollRight     = (MobileRollStick == 1);
            const float ThrottleY = bThrottleRight ? RightUpY : LeftUpY;
            const float PitchY    = bThrottleRight ? LeftUpY   : RightUpY;
            const float RollX     = bRollRight     ? RightX    : LeftX;
            const float YawX      = bRollRight     ? LeftX     : RightX;

            const float TouchThrottle = FMath::Clamp(ThrottleY, 0.f, 1.f);
            Effort = TouchThrottle;
            WDown  = TouchThrottle > 0.05f;
            Steer  = YawX;
            Roll   = RollX;
            Pitch  = PitchY * (bMobileInvertPitch ? -1.f : 1.f);
            // Hand-launch on throttle-up while grounded (there is no Space bar).
            if (TouchThrottle < 0.25f)
                bTouchLaunchPrimed = true;
            else if (bTouchLaunchPrimed && !bFlying)
            {
                bTouchLaunchPrimed = false;
                Launch = true;
            }
            // No desktop pointer on touch; keep the mouse stick neutral so a
            // stale desktop delta can never bleed into the flight channels.
            MouseX = MouseY = Wheel = 0.f;
            MuteMouseYaw = MuteMouseRoll = true;
        }
#endif
        Launch |= PC->WasInputKeyJustPressed(Get(TEXT("Launch")));
        Reset = PC->WasInputKeyJustPressed(Get(TEXT("Reset")));
        if (RcController)
        {
            Launch = (Launch || RcController->LaunchPressed()) && !RcController->IsPanelOpen();
            Reset |= RcController->ResetPressed();
        }
        // Always hard-reset the virtual mouse stick on a throw (rebound-safe), so
        // no residual look offset spoils the launch.
        if (Launch)
            ResetMouse = true;
        if (PC->WasInputKeyJustPressed(Get(TEXT("ToggleVectors"))))
        {
            VectorView = (VectorView + 1) % 3;
            bVectors = VectorView != 0;
        }
        if (PC->WasInputKeyJustPressed(Get(TEXT("ToggleUi"))))
            ToggleUIHidden();
        if (PC->WasInputKeyJustPressed(Get(TEXT("ThrottleMode"))))
            ToggleThrottleMode();
    }
    // Ground interaction: while the bird is grounded, holding the middle mouse
    // button puts the player in walk/look mode. WASD/arrows move the ground
    // observer (the future pilot) around the ornithopter and the mouse rotates
    // the camera — orbiting the bird in chase view, free-looking in ground view.
    // Releasing MMB returns to flight inputs, where W flaps the wings to creep
    // the bird into position for the next throw.
    // Walk/look mode also works in flight (cinematic ground-level shots while
    // the bird is still airborne). The bird is never carried along — it stays
    // put and only the pilot (ground observer) walks.
    const bool bGroundWalk = MmbDown && !bMenuOpen;
    if (bGroundWalk)
    {
        if (auto* PC = Cast<APlayerController>(GetController()))
        {
            using namespace born2flap::keybinds;
            const float WalkFwd = FMath::Clamp(
                (PC->IsInputKeyDown(Get(TEXT("Throttle"))) ? 1.f : 0.f) - (PC->IsInputKeyDown(Get(TEXT("PitchDown"))) ? 1.f : 0.f) +
                (PC->IsInputKeyDown(Get(TEXT("PitchUp"))) ? 1.f : 0.f) - (PC->IsInputKeyDown(Get(TEXT("PitchDownAlt"))) ? 1.f : 0.f),
                -1.f, 1.f);
            const float WalkStr = FMath::Clamp(
                (PC->IsInputKeyDown(Get(TEXT("YawRight"))) ? 1.f : 0.f) - (PC->IsInputKeyDown(Get(TEXT("YawLeft"))) ? 1.f : 0.f) +
                (PC->IsInputKeyDown(Get(TEXT("RollRight"))) ? 1.f : 0.f) - (PC->IsInputKeyDown(Get(TEXT("RollLeft"))) ? 1.f : 0.f),
                -1.f, 1.f);
            const float LookSens = 0.22f;   // degrees per raw mouse count
            // Walk vs run: holding Shift (sprint) roughly doubles the ground
            // observer's pace so the pilot can cover ground when scouting.
            const bool bRun = PC->IsInputKeyDown(EKeys::LeftShift) ||
                              PC->IsInputKeyDown(EKeys::RightShift);
            const float WalkSpeed = bRun ? 440.f : 220.f;  // cm/s
            if (bGroundView)
            {
                // Ground perspective: free-look (can turn away from the bird).
                // The invert flag mirrors the existing chase-orbit convention so
                // a single option can flip this free-look without touching flight.
                const float Inv = bFreeLookInvert ? -1.f : 1.f;
                GroundLookYaw -= Inv * MouseX * LookSens;
                GroundLookPitch -= Inv * MouseY * LookSens;
                // Allow looking straight up (~90°); 89° keeps a hair of margin so
                // the yaw/pitch Euler basis never degenerates at exactly +90.
                GroundLookPitch = FMath::Clamp(GroundLookPitch, -80.f, 89.f);
                const float FwdYaw = float(GroundGaze.Yaw + GroundLookYaw);
                FVector Dir = FRotator(0.f, FwdYaw, 0.f).Vector() * WalkFwd +
                              FRotator(0.f, FwdYaw + 90.f, 0.f).Vector() * WalkStr;
                if (!Dir.IsNearlyZero())
                    MoveWalker(Dir.GetSafeNormal() * (WalkSpeed * Dt));
            }
            else if (!bFpvAirView)
            {
                // Third-bird (chase) perspective: orbit the camera around the bird.
                ChaseOrbitYaw -= MouseX * LookSens;
                ChaseOrbitPitch -= MouseY * LookSens;
                ChaseOrbitPitch = FMath::Clamp(ChaseOrbitPitch, -80.f, 10.f);
                const float FwdYaw = float(Body->GetComponentRotation().Yaw + ChaseOrbitYaw);
                FVector Dir = FRotator(0.f, FwdYaw, 0.f).Vector() * WalkFwd +
                              FRotator(0.f, FwdYaw + 90.f, 0.f).Vector() * WalkStr;
                if (!Dir.IsNearlyZero())
                    MoveWalker(Dir.GetSafeNormal() * (WalkSpeed * Dt));
            }
            // Scroll-wheel zoom (MMB held). Ground view: lens zoom to keep a far
            // bird readable or inspect it up close. Chase view: dolly the spring
            // arm — zooming in far enough switches to the onboard FPV lens, and
            // scrolling out leaves it again.
            if (!FMath::IsNearlyZero(Wheel))
            {
                const float Scroll = FMath::Clamp(Wheel, -1.f, 1.f);
                if (bGroundView)
                {
                    GroundZoom = FMath::Clamp(GroundZoom - Scroll * 0.12f, 0.3f, 3.5f);
                }
                else if (bFpvAirView)
                {
                    // FPV sits at the innermost end of the chase arm. Keep the wheel
                    // consistent with the chase dolly: wheel-down zooms back out and
                    // leaves FPV only once the arm has passed the switch point —
                    // continuous, not a reversed binary jump. Wheel-up stays fully
                    // zoomed in.
                    if (Scroll < 0.f)
                    {
                        ChaseArmLength = FMath::Clamp(ChaseArmLength - Scroll * 60.f, 60.f, 2000.f);
                        if (ChaseArmLength > 90.f)
                        {
                            bFpvAirView = false;
                            if (PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
                        }
                    }
                }
                else
                {
                    ChaseArmLength = FMath::Clamp(ChaseArmLength - Scroll * 60.f, 60.f, 2000.f);
                    if (ChaseArmLength <= 90.f)
                    {
                        ChaseArmLength = 90.f;
                        bFpvAirView = true;
                        if (PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
                    }
                }
            }
        }
        // Neutralise the bird's own control surfaces while the pilot is walking on
        // the ground. In flight the bird keeps its throttle + sticks so orbiting the
        // chase camera never stops the wings — only the mouse is diverted to orbit.
        MouseX = MouseY = Wheel = 0.f;
        if (!bFlying)
        {
            Effort = 0.f; Roll = Pitch = Steer = 0.f;
            WDown = false;
            Launch = Reset = false;
        }
    }
    // A clean middle-mouse click (press + release with no drag/scroll) snaps the
    // chase orbit + zoom back to their default follow position.
    if (MmbReleased && !bMmbOrbitDragged && !bMenuOpen)
    {
        ChaseOrbitYaw = 0.f; ChaseOrbitPitch = -12.f;
        ChaseArmLength = 480.f;
        GroundZoom = 1.f;
        GroundLookYaw = 0.f; GroundLookPitch = 0.f;
        if (bFpvAirView)
        {
            bFpvAirView = false;
            if (auto* PC = Cast<APlayerController>(GetController()); PC && PC->PlayerCameraManager)
                PC->PlayerCameraManager->SetGameCameraCutThisFrame();
        }
    }
#if PLATFORM_IOS || PLATFORM_ANDROID
    // Mobile multi-touch: a single-finger drag (or a parallel two-finger pan)
    // rotates the view — chase orbit in the air, free-look on the ground — while
    // pinch + pivot walk the ground observer. Mirrors the desktop MMB orbit/walk.
    if (!bMenuOpen && !IsFlightSettingsOpen() && !IsPoiOverlayOpen() && !bFlightTest && !bDesktopInputTest)
    {
        UpdateTouchGestures(DeltaSeconds, Cast<APlayerController>(GetController()));
        if (!FMath::IsNearlyZero(GestureViewYaw) || !FMath::IsNearlyZero(GestureViewPitch))
        {
            if (bGroundView)
            {
                const float Inv = bFreeLookInvert ? -1.f : 1.f;
                GroundLookYaw -= Inv * GestureViewYaw;
                GroundLookPitch -= Inv * GestureViewPitch;
                GroundLookPitch = FMath::Clamp(GroundLookPitch, -80.f, 89.f);
            }
            else if (!bFpvAirView)
            {
                ChaseOrbitYaw -= GestureViewYaw;
                ChaseOrbitPitch -= GestureViewPitch;
                ChaseOrbitPitch = FMath::Clamp(ChaseOrbitPitch, -80.f, 10.f);
            }
        }
        if (bGroundView && (!FMath::IsNearlyZero(GestureWalkForward) || !FMath::IsNearlyZero(GestureWalkStrafe)))
        {
            const float FwdYaw = float(GroundGaze.Yaw + GroundLookYaw);
            const FVector Dir = FRotator(0.f, FwdYaw, 0.f).Vector() * GestureWalkForward +
                                FRotator(0.f, FwdYaw + 90.f, 0.f).Vector() * GestureWalkStrafe;
            if (!Dir.IsNearlyZero())
                MoveWalker(Dir);
        }
    }
#endif
    UpdateAvatarState(DeltaSeconds);
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
        if (FParse::Param(FCommandLine::Get(), TEXT("B2FPitchBalanceTest")))
        {
            Effort = TestTime >= 3 ? .72f : 0;
            Roll = Pitch = Steer = 0;
            Launch = Reset = false;
            double WheelSetting = .5;
            FParse::Value(FCommandLine::Get(), TEXT("B2FTestWheel="), WheelSetting);
            Desktop.speedModifier = FMath::Clamp(WheelSetting, 0., 1.);
            if (Previous < 3 && TestTime >= 3)
            {
                ResetFlight();
                double TestCg = 0;
                FParse::Value(FCommandLine::Get(), TEXT("B2FPitchTestCg="), TestCg);
                SetCgOffsetMm(TestCg);
                Body->SetWorldLocationAndRotation(FVector(0, 0, 10000), FRotator::ZeroRotator,
                    false, nullptr, ETeleportType::TeleportPhysics);
                Body->SetPhysicsLinearVelocity(FParse::Param(FCommandLine::Get(), TEXT("B2FPitchTestLaunch"))
                    ? FVector(850, 0, 220) : FVector(500, 0, 0));
                Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
                bFlying = true;
            }
            if (TestTime >= 3)
            {
                const double Angle = Body->GetComponentRotation().Pitch;
                PitchTestPeak = FMath::Max(PitchTestPeak, FMath::Abs(Angle));
                if (TestTime >= 9)
                {
                    PitchTestIntegral += Angle * DeltaSeconds;
                    PitchTestDuration += DeltaSeconds;
                }
            }
            if (TestTime >= 15 && !bTestFinished)
            {
                bTestFinished = true;
                const bool Pass = bHealthy && MathFailures == 0 && SafetyResets == 0 &&
                    PitchTestPeak < 60 && GetAltitude() > 20;
                UE_LOG(LogTemp, Display, TEXT("PitchBalanceTest %s cgMm=%.1f wheel=%.2f peakPitch=%.3f meanPitch=%.3f altitude=%.3f speed=%.3f mathFailures=%d"),
                    Pass ? TEXT("PASS") : TEXT("FAIL"), CgOffsetMm, Desktop.speedModifier,
                    PitchTestPeak, PitchTestIntegral/FMath::Max(.001,PitchTestDuration), GetAltitude(), GetSpeed(), MathFailures);
                FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
            }
        }
        double YawTestDirection = 0;
        if (FParse::Value(FCommandLine::Get(), TEXT("B2FYawBankTest="), YawTestDirection))
        {
            YawTestDirection = YawTestDirection < 0 ? -1 : 1;
            Effort = TestTime >= 3 ? .72f : 0;
            Steer = TestTime >= 18 && TestTime < 22 ? .3f * YawTestDirection : 0;
            double YawRampSeconds = 0;
            FParse::Value(FCommandLine::Get(), TEXT("B2FYawTestRamp="), YawRampSeconds);
            if (YawRampSeconds > 0) Steer *= FMath::Clamp((TestTime - 18) / YawRampSeconds, 0., 1.);
            if (TestTime >= 18 && FParse::Param(FCommandLine::Get(), TEXT("B2FYawTestGlide"))) Effort = 0;
            Roll = Pitch = 0;
            Reset = false;
            Launch = Previous < 3 && TestTime >= 3;
            // Isolate an airborne turn: ground contacts before the command
            // otherwise supply uncontrolled roll/yaw impulses to this test.
            if (Launch)
            {
                ResetFlight();
                Body->SetWorldLocationAndRotation(FVector(0, 0, 10000), FRotator::ZeroRotator,
                    false, nullptr, ETeleportType::TeleportPhysics);
                Body->SetPhysicsLinearVelocity(FVector(500, 0, 0));
                Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
                bFlying = true;
                Launch = false;
            }
            if (Previous < 18 && TestTime >= 18)
            {
                YawTestStartHeading = Body->GetComponentRotation().Yaw;
                YawTestStartBank = Body->GetComponentRotation().Roll;
            }
            if (Previous < 18.25 && TestTime >= 18.25)
                YawTestEarlyBank = FMath::FindDeltaAngleDegrees(YawTestStartBank, Body->GetComponentRotation().Roll);
            if (TestTime >= 19 && TestTime < 22)
            {
                YawTestBankIntegral += FMath::FindDeltaAngleDegrees(YawTestStartBank, Body->GetComponentRotation().Roll) * DeltaSeconds;
                YawTestDuration += DeltaSeconds;
            }
            if (TestTime >= 22 && !bTestFinished)
            {
                bTestFinished = true;
                const double MeanBank = YawTestBankIntegral / FMath::Max(.001, YawTestDuration);
                const double HeadingChange = FMath::FindDeltaAngleDegrees(YawTestStartHeading, Body->GetComponentRotation().Yaw);
                const bool Pass = bHealthy && bFlying && MathFailures == 0 && SafetyResets == 0 &&
                    YawTestDirection * HeadingChange > 5 && YawTestDirection * MeanBank > 1;
                UE_LOG(LogTemp, Display, TEXT("YawBankTest %s direction=%.0f heading=%.3f meanBank=%.3f earlyBank=%.3f startBank=%.3f altitude=%.3f speed=%.3f safety=%d mathFailures=%d"),
                    Pass ? TEXT("PASS") : TEXT("FAIL"), YawTestDirection, HeadingChange, MeanBank, YawTestEarlyBank, YawTestStartBank, GetAltitude(), GetSpeed(), SafetyResets, MathFailures);
                FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
            }
        }
        if (Previous < 18 && TestTime >= 18 && FParse::Param(FCommandLine::Get(), TEXT("B2FCapture")))
            FScreenshotRequest::RequestScreenshot(TEXT("AerodynamicFlight.png"), true, false);
        if (Previous < 60 && TestTime >= 60 && FParse::Param(FCommandLine::Get(), TEXT("B2FCapture")))
            FScreenshotRequest::RequestScreenshot(TEXT("MurexApproach.png"), true, false);
        if (FParse::Param(FCommandLine::Get(), TEXT("B2FVectorCapture")))
        {
            if (FParse::Param(FCommandLine::Get(), TEXT("B2FCgTest")))
            {
                if (Previous < 18 && TestTime >= 18) SetCgOffsetMm(-20);
                if (Previous < 19.4 && TestTime >= 19.4) SetCgOffsetMm(20);
            }
            if (FParse::Param(FCommandLine::Get(), TEXT("B2FTailAngleTest")))
            {
                if (Previous < 18 && TestTime >= 18) SetTuning(ETuningField::TailElevatorAngle, -8);
                if (Previous < 19.4 && TestTime >= 19.4) SetTuning(ETuningField::TailElevatorAngle, 8);
            }
            VectorView = TestTime < 18.6 ? 1 : 2;
            bVectors = true;
            if (Previous < 18 && TestTime >= 18)
                FScreenshotRequest::RequestScreenshot(TEXT("StripForces.png"), true, false);
            if (Previous < 19.4 && TestTime >= 19.4)
                FScreenshotRequest::RequestScreenshot(TEXT("StripAirflow.png"), true, false);
            if (TestTime >= 20 && !bTestFinished)
            {
                bTestFinished = true;
                UE_LOG(LogTemp, Display, TEXT("F5_CAPTURE_COMPLETE views=2 strips=32; visual inspection required"));
                FPlatformMisc::RequestExitWithStatus(false, 0);
            }
        }
        if (FParse::Param(FCommandLine::Get(), TEXT("B2FRollResponseTest")))
        {
            // Loaded tow fixture: constrain forward speed, pitch and yaw while
            // Chaos integrates heave, lateral motion and roll. Measure the actual
            // bent surfaces as well as body/tail; unloaded shoulder angles alone
            // cannot establish pressure-supported roll.
            Effort = Steer = Pitch = 0; Launch = Reset = false;
            Roll = .25 * FMath::Clamp((TestTime - 3.0) / 8.0, 0.0, 1.0);
            if (Previous < 1 && TestTime >= 1)
            {
                ResetFlight();
                Body->SetWorldLocationAndRotation(FVector(0,0,3000), FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
                Body->SetPhysicsLinearVelocity(FVector(800,0,0));
                bFlying = true; bCameraGrounded = false;
                RollTestPeakRate = 0;
            }
            if (TestTime >= 1)
            {
                FRotator Attitude = Body->GetComponentRotation();
                Attitude.Pitch = Attitude.Yaw = 0;
                Body->SetWorldRotation(Attitude, false, nullptr, ETeleportType::TeleportPhysics);
                FVector Velocity = Body->GetPhysicsLinearVelocity();
                Velocity.X = 800;
                Body->SetPhysicsLinearVelocity(Velocity);
                FVector Rates = Body->GetPhysicsAngularVelocityInRadians();
                Rates.Y = Rates.Z = 0;
                Body->SetPhysicsAngularVelocityInRadians(Rates);
                if (TestTime >= 3) RollTestPeakRate = FMath::Max(RollTestPeakRate, FMath::Abs(Rates.X));
            }
            B2F_WingField FieldsL[B2F_WING_STATIONS], FieldsR[B2F_WING_STATIONS];
            const bool HaveFields = MathBridge && MathBridge->ReadWingFields(FieldsL, FieldsR);
            auto SurfaceBank = [&](const B2F_WingField* Fields)
            {
                double NormalY = 0, NormalZ = 0;
                for (uint32 I = 0; I < B2F_WING_STATIONS; ++I)
                {
                    const auto& N = Fields[I].normal;
                    const FVector WorldNormal = Body->GetComponentQuat().RotateVector(FVector(N[0], N[1], N[2]));
                    NormalY += WorldNormal.Y;
                    NormalZ += WorldNormal.Z;
                }
                return FMath::RadiansToDegrees(FMath::Atan2(-NormalY, NormalZ));
            };
            if (Previous < 3 && TestTime >= 3 && HaveFields)
                RollTestBaseline = FVector(Body->GetComponentRotation().Roll, SurfaceBank(FieldsL), SurfaceBank(FieldsR));
            if (TestTime >= 13.0 && !bTestFinished)
            {
                bTestFinished = true;
                const double BodyRoll = FMath::FindDeltaAngleDegrees(RollTestBaseline.X, Body->GetComponentRotation().Roll);
                const double LeftWorld = HaveFields ? FMath::FindDeltaAngleDegrees(RollTestBaseline.Y, SurfaceBank(FieldsL)) : 999;
                const double RightWorld = HaveFields ? FMath::FindDeltaAngleDegrees(RollTestBaseline.Z, SurfaceBank(FieldsR)) : 999;
                const double TailRoll = MembraneTailPivot->GetComponentRotation().Roll;
                // UE Euler roll has the opposite sign to the span-normal bank
                // used above. Test relative joint response, not a world horizon.
                // Subtract on the circle before averaging: either wing can
                // cross +/-180 degrees without the hinge moving by 360.
                const double RelativeRoll = .5 *
                    (FMath::UnwindDegrees(-BodyRoll - LeftWorld) +
                     FMath::UnwindDegrees(-BodyRoll - RightWorld));
                const bool LegacyLevelSupport = FParse::Param(FCommandLine::Get(), TEXT("B2FLegacyLevelSupport"));
                const bool LevelGate = !LegacyLevelSupport ||
                    (FMath::Max(FMath::Abs(LeftWorld), FMath::Abs(RightWorld)) < 3 && RollTestPeakRate < 1);
                const bool Pass = bHealthy && HaveFields && MathFailures == 0 && RelativeRoll < -3 && LevelGate &&
                    FMath::Abs(FMath::FindDeltaAngleDegrees(Body->GetComponentRotation().Roll, TailRoll)) < 1;
                UE_LOG(LogTemp, Display, TEXT("RollResponseTest %s body=%.3f leftSurfaceWorld=%.3f rightSurfaceWorld=%.3f relative=%.3f tail=%.3f peakRate=%.3f mass=%.4f legacyLevelGate=%d"),
                    Pass ? TEXT("PASS") : TEXT("FAIL"), BodyRoll, LeftWorld, RightWorld, RelativeRoll, TailRoll, RollTestPeakRate, Body->GetMass(), LegacyLevelSupport);
                FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
            }
            if (Previous < 11.5 && TestTime >= 11.5 && FParse::Param(FCommandLine::Get(), TEXT("B2FCapture")))
                FScreenshotRequest::RequestScreenshot(TEXT("RollResponse.png"), true, false);
        }
    }
    if (Reset)
        ResetFlight();
    if (Launch)
        LaunchFlight();
    const FVector P = Body->GetComponentLocation(), V = Body->GetPhysicsLinearVelocity() / 100.0;
    const FVector Omega = Body->GetPhysicsAngularVelocityInRadians();
    const auto *Mode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    if(Mode && Mode->IsCoastLevel() && P.Y>-3000 && P.X>90000)
    {
        if(P.X>96000)
        {
            UE_LOG(LogTemp,Display,TEXT("EasternNoFlyBoundary: returning to launch"));
            ResetFlight();return;
        }
    }
    // Flight-safety reset: the velocity/angular-rate caps are configurable
    // (0 = off, 1 = very low / acro-friendly, 2 = normal). NaN/Inf and the hard
    // world bounds always stay — those are genuine glitch guards, not acro trips.
    const int32 Safety = FMath::Clamp(FMath::RoundToInt(FlightSafety), 0, 2);
    double SafetySpeed = 120.0, SafetyOmega = 40.0;  // normal
    if (Safety == 1) { SafetySpeed = 180.0; SafetyOmega = 55.0; }  // very low (more acro)
    const bool bSpeedReset = Safety > 0 && (V.Size() > SafetySpeed || Omega.Size() > SafetyOmega);
    const bool bVoid = Mode && Mode->IsVoidLevel();
    const double FloorZ = bVoid ? -4500.0 : (Mode ? Mode->GroundHeight(P.X, P.Y) : 0.0) - 300.0;
    const double CeilZ = bVoid ? 15000.0 : 300000.0;
    const double BoundR = bVoid ? 40000.0 : 480000.0;
    if (!Finite(P) || !Finite(V) || !Finite(Omega) || bSpeedReset ||
        P.Z < FloorZ || P.Z > CeilZ || P.Size2D() > BoundR)
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
    {
        if (bFlying)   // was airborne → touchdown sand puff
            EmitSandBurst(P, 1.0f);
        bFlying = false;
    }
    else if (GetAltitude() > .5f && GetSpeed() > 1.f)
        bFlying = true;
    UpdateLandingCamera(Dt);
    const double FieldRadius = Mode && Mode->IsDesertLevel() ? 185000. : Mode && Mode->IsVoidLevel() ? 20000. : (Mode && Mode->IsNatureLevel() ? 190000. : 30000.);
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
    // Only the RC owns the sticks while a transmitter is actually connected.
    // When RC mode is enabled but the radio is off/disconnected, the gate emits
    // all-zero channels and this override would silently pin throttle to 0 —
    // which is why the keyboard throttle looked dead after RC selection was
    // persisted. Falling back to the keyboard in that state fixes it.
    if (RcController && RcController->IsEnabled() && RcController->IsConnected())
    {
        const auto &Channels = RcController->GetChannels();
        Throttle = Channels[0];
        RollInput = Channels[1];
        PitchInput = Channels[2];
        YawInput = Channels[3];
        Desktop.speedModifier = FMath::Clamp(double(Channels[4]) * 0.5 + 0.5, 0.0, 1.0);
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
        // Aether buoyancy: a gentle spring toward the ring plane holds the turtle
        // in the volume (neutrally buoyant, not anchored). No preferred "up" is
        // imposed — the pilot still swims and rolls freely in 3D.
        if (Mode && Mode->IsVoidLevel())
        {
            const float DepthM = float(P.Z - 600.0) / 100.f;   // 600 cm = sacred-tree mid-ring
            Body->AddForce(FVector(0.f, 0.f, -DepthM * 0.4f * Body->GetMass()));
        }
    }
    // Ground repositioning: a deliberate downstroke shoves the grounded bird
    // forward along its heading so the wings can creep it into place for the
    // next throw (complementing the wing-tip lever in SweepWingColliders).
    if (!bFlying && Throttle > 0.05f)
    {
        const float FlapDelta = LeftFlap - PrevLeftFlap;   // deg this frame, + = rising
        const float DownRate = Dt > 0.f ? FMath::Max(0.f, -FlapDelta) / Dt : 0.f;
        if (DownRate > 8.f)
        {
            const FVector Heading = FRotator(0.f, float(Body->GetComponentRotation().Yaw), 0.f).Vector();
            const float Push = FMath::Clamp(DownRate * 0.06f, 0.f, 45.f);
            Body->AddImpulse(Heading * (Body->GetMass() * Push));
        }
    }
    VisualRoot->SetRelativeRotation(FRotator::ZeroRotator);
    // Two independent mount trims:
    //   * flap_centre_deg  → FLAP-STROKE-CENTRE (dihedral) trim. It shifts the
    //     servo stroke centre and is ALREADY visible in LeftFlap/RightFlap (the
    //     Roll slot below). No extra pitch here.
    //   * vertical_mount_angle_deg → REAL wing incidence (nose-up +). Applied as
    //     a shoulder PITCH (rotation about the span axis) so the wing visibly
    //     tilts leading-edge-up when positive. This is the knob the solver feeds
    //     into the strip's geometric twist, so visual and physics now share one
    //     axis and sign.
    // Grounded anti-sink: hold the wings near the +16° neutral dihedral so a
    // residual flap cannot dip the tips below the body plane and into the sand.
    // The clamp only bites once the bird is settled (not while flying), so it
    // never fights the aeroelastic stroke during a landing approach.
    const float MountPitch = float(Tuning.vertical_mount_angle_deg);
    float FlapL = LeftFlap, FlapR = RightFlap;
    if (!bFlying && Throttle < 0.05f)
    {
        // At rest, hold the wings level (no artificial dihedral offset) so the
        // rendered wing matches the physics stroke angle exactly. When throttling
        // on the ground (repositioning), let the full stroke show.
        FlapL = FMath::Clamp(LeftFlap, -12.f, 12.f);
        FlapR = FMath::Clamp(RightFlap, -12.f, 12.f);
    }
    if (RavenLeftShoulder) RavenLeftShoulder->SetRelativeRotation(FRotator(MountPitch, 0, FlapL));
    if (RavenRightShoulder) RavenRightShoulder->SetRelativeRotation(FRotator(MountPitch, 0, -FlapR));
    LeftShoulder->SetRelativeRotation(FRotator(MountPitch, 0, FlapL));
    RightShoulder->SetRelativeRotation(FRotator(MountPitch, 0, -FlapR));
    if(MembraneLeftShoulder) MembraneLeftShoulder->SetRelativeRotation(LeftShoulder->GetRelativeRotation());
    if(MembraneRightShoulder) MembraneRightShoulder->SetRelativeRotation(RightShoulder->GetRelativeRotation());
    if(PterosaurLeftShoulder) PterosaurLeftShoulder->SetRelativeRotation(LeftShoulder->GetRelativeRotation());
    if(PterosaurRightShoulder) PterosaurRightShoulder->SetRelativeRotation(RightShoulder->GetRelativeRotation());
    if(TurtleLeftShoulder) TurtleLeftShoulder->SetRelativeRotation(LeftShoulder->GetRelativeRotation());
    if(TurtleRightShoulder) TurtleRightShoulder->SetRelativeRotation(RightShoulder->GetRelativeRotation());
    // Read the physics mount angle. The Kestrel fan remains fixed at the
    // configured incidence; steering/throttle do not animate a hidden servo.
    double TailPitch = 0;
    if (MathBridge) MathBridge->ReadTailIncidence(TailPitch);
    if (RavenTailPivot) RavenTailPivot->SetRelativeRotation(FRotator(TailPitch, 0, 0));
    if (PterosaurTailPivot) PterosaurTailPivot->SetRelativeRotation(FRotator(TailPitch, 0, 0));
    if (MembraneTailPivot) MembraneTailPivot->SetRelativeRotation(FRotator(TailPitch, 0, 0));
    if (TurtleTailPivot) TurtleTailPivot->SetRelativeRotation(FRotator(TailPitch, 0, 0));
    if (bFlightTest && FParse::Param(FCommandLine::Get(), TEXT("B2FTailAngleTest")) && TestTime >= 18)
    {
        const int32 SampleBit = TestTime < 19.4 ? 1 : 2;
        if (!(TailAngleTestSamples & SampleBit))
        {
            const double Expected = SampleBit == 1 ? -8 : 8;
            const double Visible = MembraneTailPivot ? MembraneTailPivot->GetRelativeRotation().Pitch : 999;
            const bool Pass = FMath::Abs(TailPitch - Expected) < 1.e-5 && FMath::Abs(Visible - Expected) < 1.e-5;
            UE_LOG(LogTemp, Display, TEXT("TailAngleTest %s setting=%.1f solver=%.3f visible=%.3f"),
                Pass ? TEXT("PASS") : TEXT("FAIL"), Expected, TailPitch, Visible);
            TailAngleTestSamples |= SampleBit;
            if (!Pass) FPlatformMisc::RequestExitWithStatus(false, 1);
        }
    }
    B2F_WingSection LeftShape[B2F_WING_STATIONS], RightShape[B2F_WING_STATIONS];
    if(MathBridge && MathBridge->ReadWingShape(LeftShape,RightShape))
    {
        for(USceneComponent* Shoulder : {LeftShoulder.Get(),RightShoulder.Get(),RavenLeftShoulder.Get(),RavenRightShoulder.Get(),MembraneLeftShoulder.Get(),MembraneRightShoulder.Get(),TurtleLeftShoulder.Get(),TurtleRightShoulder.Get()})
            if(Shoulder)
                for(USceneComponent* Child : Shoulder->GetAttachChildren())
                    if(auto* Wing=Cast<UBorn2FlapWingMesh>(Child))
                        Wing->ApplyShape((Shoulder==LeftShoulder || Shoulder==RavenLeftShoulder || Shoulder==MembraneLeftShoulder || Shoulder==TurtleLeftShoulder) ? LeftShape : RightShape);
    }
    SweepWingColliders(Dt);
    UpdateAeroAudio(Dt);
    UpdateWindMotes(Dt);
    UpdateWaterRipples(Dt);
    if (bVectors)
    {
        // Force arrows begin at the actual deformed strip, never at a common
        // wing centroid. All force arrows share one linear scale (cm / N).
        const FQuat Q = Body->GetComponentQuat();
        const FVector Origin = Body->GetComponentLocation();
        const FVector Fwd = Q.RotateVector(FVector::ForwardVector);
        const auto WorldVector = [&Q](const double* V) { return Q.RotateVector(FVector(V[0], V[1], V[2])); };
        constexpr float ForceScale = 800.f;
        const FColor LiftColor(70, 145, 255), ThrustColor(80, 255, 100);
        const FColor PressureColor(50, 235, 240), DragColor(255, 155, 45);
        const FColor FlowColor(255, 230, 70), MomentColor(235, 90, 235);
        const auto Arrow = [this](const FVector& StripOrigin, const FVector& V, FColor Color, float Scale, float Thickness)
        {
            if (!V.IsNearlyZero(1.e-8))
                DrawDebugDirectionalArrow(GetWorld(), StripOrigin, StripOrigin + V * Scale,
                    FMath::Clamp(float(V.Size() * Scale * .18), .08f, 5.f), Color, false, 0, 1, Thickness);
        };
        B2F_WingField Fields[2][B2F_WING_STATIONS]{};
        B2F_WingLoads Loads[2][B2F_WING_STATIONS]{};
        if (MathBridge && MathBridge->ReadWingFields(Fields[0], Fields[1]) &&
            MathBridge->ReadWingLoads(Loads[0], Loads[1]))
        {
            FVector WingLift[2]{}, WingForce[2]{}, WingMoment[2]{};
            for (int32 W = 0; W < 2; ++W)
                for (uint32 I = 0; I < B2F_WING_STATIONS; ++I)
                {
                    const auto& Strip = Fields[W][I];
                    const auto& Load = Loads[W][I];
                    const FVector StripOrigin = Origin + WorldVector(Strip.position_m) * 100.0;
                    const FVector Lift = WorldVector(Strip.lift_n);
                    const FVector Force = WorldVector(Load.force_n);
                    WingLift[W] += FVector(Strip.lift_n[0], Strip.lift_n[1], Strip.lift_n[2]);
                    WingForce[W] += FVector(Load.force_n[0], Load.force_n[1], Load.force_n[2]);
                    WingMoment[W] += FVector(Load.moment_nm[0], Load.moment_nm[1], Load.moment_nm[2]);
                    DrawDebugPoint(GetWorld(), StripOrigin, 3.f, FColor::White, false, 0, 1);
                    if (VectorView == 1)
                    {
                        Arrow(StripOrigin, Force, FColor::White, ForceScale, .7f);
                        Arrow(StripOrigin, Lift, LiftColor, ForceScale, 1.6f);
                        // Forward projection of the resultant: green can point
                        // backwards in glide. It is not an extra thrust source.
                        Arrow(StripOrigin, Fwd * Load.force_n[0], ThrustColor, ForceScale, 1.3f);
                    }
                    else
                    {
                        Arrow(StripOrigin, WorldVector(Strip.pressure_n), PressureColor, ForceScale, 1.4f);
                        Arrow(StripOrigin, WorldVector(Load.drag_n), DragColor, ForceScale, 1.4f);
                        // Air moving relative to the strip is opposite the
                        // solver's surface-through-air velocity. Keep its own scale.
                        Arrow(StripOrigin, -WorldVector(Strip.air_velocity_m_s), FlowColor, 2.f, .7f);
                        Arrow(StripOrigin, WorldVector(Load.moment_nm), MomentColor, 2500.f, .9f);
                        DrawDebugLine(GetWorld(), StripOrigin, StripOrigin + WorldVector(Strip.normal) * 6.f,
                            FColor::White, false, 0, 1, .5f);
                    }
                }
            // Aggregate arrows use a separate labelled scale to stay readable.
            const FVector Hub = Origin + Q.RotateVector(FVector(0, 0, 45));
            DrawDebugSphere(GetWorld(), Body->GetCenterOfMass(), 2.5f, 12, FColor::Orange, false, 0, 0, 1.5f);
            Arrow(Hub, AeroForce, FColor::White, 100.f, 2.f);
            Arrow(Hub, FVector(0, 0, -Body->GetMass() * 9.81), FColor::Red, 100.f, 2.f);
            VectorLegend = VectorView == 1
                ? TEXT("F5 1/2 | WING + TAIL PATCHES | Blue: lift   Green: forward force   White: resultant | 800 cm/N")
                : TEXT("F5 2/2 | Cyan: pressure   Orange: drag (800 cm/N)   White: surface normal");
            VectorDetail = VectorView == 1
                ? TEXT("Lift follows local airflow; forward force can point backwards. Thrust is a component, not an extra force.")
                : TEXT("Yellow: relative airflow (2 cm per m/s)   Magenta: torque axis about body origin (2500 cm/Nm)");
            VectorTotals = FString::Printf(TEXT("Instantaneous L/R  |  Lift magnitude %.3f / %.3f N   Forward %+.3f / %+.3f N   Roll torque %+.4f / %+.4f Nm"),
                WingLift[0].Size(), WingLift[1].Size(), WingForce[0].X, WingForce[1].X, WingMoment[0].X, WingMoment[1].X);
            VectorAverage = FString::Printf(TEXT("Smoothed 0.5 s L/R | Vertical lift %+.3f / %+.3f N   Wing roll moment %+.4f / %+.4f Nm"),
                SmoothedStripStats[0].X, SmoothedStripStats[1].X, SmoothedStripStats[0].Z, SmoothedStripStats[1].Z);
            const double YawRate = Q.UnrotateVector(Body->GetPhysicsAngularVelocityInRadians()).Z;
            const TCHAR* Outside = YawRate > .02 ? TEXT("LEFT") : YawRate < -.02 ? TEXT("RIGHT") : TEXT("no turn");
            VectorFlow = FString::Printf(TEXT("Smoothed forward airspeed L/R %.2f / %.2f m/s | Outside wing: %s | Upright: +roll moment raises RIGHT"),
                SmoothedStripStats[0].Y, SmoothedStripStats[1].Y, Outside);
            B2F_TailField TailFields[B2F_TAIL_PATCHES]{};
            VectorTail = TEXT("Tail patch diagnostics unavailable for this model/backend.");
            if (MathBridge->ReadTailFields(TailFields))
            {
                FVector TailForceWorld = FVector::ZeroVector, TailMomentWorld = FVector::ZeroVector;
                for (const auto& Patch : TailFields)
                {
                    const FVector PatchOrigin = Origin + WorldVector(Patch.position_m) * 100.0;
                    const FVector PatchLift = WorldVector(Patch.lift_n);
                    const FVector PatchPressure = WorldVector(Patch.pressure_n);
                    const FVector PatchDrag = WorldVector(Patch.drag_n);
                    const FVector PatchForce = PatchLift + PatchPressure + PatchDrag;
                    TailForceWorld += PatchForce;
                    TailMomentWorld += WorldVector(Patch.moment_nm);
                    DrawDebugPoint(GetWorld(), PatchOrigin, 3.f, FColor::White, false, 0, 1);
                    if (VectorView == 1)
                    {
                        Arrow(PatchOrigin, PatchForce, FColor::White, ForceScale, .7f);
                        Arrow(PatchOrigin, PatchLift, LiftColor, ForceScale, 1.6f);
                    }
                    else
                    {
                        Arrow(PatchOrigin, PatchLift, LiftColor, ForceScale, 1.6f);
                        Arrow(PatchOrigin, PatchPressure, PressureColor, ForceScale, 1.4f);
                        Arrow(PatchOrigin, PatchDrag, DragColor, ForceScale, 1.4f);
                        Arrow(PatchOrigin, -WorldVector(Patch.air_velocity_m_s), FlowColor, 2.f, .7f);
                    }
                }
                VectorTail = FString::Printf(TEXT("Fixed tail %+0.1f deg | World vertical force %+.3f N | World yaw moment %+.4f Nm"),
                    TailPitch, TailForceWorld.Z, TailMomentWorld.Z);
            }
        }
        else
        {
            VectorLegend = TEXT("F5 strip diagnostics unavailable: restart with the updated physics backend.");
            VectorDetail.Empty(); VectorTotals.Empty(); VectorAverage.Empty(); VectorFlow.Empty(); VectorTail.Empty();
        }
    }
    LogTime += DeltaSeconds;
    if (LogTime >= 1)
    {
        LogTime = 0;
        UE_LOG(LogTemp, Display,
               TEXT("FlightTelemetry mode=aerodynamic flying=%d healthy=%d altitude=%.3fm speed=%.3fm/s climb=%.3fm/s "
                    "pitch=%.2f roll=%.2f yaw=%.1f throttle=%.3f soc=%.3f aeroN=%s torqueNm=%s rc=(%.3f,%.3f,%.3f) "
                    "flap=(%.1f,%.1f) throttleSource=%s speedMod=%.3f coupled=%d"),
               bFlying, bHealthy, GetAltitude(), GetSpeed(), GetClimbRate(), Body->GetComponentRotation().Pitch,
               Body->GetComponentRotation().Roll, Body->GetComponentRotation().Yaw, Throttle, BatterySoc,
               *AeroForce.ToString(), *AeroMoment.ToString(), RollInput, PitchInput, YawInput, LeftFlap, RightFlap,
               RcController && RcController->IsEnabled() ? TEXT("RC") : TEXT("keys"), Desktop.speedModifier,
               bCoupledThrottle ? 1 : 0);
    }
    if (bFlightTest)
        CheckFlightTest();
    if (bDesktopInputTest)
        CheckDesktopInputTest(DeltaSeconds);
}

#if PLATFORM_IOS || PLATFORM_ANDROID
// Mobile multi-touch gesture recogniser (polled each tick via GetInputTouchState,
// so it never fights the virtual joysticks for touch ownership). Positions are
// normalized viewport coordinates (0..1). Recognises:
//   • single-finger drag                → view rotation
//   • two-finger same-direction drag    → walk sideways (strafe)
//   • two-finger symmetric pinch        → walk forward/back  (spread = zoom-in = closer)
//   • two-finger pivot (1 still)        → walk in the moving finger's direction
void ABorn2FlapFlightPawn::UpdateTouchGestures(float Dt, APlayerController* PC)
{
    GestureViewYaw = GestureViewPitch = GestureWalkForward = GestureWalkStrafe = 0.f;
    if (!PC)
        return;

    // Free-look zone: the middle band of the screen, clear of the twin virtual
    // joysticks (bottom corners) and the HUD readout/action bar (top edge).
    const float ZoneTop = 0.18f, ZoneBottom = 0.78f;

    float X[2] = { 0.f, 0.f }, Y[2] = { 0.f, 0.f };
    bool Down[2] = { false, false };
    PC->GetInputTouchState(ETouchIndex::Touch1, X[0], Y[0], Down[0]);
    PC->GetInputTouchState(ETouchIndex::Touch2, X[1], Y[1], Down[1]);

    // GetInputTouchState returns viewport-relative PIXEL coordinates (0..width,
    // 0..height), NOT normalized 0..1 — normalize before the zone test and the
    // 220°/150° full-drag gains, otherwise the zone never matches and the look
    // deltas are astronomically large. Without this the gestures are dead.
    int32 VW = 0, VH = 0;
    PC->GetViewportSize(VW, VH);
    if (VW <= 0 || VH <= 0)
        return;
    const float InvW = 1.f / VW, InvH = 1.f / VH;

    FVector2D Delta[2] = { FVector2D::ZeroVector, FVector2D::ZeroVector };
    bool Moved[2] = { false, false };

    for (int32 I = 0; I < 2; ++I)
    {
        X[I] *= InvW;
        Y[I] *= InvH;
        const bool bInZone = Down[I] && Y[I] > ZoneTop && Y[I] < ZoneBottom;
        const FVector2D Cur(X[I], Y[I]);
        if (bInZone && !GestureTracked[I])
        {
            GestureTracked[I] = true;
            GesturePrev[I] = Cur;   // entering the zone: no delta this frame
        }
        else if (!bInZone && GestureTracked[I])
        {
            GestureTracked[I] = false;   // lifted / left the zone
        }
        else if (bInZone && GestureTracked[I])
        {
            const FVector2D D = Cur - GesturePrev[I];
            GesturePrev[I] = Cur;
            if (D.SizeSquared() > 1e-8f)
            {
                Delta[I] = D;
                Moved[I] = true;
            }
        }
    }

    const int32 ActiveCount = (GestureTracked[0] ? 1 : 0) + (GestureTracked[1] ? 1 : 0);
    if (ActiveCount < 2)
    {
        // Single-finger drag → look. (Normalized → degrees: full screen ≈ 220°.)
        const FVector2D D = Moved[0] ? Delta[0] : (Moved[1] ? Delta[1] : FVector2D::ZeroVector);
        GestureViewYaw = D.X * 220.f;
        GestureViewPitch = D.Y * 150.f;
        return;
    }

    const FVector2D D0 = Moved[0] ? Delta[0] : FVector2D::ZeroVector;
    const FVector2D D1 = Moved[1] ? Delta[1] : FVector2D::ZeroVector;
    const bool bBothMoved = Moved[0] && Moved[1];
    const bool bOneMoved = Moved[0] != Moved[1];

    if (bOneMoved)
    {
        // Pivot: the moving finger's travel, decomposed against the line to the
        // still finger — radial = forward/back, tangential = strafe.
        const FVector2D D = Moved[0] ? D0 : D1;
        const FVector2D Anchor = Moved[0] ? GesturePrev[1] : GesturePrev[0];
        const FVector2D Moving = Moved[0] ? GesturePrev[0] : GesturePrev[1];
        FVector2D Line = Anchor - Moving;
        const float Len = Line.Size();
        if (Len > 1e-4f)
        {
            Line /= Len;
            const FVector2D Perp(-Line.Y, Line.X);
            const float Radial = FVector2D::DotProduct(D, Line);   // toward still = forward
            const float Tangent = FVector2D::DotProduct(D, Perp);
            GestureWalkForward += Radial * 600.f;    // cm
            GestureWalkStrafe  += Tangent * 600.f;   // cm
        }
    }
    else if (bBothMoved)
    {
        const float Dot = FVector2D::DotProduct(D0.GetSafeNormal(), D1.GetSafeNormal());
        if (Dot > 0.35f)
        {
            // Two fingers dragging the SAME direction → walk sideways (strafe).
            // The horizontal component of the shared motion picks left vs right.
            const FVector2D Pan = (D0 + D1) * 0.5f;
            GestureWalkStrafe += Pan.X * 900.f;   // cm
        }
        else
        {
            // Symmetric pinch → walk forward/back (speed ∝ the zoom amount).
            // Spread (zoom-in) walks closer; pinch-together (zoom-out) walks back.
            const FVector2D CurA(X[0], Y[0]), CurB(X[1], Y[1]);
            const FVector2D PrevA = CurA - D0, PrevB = CurB - D1;
            const float NewDist = (CurA - CurB).Size();
            const float OldDist = (PrevA - PrevB).Size();
            const float Pinch = NewDist - OldDist;   // + = spread (zoom-in)
            GestureWalkForward += Pinch * 800.f;     // cm (zoom-in → closer)
        }
    }
}
#endif

void ABorn2FlapFlightPawn::UpdateWindMotes(float DeltaSeconds)
{
    if (!WindMotes || MotePositions.Num() == 0)
        return;
    const auto* Mode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    const FVector Cam = Camera ? Camera->GetComponentLocation() : Body->GetComponentLocation();
    const FVector Fwd = Camera ? Camera->GetForwardVector() : FVector::ForwardVector;
    for (int32 I = 0; I < MotePositions.Num(); ++I)
    {
        FVector P = MotePositions[I];
        FVector Wind = Mode ? Mode->WindAt(P, WorldTime) : Born2FlapWind::Sample(P, WorldTime);
        Wind.Z = 0.0; // motes ride the horizontal stream, not thermals/lift
        P += Wind * 100.0 * MoteSpeed[I] * DeltaSeconds;
        // Recycle strays: keep a loose cloud around the camera so the stream is
        // continuous without ever clumping in one spot.
        if (!Finite(P) || FVector(P - Cam).Size() > 2600.0)
        {
            P = Cam + Fwd * MoteRandom.FRandRange(150.f, 1900.f) +
                FVector(MoteRandom.FRandRange(-2100.f, 2100.f), MoteRandom.FRandRange(-2100.f, 2100.f),
                        MoteRandom.FRandRange(-500.f, 1000.f));
            MoteScales[I] = MoteRandom.FRandRange(0.012f, 0.028f);
        }
        MotePositions[I] = P;
        WindMotes->UpdateInstanceTransform(I, FTransform(FQuat::Identity, P, FVector(MoteScales[I])), false, false, false);
    }
    WindMotes->MarkRenderStateDirty();
}

void ABorn2FlapFlightPawn::DrawVectorOverlay(UCanvas* Canvas, APlayerController* DebugController)
{
    if (!bVectors || !Canvas || !GEngine || (DebugController && DebugController->GetPawn() != this)) return;
    // A dedicated lower panel avoids the instrument readouts at the top left.
    const TArray<FString> Lines{VectorLegend, VectorDetail, VectorTotals, VectorAverage, VectorFlow, VectorTail,
        FString::Printf(TEXT("CG %+.1f cm (orange marker; +aft) | CG pitch-moment correction %+.4f Nm (+nose-down)"),
            CgOffsetMm / 10., CgPitchMomentCorrection),
        TEXT("Body arrows: white = host force, red = gravity (100 cm/N). F5: forces > airflow/torque > off.")};
    UFont* Font = GEngine->GetSmallFont();
    float MaxWidth = 1.f, TextHeight = 0.f;
    for (const FString& Line : Lines)
    {
        float Width = 0.f;
        Canvas->StrLen(Font, Line, Width, TextHeight);
        MaxWidth = FMath::Max(MaxWidth, Width);
    }
    const float Scale = FMath::Min(1.f, (Canvas->SizeX - 48.f) / MaxWidth);
    const float Row = FMath::Max(16.f, TextHeight * Scale + 3.f);
    const float Top = Canvas->SizeY - Row * Lines.Num() - 24.f;
    FCanvasTileItem Backdrop(FVector2D(12.f, Top - 8.f),
        FVector2D(FMath::Min(MaxWidth * Scale + 24.f, Canvas->SizeX - 24.f), Row * Lines.Num() + 16.f),
        FLinearColor(.012f, .018f, .025f, .88f));
    Backdrop.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Backdrop);
    for (int32 I = 0; I < Lines.Num(); ++I)
    {
        FCanvasTextItem Text(FVector2D(24.f, Top + I * Row), FText::FromString(Lines[I]), Font, FLinearColor::White);
        Text.Scale = FVector2D(Scale, Scale);
        Text.EnableShadow(FLinearColor::Black);
        Canvas->DrawItem(Text);
    }
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
    {
        // ResetFlight returns to the selected launch point, which is far from
        // world origin on authored maps such as Murex's runway head.
        const FVector ExpectedSpawn = POIs.IsValidIndex(SelectedPoi)
            ? POIs[SelectedPoi].Position : FVector::ZeroVector;
        bTestReset = !bFlying && (Body->GetComponentLocation() - ExpectedSpawn).Size2D() < 100 && GetAltitude() < .3;
    }
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
