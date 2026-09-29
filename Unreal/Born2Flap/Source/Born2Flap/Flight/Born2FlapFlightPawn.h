#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Math/Born2FlapMathBridge.h"
#include "Input/Born2FlapRcController.h"
#include "born2flap_rc_input.h"
#include "born2flap_desktop_input.h"
#include "Born2FlapFlightPawn.generated.h"
class UBoxComponent;
class USceneComponent;
class USpringArmComponent;
class UCameraComponent;
class UBorn2FlapAudioSynth;
class SWidget;
// Live-tuning surface the hangar edits: servo, battery and the exposed
// controller knobs. Mirrors B2F_TuningConfig field order.
enum class ETuningField : uint8
{
    ServoSpeed, StallTorque, Backdrive, BatteryVoltage, BatteryResistance, BatteryCapacity,
    FlapBaseFreq, MountAngle, GlideAngle, StrokeFerocity, AileronScale, ElevatorScale, Count
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
    FVector GetMouseGains() const { return MouseGains; }
    void SetMouseGain(int32 Axis, float Gain);
    float GetControlExpo() const { return ControlExpo; }
    void SetControlExpo(float Value) { if(FMath::IsFinite(Value)) ControlExpo=FMath::Clamp(Value,0.f,1.f); }
    void OpenFlightSettings();
    void CloseFlightSettings();
    bool IsFlightSettingsOpen() const { return SettingsWidget.IsValid(); }
    bool IsFlying() const { return bFlying; }
    bool IsHealthy() const { return bHealthy; }
    bool IsBlindFlight() const { return bBlind; }
    float GetAltitude() const;
    float GetSpeed() const;
    float GetEffort() const { return Throttle; }
    float GetBattery() const { return BatterySoc; }
    float GetClimbRate() const;
    FVector GetRcSticks() const { return FVector(RollInput, PitchInput, YawInput); }
    FVector2D GetWingAngles() const { return FVector2D(LeftFlap, RightFlap); }
    float GetWheelThrottle() const { return Desktop.wheelThrottle; }
    bool IsWheelThrottleActive() const { return Desktop.wheelOwnsThrottle; }
    FString GetFlightStatus() const;
    const FBorn2FlapRcController *GetRcController() const { return RcController.Get(); }

  private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> LeftShoulder;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RightShoulder;
    UPROPERTY() TObjectPtr<USceneComponent> PrototypeRoot;
    UPROPERTY() TObjectPtr<USceneComponent> RavenRoot;
    UPROPERTY() TObjectPtr<USceneComponent> RavenLeftShoulder;
    UPROPERTY() TObjectPtr<USceneComponent> RavenRightShoulder;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBorn2FlapAudioSynth> AudioSynth;
    UPROPERTY() TObjectPtr<UCameraComponent> GroundCamera;
    UPROPERTY() TObjectPtr<UCameraComponent> FpvCamera;
    FVector GroundAnchor = FVector::ZeroVector, LastLanding = FVector::ZeroVector;
    FRotator GroundGaze = FRotator::ZeroRotator;
    bool bGroundView = false, bFpvAirView = false, bCameraGrounded = false, bGroundGazeReady = false;
    double CameraTime = 0, GroundSettleTime = 0;
    void RememberLanding(FVector Position);
    void UpdateLandingCamera(float Dt);
    bool CheckFlightCameras();
    TUniquePtr<FBorn2FlapMathBridge> MathBridge;
    TUniquePtr<FBorn2FlapRcController> RcController;
    double Accumulator = 0, LogTime = 0, WorldTime = 0;
    double LastMechanicalPower = 0, LastPhaseError = 0, LastKGainMod = 1;
    float PrevLeftFlap = 0;
    born2flap::DesktopInput Desktop;
    float ControlExpo=.65f;
    FVector MouseGains = FVector(1, -1, 1);
    int32 BirdModel = 0;
    B2F_TuningConfig Tuning{};
    void ApplyTuning();
    TSharedPtr<SWidget> SettingsWidget;
    void BuildRavenCrow();
    void LoadFlightPreferences();
    void SaveFlightPreferences();
    float Throttle = 0, RollInput = 0, YawInput = 0, PitchInput = 0, LeftFlap = 0, RightFlap = 0, BatterySoc = 1;
    bool bFlying = false, bHealthy = false, bVectors = false, bReturning = false, bBlind = false;
    FVector AeroForce = FVector::ZeroVector, AeroMoment = FVector::ZeroVector;
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
    void SetBlindFlight(bool bOn);
    void CheckFlightTest();
    bool bHandlingTest = false;
    double TestLeftRecovery = 0, TestRightRecovery = 0;
    bool bDesktopInputTest = false, bDesktopTestPass = true;
    double DesktopTestTime = 0;
    int32 DesktopTestStage = -1;
    void CheckDesktopInputTest(float DeltaSeconds);
};