#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Math/Born2FlapMathBridge.h"
#include "born2flap_rc_input.h"
#include "Born2FlapFlightPawn.generated.h"
class UBoxComponent;
class USceneComponent;
class USpringArmComponent;
class UCameraComponent;
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
    bool IsFlying() const { return bFlying; }
    bool IsHealthy() const { return bHealthy; }
    float GetAltitude() const;
    float GetSpeed() const;
    float GetEffort() const { return Throttle; }
    float GetBattery() const { return BatterySoc; }
    float GetClimbRate() const;
    FVector GetRcSticks() const { return FVector(RollInput, PitchInput, YawInput); }
    FVector2D GetWingAngles() const { return FVector2D(LeftFlap, RightFlap); }
    FString GetFlightStatus() const;

  private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> LeftShoulder;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RightShoulder;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    TUniquePtr<FBorn2FlapMathBridge> MathBridge;
    double Accumulator = 0, LogTime = 0;
    born2flap::RcKeyboard Keyboard;
    float Throttle = 0, RollInput = 0, YawInput = 0, PitchInput = 0, LeftFlap = 0, RightFlap = 0, BatterySoc = 1;
    bool bFlying = false, bHealthy = false, bVectors = false, bReturning = false;
    FVector AeroForce = FVector::ZeroVector, AeroMoment = FVector::ZeroVector;
    int32 SafetyResets = 0, MathFailures = 0, BoundaryReturns = 0;
    // The integration test uses the actual pawn/controller/Chaos path.
    bool bFlightTest = false, bSoakTest = false, bTestResetSent = false, bTestFinished = false;
    double TestTime = 0, TestPeakSpeed = 0, TestPeakAltitude = 0;
    double TestPoweredAltitude = 0, TestGlideAltitude = 0, TestBoostAltitude = 0;
    double TestBeforePullSpeed = 0, TestPullSpeed = 0, TestBeforePullHeight = 0, TestPullHeight = 0;
    double TestFlapMin = 80, TestFlapMax = -80;
    double TestGlideWingDiff = 0, TestRollWingDiff = 0;
    bool bTestIdle = false, bTestTurn = false, bTestLand = false, bTestReset = false, bTestSecondFlight = false;
    void BuildGeometry();
    void ResetFlight(bool bSafety = false);
    void LaunchFlight();
    UFUNCTION()
    void OnBodyHit(UPrimitiveComponent *HitComponent, AActor *OtherActor, UPrimitiveComponent *OtherComponent,
                   FVector NormalImpulse, const FHitResult &Hit);
    bool StepMath(float DeltaSeconds);
    void CheckFlightTest();
};
