#include "Audio/Born2FlapAudioSynth.h"

UBorn2FlapAudioSynth::UBorn2FlapAudioSynth(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , Engine(44100.0)
{
}

void UBorn2FlapAudioSynth::SetVoiceParams(const std::string& Name, const born2flap::ui::FProps& Params)
{
    FScopeLock Lock(&VoiceLock);
    Engine.SetParams(Name, Params);
}

bool UBorn2FlapAudioSynth::Init(int32& SampleRate)
{
    SampleRate = static_cast<int32>(Engine.SampleRate());
    return true;
}

int32 UBorn2FlapAudioSynth::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
    FScopeLock Lock(&VoiceLock);

    const int32 Channels = FMath::Clamp(NumChannels, 1, 2);
    const int32 NumFrames = NumSamples / Channels;
    for (int32 Frame = 0; Frame < NumFrames; ++Frame)
    {
        float L = 0.f, R = 0.f;
        Engine.RenderFrame(L, R);
        if (Channels >= 2)
        {
            OutAudio[Frame * 2 + 0] = L;
            OutAudio[Frame * 2 + 1] = R;
        }
        else
        {
            OutAudio[Frame] = (L + R) * 0.5f;
        }
    }
    return NumSamples;
}