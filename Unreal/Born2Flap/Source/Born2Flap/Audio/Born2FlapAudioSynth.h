#pragma once
// Born2FlapAudioSynth.h — the real-time Unreal audio loop for the aero-audio
// engine. It owns the dependency-free FAudioEngine (Born2FlapAudioEngine.h)
// and renders its five procedural voices into the audio mixer each callback.
//
// This is the piece that was previously "open" in docs/ui-audio.md: the
// FAudioEngine held params but nothing fed PCM into the engine. USynthComponent
// bridges that gap — SetVoiceParams is called from the game thread (flight pawn
// telemetry), OnGenerateAudio runs on the audio render thread.

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "UI/Born2FlapAudioEngine.h"
#include "UI/Born2FlapUiOps.h"
#include "Born2FlapAudioSynth.generated.h"

UCLASS(ClassGroup = (Audio, Synth), meta = (BlueprintSpawnableComponent))
class BORN2FLAP_API UBorn2FlapAudioSynth : public USynthComponent
{
    GENERATED_BODY()

public:
    UBorn2FlapAudioSynth(const FObjectInitializer& ObjectInitializer);

    // Feed one voice's spectral params from the game thread. Thread-safe
    // against the audio render thread (FCriticalSection).
    void SetVoiceParams(const std::string& Name, const born2flap::ui::FProps& Params);

protected:
    virtual bool Init(int32& SampleRate) override;
    virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
    born2flap::audio::FAudioEngine Engine;
    FCriticalSection VoiceLock;
};