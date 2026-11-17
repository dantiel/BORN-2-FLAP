#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Born2FlapFootsteps.generated.h"

class USoundWaveProcedural;
class UAudioComponent;

// UBorn2FlapFootsteps — procedural on-foot footstep player for the pilot
// avatar. There is no footstep audio asset; each stride synthesises a short
// filtered noise burst + low thump into a USoundWaveProcedural stream, so the
// walk/run gait gets real footfalls. The volume follows the pawn's
// FootstepVolume preference (APP SETTINGS > AUDIO > FOOTSTEPS VOLUME).
UCLASS()
class BORN2FLAP_API UBorn2FlapFootsteps : public UActorComponent
{
    GENERATED_BODY()
public:
    UBorn2FlapFootsteps();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaSeconds, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

    // 0..1, applied live to the audio component and the synth amplitude.
    void SetVolume(float InVolume);

private:
    // Synthesise one footfall: noise transient + low sine thump, ~70 ms.
    void QueueStep();
    // Keep the stream alive while idle with a tiny silence chunk.
    void QueueSilence(float Seconds);

    UPROPERTY() TObjectPtr<USoundWaveProcedural> Wave = nullptr;
    UPROPERTY() TObjectPtr<UAudioComponent> Audio = nullptr;

    float Volume = 1.f;
    float PrevPhase = 0.f;      // sin(gait phase) last tick (sign of the swing)
    float IdleFill = 0.f;       // seconds of silence still buffered
    int32 StepParity = 0;       // alternates left/right for slight timbre variety
};
