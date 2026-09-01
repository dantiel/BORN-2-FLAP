#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Born2FlapFlightPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class FBorn2FlapMathBridge;

UCLASS()
class BORN2FLAP_API ABorn2FlapFlightPawn : public APawn
{
    GENERATED_BODY()

public:
    ABorn2FlapFlightPawn();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Body;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USpringArmComponent> CameraBoom;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCameraComponent> Camera;

    TUniquePtr<FBorn2FlapMathBridge> MathBridge;
    double AccumulatorSeconds = 0.0;
    float ThrottleInput = 0.0f;
    float RollInput = 0.0f;
    float PitchInput = 0.0f;
    float YawInput = 0.0f;

    static constexpr double MathStepSeconds = 1.0 / 240.0;

    void StepMath(double DeltaTimeSeconds);
    void SetThrottle(float Value) { ThrottleInput = Value; }
    void SetRoll(float Value) { RollInput = Value; }
    void SetPitch(float Value) { PitchInput = Value; }
    void SetYaw(float Value) { YawInput = Value; }
};
