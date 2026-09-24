#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Math/Born2FlapMathBridge.h"
#include "Born2FlapFlightPawn.generated.h"
class UBoxComponent;
class USceneComponent;
class USpringArmComponent;
class UCameraComponent;
// Explicit assisted trainer: measured aerodynamics and assistance are logged separately.
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
    float GetTargetAltitude() const { return TargetAltitude; }
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
    float TargetAltitude = 7, Throttle = 0, Turn = 0, LeftFlap = 0, RightFlap = 0, Bank = 0;
    bool bFlying = false, bLanding = false, bHealthy = false, bVectors = false, bReturning = false;
    FVector AeroForce = FVector::ZeroVector, AeroMoment = FVector::ZeroVector, AssistForce = FVector::ZeroVector;
    int32 SafetyResets = 0, MathFailures = 0, BoundaryReturns = 0;
    // The integration test uses the actual pawn/controller/Chaos path.
    bool bFlightTest = false, bSoakTest = false, bTestResetSent = false, bTestFinished = false;
    double TestTime = 0, TestPeakSpeed = 0, TestPeakAltitude = 0, TestHoldError = 0;
    int32 TestHoldSamples = 0;
    bool bTestIdle = false, bTestTurn = false, bTestLand = false, bTestReset = false, bTestSecondFlight = false;
    void BuildGeometry();
    void ResetFlight(bool bSafety = false);
    bool StepMath(float DeltaSeconds);
    void CheckFlightTest();
};
