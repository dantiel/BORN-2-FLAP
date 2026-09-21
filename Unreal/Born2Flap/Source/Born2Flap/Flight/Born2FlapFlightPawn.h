#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Math/Born2FlapMathBridge.h"
#include "Born2FlapFlightPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class USceneComponent;

UCLASS()
class BORN2FLAP_API ABorn2FlapFlightPawn : public APawn
{
    GENERATED_BODY()

public:
    ABorn2FlapFlightPawn();
    virtual ~ABorn2FlapFlightPawn() override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
    // ── Physics root + camera ──────────────────────────────────────
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Body;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USpringArmComponent> CameraBoom;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCameraComponent> Camera;

    // ── Ornithopter geometry (procedural, no art assets) ──────────
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Nose;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> TailHorizontal;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> TailVertical;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> RightShoulder;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> LeftShoulder;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> RightWing;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> LeftWing;

    // ── Math backend + pilot state ─────────────────────────────────
    TUniquePtr<FBorn2FlapMathBridge> MathBridge;
    double AccumulatorSeconds = 0.0;
    float ThrottleInput = 0.0f;
    float RollInput = 0.0f;
    float PitchInput = 0.0f;
    float YawInput = 0.0f;

    // Latest observables, for debug readout.
    float LastLeftFlapDeg = 0.0f;
    float LastRightFlapDeg = 0.0f;
    float LastSoc = 1.0f;
    bool bIsFlapping = false;

    // Wings flap at 1:1 with the firmware's actual flap deviation; this is a
    // tunable visual gain (kept 1.0 for physical fidelity).
    UPROPERTY(EditAnywhere, Category = "Ornithopter")
    float FlapVisualGain = 1.0f;

    static constexpr double MathStepSeconds = 1.0 / 240.0;

    void BuildGeometry();
    void StepMath(double DeltaTimeSeconds);
    void ApplyFlapAngles(float LeftDeg, float RightDeg);
    void SetThrottle(float Value) { ThrottleInput = Value; }
    void SetRoll(float Value) { RollInput = Value; }
    void SetPitch(float Value) { PitchInput = Value; }
    void SetYaw(float Value) { YawInput = Value; }
};
