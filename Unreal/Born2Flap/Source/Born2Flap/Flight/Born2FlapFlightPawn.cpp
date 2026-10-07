#include "Flight/Born2FlapFlightPawn.h"
#include "Flight/Born2FlapWingMesh.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"
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
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "UObject/ConstructorHelpers.h"
#include "Input/Born2FlapRcController.h"
#include "Input/Born2FlapKeybinds.h"
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
    Tuning.tail_elevator_angle_deg = 0;
    Tuning.glide_angle_deg = -4;
    Tuning.stroke_ferocity = 50;
    Tuning.aileron_scale = 10;
    Tuning.elevator_scale = 55;
    Tuning.mount_angle_deg = 0;
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
    case ETuningField::FlapBaseFreq:      return FMath::Clamp(Value, 10.f, 200.f);
    case ETuningField::TailElevatorAngle:    return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::GlideAngle:        return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::StrokeFerocity:    return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::AileronScale:      return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::ElevatorScale:     return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::MountAngle:        return FMath::Clamp(Value, -15.f, 15.f);
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
    case ETuningField::TailElevatorAngle:    return float(Tuning.tail_elevator_angle_deg);
    case ETuningField::GlideAngle:        return float(Tuning.glide_angle_deg);
    case ETuningField::StrokeFerocity:    return float(Tuning.stroke_ferocity);
    case ETuningField::AileronScale:      return float(Tuning.aileron_scale);
    case ETuningField::ElevatorScale:     return float(Tuning.elevator_scale);
    case ETuningField::MountAngle:        return float(Tuning.mount_angle_deg);
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
    case ETuningField::TailElevatorAngle:    Tuning.tail_elevator_angle_deg = Value; break;
    case ETuningField::GlideAngle:        Tuning.glide_angle_deg = Value; break;
    case ETuningField::StrokeFerocity:    Tuning.stroke_ferocity = Value; break;
    case ETuningField::AileronScale:      Tuning.aileron_scale = Value; break;
    case ETuningField::ElevatorScale:     Tuning.elevator_scale = Value; break;
    case ETuningField::MountAngle:        Tuning.mount_angle_deg = Value; break;
    default: return;
    }
    ApplyTuning();
}
void ABorn2FlapFlightPawn::ApplyTuning()
{
    // The airframe mass drives the physics wing scale (small bird → small wing,
    // so a micro servo's torque is enough to flap it).
    Tuning.body_mass_kg = BodyMassKg;
    if (MathBridge && MathBridge->IsReady())
        MathBridge->Reconfigure(Tuning);
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
    if (GameMode && GameMode->IsCoastLevel())
        return 2;                       // Shiomori Bay -> common kestrel (falcon)
    if (GameMode && !GameMode->IsNatureLevel())
        return 1;                       // Training -> Prototype
    return 0;                           // Ravenstonefield / nature -> RavenCrow
}
void ABorn2FlapFlightPawn::BeginPlay()
{
    Super::BeginPlay();
    if (AudioSynth)
        AudioSynth->Start();
    PanelKeyProcessor = MakeShared<FPanelKeyInputProcessor>();
    if (FSlateApplication::IsInitialized())
        FSlateApplication::Get().RegisterInputPreProcessor(PanelKeyProcessor);
    ApplyBodyMass();
    UE_LOG(LogTemp, Display, TEXT("FlightBody massKg=%.4f inertiaKgM2=%s"), Body->GetMass(),
           *(Body->GetInertiaTensor() / 10000.0).ToString());
    // Weather owns sky optics and exposure for all cameras in the world.
    Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = false;
    Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = false;
    Camera->PostProcessSettings.bOverride_AutoExposureBias = false;
    if (SkyDome) SkyDome->SetVisibility(false);
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
    if (BirdModel < 0)
        BirdModel = DefaultBirdModel();   // no explicit menu choice yet → level default
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
    SetBlindFlight(bBlind);
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
    GroundSettleTime = 0;
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
            MembraneLeftShoulder.Get(), MembraneRightShoulder.Get() })
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

        // Wingtip ground contact is a planted PIVOT, not a bouncy spring. Two parts:
        // (1) Resolve the contact point's inward velocity (restitution ~0.15) so the
        //     tip settles onto the surface instead of rebounding; applied AT the tip,
        //     the impulse makes the body lever around it.
        // (2) While the wing is being driven DOWNWARD into the ground (a deliberate
        //     downstroke — even a slow, elevator-driven one), add a ground-reaction
        //     impulse at the contact point proportional to the downstroke rate.
        //     Because the tip is aft of the centre of mass, that reaction lifts the
        //     bird AND pitches the nose up, letting it stand on its wingtips like a
        //     real bird. No spar-bend model is needed: the rigid lever arm between
        //     the contact point and the CoM already produces the pitch.
        if (bTouched && !bWingTouching)
        {
            if (GEngine)
                GEngine->AddOnScreenDebugMessage(42, 0.6f, FColor(255, 92, 64), TEXT("WING HIT"));
        }
        if (bTouched)
        {
            // (1) Plant: softly cancel inward velocity (no overshoot, hard-capped).
            const FVector ContactVel = Body->GetPhysicsLinearVelocityAtPoint(ContactPoint);
            const float   Approach   = FVector::DotProduct(ContactVel, ContactNormal); // - = into surface
            if (Approach < 0.f)
            {
                const float Kill = FMath::Min(-Approach * 0.85f, 60.f);
                Body->AddImpulseAtLocation(ContactNormal * (Body->GetMass() * Kill), ContactPoint);
            }
    
            // (2) Downstroke lever: reaction force scales with the flap-down rate.
            const float FlapDelta = LeftFlap - PrevLeftFlap;                 // deg this frame
            const float DownRate  = Dt > 0.f ? FMath::Max(0.f, -FlapDelta) / Dt : 0.f; // °/s downstroke
            const FVector V  = Body->GetPhysicsLinearVelocity();
            const float   Vn = FVector::DotProduct(V, ContactNormal);        // + = leaving surface
            if (DownRate > 8.f && Vn < 25.f &&
                World->GetTimeSeconds() - LastWingPushTime > 0.12)
            {
                // Slow deliberate stroke (~40°/s) → ~2.4 cm/s lever; a hard flap
                // (~600°/s) → ~36 cm/s shove. Applied AT the contact point, the
                // impulse rotates the body (nose up) rather than just translating.
                const float Push = FMath::Clamp(DownRate * 0.06f, 0.f, 36.f);
                Body->AddImpulseAtLocation(ContactNormal * (Body->GetMass() * Push), ContactPoint);
                LastWingPushTime = World->GetTimeSeconds();
            }
        }
        bWingTouching = bTouched;
}
void ABorn2FlapFlightPawn::LaunchFlight()
{
    if (!bHealthy || (!bCameraGrounded && GetAltitude() > .4f) || GetSpeed() > 1.f)
        return;
    // One explicit hand launch supplies initial momentum; it cannot repeat in
    // the air. Every subsequent acceleration comes from aero forces/gravity.
    FRotator Heading(0, Body->GetComponentRotation().Yaw, 0);
    ChaseOrbitYaw = 0.f; ChaseOrbitPitch = -12.f;
    GroundLookYaw = 0.f; GroundLookPitch = 0.f;
    GroundZoom = 1.f; ChaseArmLength = 480.f;
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
    CurrentWind = bFlightTest ? FVector::ZeroVector : (WindMode?WindMode->WindAt(BodyPos,WorldTime):Born2FlapWind::Sample(BodyPos,WorldTime));
    const FVector Wind = CurrentWind * Shelter;
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
        // CG authority: the firmware resolves the aerodynamic moment about the
        // body origin. Shift it to the actual centre of mass so a fore/aft CG
        // offset produces a real pitching moment (M_cg = M - r_cg x F). The CG
        // is + = aft, and aft is -X in the body frame, so r_cg = (-cg, 0, 0).
        const double CgM = double(CgOffsetMm) / 1000.0;   // metres
        const FVector Mcg(M.X, M.Y - CgM * F.Z, M.Z + CgM * F.Y);
        Valid = Valid && Finite(F) && Finite(M) && Finite(Mcg) && F.Size() < 100 && M.Size() < 50 &&
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
        const FVector WorldMoment = Rotation.RotateVector(Mcg);
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
    Tel.wingbeat_hz = MeasuredWingbeatHz > 0.5 ? MeasuredWingbeatHz : (2.0 + 5.0 * Throttle);
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
        // Voice 1 is "wing" — the wingbeat. Scale it alone so the flap can be
        // heard over wind/surf/music without touching the rest of the mix. The
        // wing is exempt from the onboard headroom cut (CameraGain) so it stays
        // prominent when the camera rides at the bird — that cut was what made
        // the wingbeat almost inaudible in FPV/chase.
        if (I == 1) Gain *= WingbeatVolume;
        else        Gain *= CameraGain;
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
        Steer = (PC->IsInputKeyDown(Get(TEXT("YawRight"))) ? 1.f : 0.f) - (PC->IsInputKeyDown(Get(TEXT("YawLeft"))) ? 1.f : 0.f);
        Roll = (PC->IsInputKeyDown(Get(TEXT("RollRight"))) ? 1.f : 0.f) - (PC->IsInputKeyDown(Get(TEXT("RollLeft"))) ? 1.f : 0.f);
        Pitch = (PC->IsInputKeyDown(Get(TEXT("PitchUp"))) ? 1.f : 0.f) -
                (PC->IsInputKeyDown(Get(TEXT("PitchDown"))) || PC->IsInputKeyDown(Get(TEXT("PitchDownAlt"))) ? 1.f : 0.f);
        Launch = PC->WasInputKeyJustPressed(Get(TEXT("Launch")));
        Reset = PC->WasInputKeyJustPressed(Get(TEXT("Reset")));
        if (RcController)
        {
            Launch = (Launch || RcController->LaunchPressed()) && !RcController->IsPanelOpen();
            Reset |= RcController->ResetPressed();
        }
        if (PC->WasInputKeyJustPressed(Get(TEXT("ToggleVectors"))))
            bVectors = !bVectors;
        if (PC->WasInputKeyJustPressed(Get(TEXT("ToggleBlind"))))
            SetBlindFlight(!bBlind);
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
    bWalkLookActive = bGroundWalk;
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
            const float WalkSpeed = 220.f;  // cm/s
            if (bGroundView)
            {
                // Ground perspective: free-look (can turn away from the bird).
                // The invert flag mirrors the existing chase-orbit convention so
                // a single option can flip this free-look without touching flight.
                const float Inv = bFreeLookInvert ? -1.f : 1.f;
                GroundLookYaw -= Inv * MouseX * LookSens;
                GroundLookPitch -= Inv * MouseY * LookSens;
                GroundLookPitch = FMath::Clamp(GroundLookPitch, -80.f, 80.f);
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
                    if (Scroll > 0.f)
                    {
                        bFpvAirView = false;
                        if (PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
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
        // Neutralise the bird's own control surfaces while the pilot is walking.
        MouseX = MouseY = Wheel = 0.f;
        Effort = 0.f; Roll = Pitch = Steer = 0.f;
        WDown = false;
        Launch = Reset = false;
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
    // Flight-safety reset: the velocity/angular-rate caps are configurable
    // (0 = off, 1 = very low / acro-friendly, 2 = normal). NaN/Inf and the hard
    // world bounds always stay — those are genuine glitch guards, not acro trips.
    const int32 Safety = FMath::Clamp(FMath::RoundToInt(FlightSafety), 0, 2);
    double SafetySpeed = 120.0, SafetyOmega = 40.0;  // normal
    if (Safety == 1) { SafetySpeed = 180.0; SafetyOmega = 55.0; }  // very low (more acro)
    const bool bSpeedReset = Safety > 0 && (V.Size() > SafetySpeed || Omega.Size() > SafetyOmega);
    if (!Finite(P) || !Finite(V) || !Finite(Omega) || bSpeedReset ||
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
    // A 2-servo ornithopter has one actuator per wing: the flap hinge. The wing
    // rotates around that single axis; pitch and roll are already encoded in
    // LeftFlap/RightFlap (stroke-centre shift / differential), so there is no
    // separate shoulder-pitch (incidence) term.
    // Grounded anti-sink: hold the wings near the +16° neutral dihedral so a
    // residual flap cannot dip the tips below the body plane and into the sand.
    // The clamp only bites once the bird is settled (not while flying), so it
    // never fights the aeroelastic stroke during a landing approach.
    float FlapL = LeftFlap, FlapR = RightFlap;
    if (!bFlying && Throttle < 0.05f)
    {
        // At rest, hold the wings near the +16° neutral dihedral so a residual
        // flap cannot dip the tips below the body plane and into the sand. When
        // throttling on the ground (repositioning), let the full stroke show.
        FlapL = FMath::Clamp(LeftFlap, -12.f, 12.f);
        FlapR = FMath::Clamp(RightFlap, -12.f, 12.f);
    }
    if (RavenLeftShoulder) RavenLeftShoulder->SetRelativeRotation(FRotator(0, 0, FlapL + 16));
    if (RavenRightShoulder) RavenRightShoulder->SetRelativeRotation(FRotator(0, 0, -FlapR - 16));
    LeftShoulder->SetRelativeRotation(FRotator(0, 0, FlapL + 16));
    RightShoulder->SetRelativeRotation(FRotator(0, 0, -FlapR - 16));
    if(MembraneLeftShoulder) MembraneLeftShoulder->SetRelativeRotation(LeftShoulder->GetRelativeRotation());
    if(MembraneRightShoulder) MembraneRightShoulder->SetRelativeRotation(RightShoulder->GetRelativeRotation());
    // Tail-elevator trim, visible in the rendered bird: pitch the tail fan.
    const float TailPitch = Tuning.tail_elevator_angle_deg;
    if (RavenTailPivot) RavenTailPivot->SetRelativeRotation(FRotator(TailPitch, 0, 0));
    if (MembraneTailPivot) MembraneTailPivot->SetRelativeRotation(FRotator(TailPitch, 0, 0));
    B2F_WingSection LeftShape[B2F_WING_STATIONS], RightShape[B2F_WING_STATIONS];
    if(MathBridge && MathBridge->ReadWingShape(LeftShape,RightShape))
    {
        for(USceneComponent* Shoulder : {LeftShoulder.Get(),RightShoulder.Get(),RavenLeftShoulder.Get(),RavenRightShoulder.Get(),MembraneLeftShoulder.Get(),MembraneRightShoulder.Get()})
            if(Shoulder)
                for(USceneComponent* Child : Shoulder->GetAttachChildren())
                    if(auto* Wing=Cast<UBorn2FlapWingMesh>(Child))
                        Wing->ApplyShape((Shoulder==LeftShoulder || Shoulder==RavenLeftShoulder || Shoulder==MembraneLeftShoulder) ? LeftShape : RightShape);
    }
    SweepWingColliders(Dt);
    UpdateAeroAudio(Dt);
    UpdateWindMotes(Dt);
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