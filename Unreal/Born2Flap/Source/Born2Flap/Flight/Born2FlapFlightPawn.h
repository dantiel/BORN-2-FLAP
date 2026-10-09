#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Math/Born2FlapMathBridge.h"
#include "Input/Born2FlapRcController.h"
#include "born2flap_rc_input.h"
#include "born2flap_desktop_input.h"
#include "Game/Born2FlapPoi.h"
#include "Born2FlapFlightPawn.generated.h"
class UBoxComponent;
class USphereComponent;
class UCapsuleComponent;
class UBorn2FlapWingMesh;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class USceneComponent;
class USpringArmComponent;
class UCameraComponent;
class UBorn2FlapAudioSynth;
class ABorn2FlapFlightSettings;
class ABorn2FlapPoiOverlay;
class IInputProcessor;
// Live-tuning surface the hangar edits: servo, battery and the exposed
// controller knobs. Mirrors B2F_TuningConfig field order.
enum class ETuningField : uint8
{
    ServoSpeed, StallTorque, Backdrive, BatteryVoltage, BatteryResistance, BatteryCapacity,
    FlapBaseFreq, TailElevatorAngle, GlideAngle, StrokeFerocity, AileronScale, ElevatorScale,
    MountAngle, Count
};
// RC channels drive the native actuators. All forces and moments are aerodynamic.
UCLASS()
class BORN2FLAP_API ABorn2FlapFlightPawn : public APawn
{
    GENERATED_BODY()
  public:
    ABorn2FlapFlightPawn();
    virtual ~ABorn2FlapFlightPawn() override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
    void ToggleGroundView();
    void ToggleFpvView();
    bool IsFpvAirView() const { return bFpvAirView; }
    FString GetCameraLabel() const;
    void SelectBirdModel(int32 Index);
    int32 GetBirdModel() const { return BirdModel; }
    float GetTuning(ETuningField Field) const;
    void SetTuning(ETuningField Field, float Value);
    int32 DefaultBirdModel() const;
    UFUNCTION(BlueprintCallable, Category="Wing")
    void SetWingPaint(UTexture2D* Texture);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wing")
    TObjectPtr<UTexture2D> WingPaint;    FVector GetMouseGains() const { return MouseGains; }
    void SetMouseGain(int32 Axis, float Gain);
    float GetControlExpo() const { return ControlExpo; }
    void SetControlExpo(float Value) { if(FMath::IsFinite(Value)) ControlExpo=FMath::Clamp(Value,0.f,1.f); }
    // Flight-safety reset amount: 0 = off, 1 = very low (acro-friendly), 2 = normal.
    float GetFlightSafety() const { return FlightSafety; }
    void SetFlightSafety(float Value) { if(FMath::IsFinite(Value)) FlightSafety=FMath::Clamp(Value,0.f,2.f); }
    // Wingbeat-only audio level (0..4, default 1). Scales just the "wing" voice
    // of the aero-audio synth so the flap can cut through wind/surf/music.
    float GetWingbeatVolume() const { return WingbeatVolume; }
    void SetWingbeatVolume(float Value) { if(FMath::IsFinite(Value)) WingbeatVolume=FMath::Clamp(Value,0.f,4.f); }
    // Replay shadow-doppelgängers: show past rounds flying alongside (default on).
    bool GetReplaySpiritsEnabled() const { return bReplaySpirits; }
    void SetReplaySpiritsEnabled(bool bOn);
    // Delete all saved replay-spirit recordings for the current level.
    void DeleteReplaySpirits();
    // World time of the most recent wing contact (ground/object). The race gates
    // use this to require a clean (no-bump) pass.
    double GetLastWingTouchTime() const { return LastWingTouchTime; }
    void OpenFlightSettings();
    void CloseFlightSettings();
    bool IsFlightSettingsOpen() const { return SettingsPanel.IsValid() || bBrainSettingsOpen; }
    bool IsFlying() const { return bFlying; }
    bool IsHealthy() const { return bHealthy; }
    bool IsBlindFlight() const { return bBlind; }
    // F7 "remove all UI" toggle: collapses the cockpit/radio overlays.
    bool IsUIHidden() const { return bHideUI; }
    void ToggleUIHidden() { bHideUI = !bHideUI; }
    float GetAltitude() const;
    float GetSpeed() const;
    float GetEffort() const { return Throttle; }
    float GetBattery() const { return BatterySoc; }
    float GetClimbRate() const;
    // The atmospheric wind (m/s, Unreal world axes) the bird currently feels —
    // the unsheltered field, so the cockpit reads the true wind even at rest.
    FVector GetWind() const { return CurrentWind; }
    // The bird's world yaw (degrees) — the nose direction the wind compass reads against.
    float GetHeadingDeg() const { return GetActorRotation().Yaw; }
    FVector GetRcSticks() const { return FVector(RollInput, PitchInput, YawInput); }
    // Mouse-only control command (virtual stick), for the F9 stream overlay.
    FVector2D GetMouseControl() const { return FVector2D(float(Desktop.mouseRoll), float(Desktop.mousePitch)); }
    FVector2D GetWingAngles() const { return FVector2D(LeftFlap, RightFlap); }
    float GetSpeedModifier() const { return float(Desktop.speedModifier); }
    bool IsThrottleCoupled() const { return bCoupledThrottle; }
    void ToggleThrottleMode() { bCoupledThrottle = !bCoupledThrottle; }
    // BIRD-tab knobs (further bird tuning): direct setters mirroring the
    // existing toggle/getter, so the settings surface can bind sliders/toggles.
    void SetThrottleCoupled(bool bOn) { bCoupledThrottle = bOn; }
    void SetSpeedModifier(float Value) { if (FMath::IsFinite(Value)) Desktop.speedModifier = FMath::Clamp((double)Value, 0.0, 1.0); }
    float GetFpvCameraAngle() const { return FpvCameraAngleDeg; }
    void SetFpvCameraAngle(float Deg) { if (FMath::IsFinite(Deg)) FpvCameraAngleDeg = FMath::Clamp(Deg, -45.f, 45.f); }
    bool GetFreeLookInvert() const { return bFreeLookInvert; }
    void SetFreeLookInvert(bool bOn) { bFreeLookInvert = bOn; }
    // Airframe mass (kg) and longitudinal centre-of-gravity offset (mm, + = aft).
    // These are physical body properties, not firmware knobs: weight drives
    // Chaos' body mass, CG shifts the centre of mass fore/aft. The usable CG
    // travel scales with the craft's total length (rounded to whole cm).
    float GetBodyMassKg() const { return BodyMassKg; }
    void SetBodyMassKg(float Kg) { if (FMath::IsFinite(Kg)) { BodyMassKg = FMath::Clamp(Kg, 0.01f, 2.0f); ApplyBodyMass(); } }
    // Total nose-to-tail length of the current silhouette (cm).
    float GetCraftLengthCm() const
    {
        switch (BirdModel)
        {
        case 2:  return 106.f;   // common kestrel (falcon): nose +28.5 -> tail -77.5
        case 1:  return 144.f;   // prototype: beak +59 -> tail plate -85
        default: return 182.f;   // ravencrow: beak +70 -> tail tip -112
        }
    }
    // Half-range of the CG slider in cm — ±10% of the craft length, whole cm.
    float GetCgRangeCm() const { return FMath::Max(1.f, FMath::RoundToFloat(GetCraftLengthCm() * 0.10f)); }
    float GetCgRangeMm() const { return GetCgRangeCm() * 10.f; }
    float GetCgOffsetMm() const { return CgOffsetMm; }
    void SetCgOffsetMm(float Mm) { if (FMath::IsFinite(Mm)) { CgOffsetMm = FMath::Clamp(Mm, -GetCgRangeMm(), GetCgRangeMm()); ApplyBodyMass(); } }
    // Persist the current flight/tuning preferences to Config (called by the
    // integrated FLIGHT DESK page when the menu closes).
    void SaveFlightPreferences();
    FString GetFlightStatus() const;
    const FBorn2FlapRcController *GetRcController() const { return RcController.Get(); }
    // Mutable accessor for the semantic (Brain/umghaml) control panel to drive
    // device selection / calibration / button learning.
    FBorn2FlapRcController *GetRcControllerMutable() { return RcController.Get(); }
    // Points of interest: named reset/launch points supplied by the GameMode.
    // The start point is chosen in the level-select menu (?PoiKey=) and read once
    // at spawn; it is no longer cycled or shown as beacons in the world.
    int32 GetSelectedPoi() const { return SelectedPoi; }
    int32 GetPoiCount() const { return POIs.Num(); }
    const TArray<FBorn2FlapPoi>& GetPOIs() const { return POIs; }
    // Teleport to a point of interest from the F8 overlay (resets flight state).
    void SelectPoi(int32 Index);
    // F8 "points of interest" overlay (lightweight in-flight teleport selector).
    void OpenPoiOverlay();
    void ClosePoiOverlay();
    bool IsPoiOverlayOpen() const { return PoiOverlay.IsValid() || bBrainPoiOpen; }

  private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> LeftShoulder;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RightShoulder;
    UPROPERTY() TObjectPtr<USceneComponent> PrototypeRoot;
    UPROPERTY() TObjectPtr<USceneComponent> RavenRoot;
    UPROPERTY() TObjectPtr<USceneComponent> MembraneRoot;
    UPROPERTY() TObjectPtr<USceneComponent> MembraneLeftShoulder;
    UPROPERTY() TObjectPtr<USceneComponent> MembraneRightShoulder;
    UPROPERTY() TObjectPtr<USceneComponent> RavenLeftShoulder;
    UPROPERTY() TObjectPtr<USceneComponent> RavenRightShoulder;
    // Tail fans pivot here so the tail-elevator trim is visible in the bird.
    UPROPERTY() TObjectPtr<USceneComponent> RavenTailPivot;
    UPROPERTY() TObjectPtr<USceneComponent> MembraneTailPivot;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> SkyDome;
    // Sparse wind motes: a few tiny particles advected by the atmospheric wind so
    // the invisible current becomes perceivable as drifting specks in the air.
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WindMotes;
    TArray<FVector> MotePositions;
    TArray<float> MoteScales;
    TArray<float> MoteSpeed;
    FRandomStream MoteRandom{13011};
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBorn2FlapAudioSynth> AudioSynth;
    UPROPERTY() TObjectPtr<UCameraComponent> GroundCamera;
    UPROPERTY() TObjectPtr<UCameraComponent> FpvCamera;
    FVector GroundAnchor = FVector::ZeroVector, LastLanding = FVector::ZeroVector;
    FRotator GroundGaze = FRotator::ZeroRotator;
    bool bGroundView = false, bFpvAirView = false, bCameraGrounded = false, bGroundGazeReady = false;
    double CameraTime = 0, GroundSettleTime = 0;
    // True once the pilot has walked away from the auto-anchor beside the bird:
    // a later throw then launches from the pilot's position and gaze instead of
    // the bird's resting spot. Cleared when the bird lands (RememberLanding).
    bool bPilotWalked = false;
    // Ground interaction (pilot walk + look), active while the middle mouse
    // button is held and the bird is grounded. Chase-orbit offsets rotate the
    // spring arm around the bird; ground free-look offsets detach the ground
    // camera from its auto-aim so the pilot can look away from the bird.
    float ChaseOrbitYaw = 0.f, ChaseOrbitPitch = -12.f;
    float GroundLookYaw = 0.f, GroundLookPitch = 0.f;
    // Scroll-wheel zoom (MMB held). GroundZoom scales the ground lens FOV;
    // ChaseArmLength dollies the chase spring arm (auto-FPV when close enough).
    float GroundZoom = 1.f;
    float ChaseArmLength = 480.f;
    // Click-vs-drag latch for the middle mouse button: a clean click (press +
    // release with no mouse move or scroll) snaps the chase orbit + zoom back
    // to default, while a drag leaves the orbit/zoom where the player parked it.
    bool bMmbOrbitDragged = false;
    // Invert the ground free-look mouse (an accessibility/comfort option in the
    // GENERAL SETTINGS). Only the detached ground-perspective free-look honours
    // it — chase orbit and flight controls keep their own conventions.
    bool bFreeLookInvert = false;
    // Fixed FPV camera angle (degrees, pitch offset from the body). Adjustable
    // in flight (Q/E) and persisted; the FPV lens stays at this fixed tilt.
    float FpvCameraAngleDeg = 0.f;
    void RememberLanding(FVector Position);
    void UpdateLandingCamera(float Dt);
    // Move the ground observer (pilot) across the terrain, blocked by geometry.
    void MoveWalker(const FVector& Delta);
    bool CheckFlightCameras();
    TUniquePtr<FBorn2FlapMathBridge> MathBridge;
    TUniquePtr<FBorn2FlapRcController> RcController;
    double Accumulator = 0, LogTime = 0, WorldTime = 0;
    double LastMechanicalPower = 0, LastPhaseError = 0, LastKGainMod = 1;
    float PrevLeftFlap = 0;
    bool bFlapRising = false;        // flap velocity sign for top-of-stroke detection
    double LastStrokeTopTime = -1.0; // world-time of last flap top (period measurement)
    double MeasuredWingbeatHz = 0.0; // actual flap frequency measured from the servo
    double WingForceLp = 0.0;        // low-passed total |strip force| for force-driven audio
    born2flap::DesktopInput Desktop;
    float ControlExpo=.65f;
    float WingbeatVolume=1.0f;   // wingbeat-only voice volume (0..4)
    float FlightSafety=1.0f;  // 0 off · 1 very low (acro) · 2 normal
    bool bReplaySpirits = true;   // show replay shadow-doppelgängers
    bool bCoupledThrottle = true;
    FVector MouseGains = FVector(1, -1, 1);
    int32 BirdModel = 0;
    float BodyMassKg = 0.45f;
    float CgOffsetMm = 0.f;
    B2F_TuningConfig Tuning{};
    void ApplyTuning();
    void ApplyBodyMass();
    TWeakObjectPtr<ABorn2FlapFlightSettings> SettingsPanel;
    TWeakObjectPtr<ABorn2FlapPoiOverlay> PoiOverlay;
    // Brain (UMGHAML) path: when the Ruby Brain is the live authoring host the
    // native panel actors are not spawned — these flags stand in for their open
    // state so IsPoiOverlayOpen/IsFlightSettingsOpen still read correctly for
    // telemetry and the F8/Esc toggle handlers.
    bool bBrainPoiOpen = false;
    bool bBrainSettingsOpen = false;
    void BuildRavenCrow();
    void SweepWingColliders(float Dt);
    TMap<UBorn2FlapWingMesh*, FVector> WingSweepPrev;
    double LastWingTouchTime = -1e30;
    double LastWingPushTime = -1e30;
    bool bWingTouching = false;
    void LoadFlightPreferences();
    float Throttle = 0, RollInput = 0, YawInput = 0, PitchInput = 0, LeftFlap = 0, RightFlap = 0, BatterySoc = 1;
    bool bFlying = false, bHealthy = false, bVectors = false, bReturning = false, bBlind = false;
    bool bHideUI = false;
    // Raw F8/Esc capture. An input pre-processor (registered in BeginPlay) sees
    // every key-down before FInputModeGameAndUI routes keyboard away from
    // PlayerInput while the flight-desk panel is open; Tick consumes its flags.
    TSharedPtr<IInputProcessor> PanelKeyProcessor;
    TArray<FBorn2FlapPoi> POIs;
    int32 SelectedPoi = 0;
    FVector AeroForce = FVector::ZeroVector, AeroMoment = FVector::ZeroVector;
    FVector CurrentWind = FVector::ZeroVector;
    int32 SafetyResets = 0, MathFailures = 0, BoundaryReturns = 0;
    // The integration test uses the actual pawn/controller/Chaos path.
    bool bFlightTest = false, bSoakTest = false, bRavenFlightTest = false, bTestResetSent = false, bTestFinished = false;
    double TestTime = 0, TestPeakSpeed = 0, TestPeakAltitude = 0;
    double TestPoweredAltitude = 0, TestGlideAltitude = 0, TestBoostAltitude = 0;
    double TestBeforePullSpeed = 0, TestPullSpeed = 0, TestBeforePullHeight = 0, TestPullHeight = 0;
    double TestFlapMin = 80, TestFlapMax = -80;
    double TestGlideWingDiff = 0, TestRollWingDiff = 0;
    double TestBeforeTurnYaw = 0;
    double TestTurnTravel = 0;
    bool bTestIdle = false, bTestTurn = false, bTestLand = false, bTestReset = false, bTestSecondFlight = false;
    void BuildGeometry();
    void ResetFlight(bool bSafety = false);
    void LaunchFlight();
    UFUNCTION()
    void OnBodyHit(UPrimitiveComponent *HitComponent, AActor *OtherActor, UPrimitiveComponent *OtherComponent,
                   FVector NormalImpulse, const FHitResult &Hit);
    bool StepMath(float DeltaSeconds);
    void UpdateAeroAudio(float DeltaSeconds);
    void UpdateWindMotes(float DeltaSeconds);
    void SetBlindFlight(bool bOn);
    void CheckFlightTest();
    bool bHandlingTest = false;
    double TestLeftRecovery = 0, TestRightRecovery = 0;
    bool bDesktopInputTest = false, bDesktopTestPass = true;
    double DesktopTestTime = 0;
    int32 DesktopTestStage = -1;
    void CheckDesktopInputTest(float DeltaSeconds);
};