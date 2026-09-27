#include "Audio/Born2FlapAudioSynth.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

UBorn2FlapAudioSynth::UBorn2FlapAudioSynth(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , Engine(44100.0)
{
    NumChannels=2;
}

void UBorn2FlapAudioSynth::SetVoiceParams(const std::string& Name, const born2flap::ui::FProps& Params)
{
    FScopeLock Lock(&VoiceLock);
    Engine.SetParams(Name, Params);
}

bool UBorn2FlapAudioSynth::Init(int32& SampleRate)
{
    bMeasureAudio=FParse::Param(FCommandLine::Get(),TEXT("B2FAudioTest"));
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
    if(bMeasureAudio)
    {
        for(int32 I=0;I<NumSamples;++I)
        {
            if(!FMath::IsFinite(OutAudio[I])) { ++InvalidSamples; OutAudio[I]=0; }
            SquareSum+=double(OutAudio[I])*OutAudio[I];
            Peak=FMath::Max(Peak,double(FMath::Abs(OutAudio[I])));
        }
        RenderedSamples+=NumSamples;
    }
    return NumSamples;
}
bool UBorn2FlapAudioSynth::CheckRenderedAudio()
{
    FScopeLock Lock(&VoiceLock);
    const double Rms=RenderedSamples ? FMath::Sqrt(SquareSum/RenderedSamples) : 0;
    const bool Pass=RenderedSamples>44100 && InvalidSamples==0 && Rms>0.00001 && Peak<=1.0;
    UE_LOG(LogTemp,Display,TEXT("AeroAudioTest %s samples=%llu rms=%.6f peak=%.6f invalid=%llu"),
        Pass ? TEXT("PASS") : TEXT("FAIL"),RenderedSamples,Rms,Peak,InvalidSamples);
    return Pass;
}
