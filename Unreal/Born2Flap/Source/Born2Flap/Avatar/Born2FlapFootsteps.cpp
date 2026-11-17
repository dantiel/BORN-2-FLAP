#include "Avatar/Born2FlapFootsteps.h"
#include "Avatar/Born2FlapAvatar.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundCue.h"

namespace
{
constexpr int32 SampleRate = 22050;   // mono 16-bit PCM
constexpr float StepBurstSec = 0.07f; // footfall transient length
}

UBorn2FlapFootsteps::UBorn2FlapFootsteps()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UBorn2FlapFootsteps::BeginPlay()
{
    Super::BeginPlay();

    Wave = NewObject<USoundWaveProcedural>(this);
    Wave->SetSampleRate(SampleRate);
    Wave->NumChannels = 1;
    Wave->Duration = INDEFINITELY_LOOPING_DURATION;
    Wave->bLooping = false;

    Audio = NewObject<UAudioComponent>(GetOwner());
    if (!Audio)
        return;
    Audio->AttachToComponent(GetOwner()->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
    Audio->SetSound(Wave);
    Audio->SetVolumeMultiplier(Volume);
    Audio->Play();
}

void UBorn2FlapFootsteps::SetVolume(float InVolume)
{
    Volume = FMath::Clamp(InVolume, 0.f, 1.f);
    if (Audio)
        Audio->SetVolumeMultiplier(Volume);
}

void UBorn2FlapFootsteps::TickComponent(float DeltaSeconds, ELevelTick TickType,
                                        FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);
    if (!Wave || !Audio || !Audio->IsPlaying())
        return;

    const ABorn2FlapAvatar* Owner = Cast<ABorn2FlapAvatar>(GetOwner());
    if (!Owner)
        return;

    // Walking is gait-driven: the leg swing sin() crosses zero twice per full
    // stride (left foot down, right foot down). Grounded state comes from the
    // pawn only ever sending non-zero local velocity while the pilot walks.
    const float Speed = FMath::Abs(Owner->GetGroundSpeed());
    const bool bGait = Speed > 40.f && !Owner->IsThrowing() && !Owner->IsCatching();
    const float Phase = bGait ? FMath::Sin(Owner->GetGaitPhase()) : 0.f;

    if (bGait && (PrevPhase > 0.05f && Phase <= -0.05f) || (PrevPhase < -0.05f && Phase >= 0.05f))
        QueueStep();
    PrevPhase = Phase;

    // Idle stream upkeep: keep ~0.3 s of silence buffered so the stream never
    // stalls, without growing an unbounded queue.
    IdleFill -= DeltaSeconds;
    if (!bGait && IdleFill < 0.2f)
    {
        QueueSilence(0.1f);
        IdleFill += 0.1f;
    }
}

void UBorn2FlapFootsteps::QueueStep()
{
    if (Volume <= 0.001f)
        return;

    const int32 N = FMath::Max(1, int32(StepBurstSec * SampleRate));
    TArray<uint8> Pcm;
    Pcm.SetNumUninitialized(N * sizeof(int16));
    int16* Samples = reinterpret_cast<int16*>(Pcm.GetData());

    const float Heel = 88.f + (StepParity & 1) * 14.f;  // slight alternation
    StepParity++;
    for (int32 I = 0; I < N; ++I)
    {
        const float T = float(I) / SampleRate;
        const float Env = FMath::Exp(-T * 46.f);                     // fast transient
        const float Noise = 2.f * FMath::FRand() - 1.f;              // gravel texture
        const float LowNoise = 0.5f * (Noise + (2.f * FMath::FRand() - 1.f)); // darker
        const float Thump = FMath::Sin(2.f * PI * Heel * T);         // heel knock
        const float V = (0.42f * LowNoise + 0.22f * Thump) * Env * Volume;
        Samples[I] = int16(FMath::Clamp(V, -1.f, 1.f) * 32000.f);
    }
    Wave->QueueAudio(Pcm.GetData(), Pcm.Num());
    IdleFill += StepBurstSec;
}

void UBorn2FlapFootsteps::QueueSilence(float Seconds)
{
    const int32 N = FMath::Max(1, int32(Seconds * SampleRate));
    TArray<uint8> Pcm;
    Pcm.SetNumZeroed(N * sizeof(int16));
    Wave->QueueAudio(Pcm.GetData(), Pcm.Num());
}