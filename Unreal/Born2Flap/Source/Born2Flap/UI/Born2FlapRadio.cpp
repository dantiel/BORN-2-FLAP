// Born2FlapRadio.cpp — the unified radio implementation. See header for notes.

#include "UI/Born2FlapRadio.h"

#include "UI/Born2FlapUIRenderer.h"
#include "UI/Born2FlapI18n.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

using namespace born2flap::ui;

// ---- compact wire-model builders (mirrors Born2FlapFlightHUD.cpp) ----------
namespace {

FValue S(const char* v) { FValue x; x.kind = FValue::Kind::String; x.str = v; return x; }
FValue N(double v) { FValue x; x.kind = FValue::Kind::Number; x.num = v; return x; }
FValue L(const char* Key) { return S(TCHAR_TO_UTF8(*Born2Flap::I18n::T(Key))); }

FProps P(std::initializer_list<std::pair<const char*, FValue>> Items)
{
    FProps M;
    for (const auto& KV : Items) M[KV.first] = KV.second;
    return M;
}

FNode Nd(const char* Type, FProps Props = {}, std::vector<FNode> Children = {})
{
    FNode N_;
    N_.type = Type;
    N_.props = std::move(Props);
    N_.children = std::move(Children);
    return N_;
}

FOp UpdateProps(FPath Path, FProps Props)
{
    FOp O;
    O.op = EOp::UpdateProps;
    O.path = std::move(Path);
    O.props = std::move(Props);
    return O;
}

FPath Pth(std::initializer_list<int> I) { return FPath(I); }

}  // namespace

// ---- station ---------------------------------------------------------------

void UBorn2FlapRadioStation::Initialize(UWorld* World, const FString& LevelName)
{
    const UBorn2FlapRadioSettings* Settings = GetDefault<UBorn2FlapRadioSettings>();
    if (!Settings)
        return;

    const FBorn2FlapLevelRadio* Found = nullptr;
    for (const FBorn2FlapLevelRadio& R : Settings->LevelRadios)
    {
        if (R.LevelName.Equals(LevelName, ESearchCase::IgnoreCase))
        {
            Found = &R;
            break;
        }
    }
    if (!Found)
    {
        UE_LOG(LogTemp, Display, TEXT("Born2FlapRadio: no config for level '%s' (silent)"), *LevelName);
        return;
    }

    StationName = Found->StationName.IsEmpty() ? Born2Flap::I18n::T("radio.default") : Found->StationName;
    Volume = Found->Volume;
    Crossfade = Found->CrossfadeSeconds;

    for (const FString& Path : Found->Tracks)
    {
        if (USoundWave* W = LoadObject<USoundWave>(nullptr, *Path))
            Playlist.Add(W);
    }
    if (Playlist.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("Born2FlapRadio: no tracks imported for level '%s'"), *LevelName);
        return;
    }

    UE_LOG(LogTemp, Display, TEXT("Born2FlapRadio: level='%s' station='%s' tracks=%d"),
           *LevelName, *StationName, Playlist.Num());

    Active = UGameplayStatics::CreateSound2D(World, Playlist[0].Get(), 0.0f, 1.0f, 0.0f, nullptr, false, false);
    if (!Active)
        return;

    // A single-track station loops seamlessly instead of crossfading into itself.
    if (Playlist.Num() == 1)
    {
        Playlist[0]->bLooping = true;
        Fader = nullptr;
    }
    else
    {
        Fader = UGameplayStatics::CreateSound2D(World, Playlist[0].Get(), 0.0f, 1.0f, 0.0f, nullptr, false, false);
    }

    StartTrack(0, Volume);
}

void UBorn2FlapRadioStation::StartTrack(int32 Index, float StartVolume)
{
    if (!Active || !Playlist.IsValidIndex(Index) || !Playlist[Index])
        return;
    TrackIndex = Index;
    Active->SetSound(Playlist[Index].Get());
    Active->SetVolumeMultiplier(StartVolume);
    Active->Play();
    TrackDuration = Playlist[Index]->Duration;
    TrackStartTime = 0.0f;
    bCrossfading = false;
}

void UBorn2FlapRadioStation::Tick(float DeltaSeconds)
{
    if (!Active || Playlist.IsEmpty() || !bPlaying || Playlist.Num() < 2)
        return;

    TrackStartTime += DeltaSeconds;

    // Start the next track shortly before this one ends (gapless overlap).
    if (!bCrossfading && TrackDuration > Crossfade * 2.0f && TrackDuration - TrackStartTime <= Crossfade)
    {
        if (Fader)
        {
            const int32 Next = (TrackIndex + 1) % Playlist.Num();
            if (!Playlist.IsValidIndex(Next) || !Playlist[Next])
                return;
            Fader->SetSound(Playlist[Next].Get());
            Fader->SetVolumeMultiplier(0.0f);
            Fader->Play();
            bCrossfading = true;
            CrossfadeElapsed = 0.0f;
        }
    }

    if (bCrossfading)
    {
        CrossfadeElapsed += DeltaSeconds;
        const float T = FMath::Clamp(CrossfadeElapsed / FMath::Max(Crossfade, 0.01f), 0.0f, 1.0f);
        Fader->SetVolumeMultiplier(T * Volume);
        Active->SetVolumeMultiplier((1.0f - T) * Volume);
        if (T >= 1.0f)
        {
            Active->Stop();
            UAudioComponent* Tmp = Active;
            Active = Fader;
            Fader = Tmp;
            TrackIndex = (TrackIndex + 1) % Playlist.Num();
            if (Playlist.IsValidIndex(TrackIndex) && Playlist[TrackIndex])
                TrackDuration = Playlist[TrackIndex]->Duration;
            // The new track has already been audible for Crossfade seconds.
            TrackStartTime = Crossfade;
            bCrossfading = false;
        }
    }
}

void UBorn2FlapRadioStation::TogglePlayPause()
{
    if (!Active)
        return;
    bPlaying = !bPlaying;
    Active->SetPaused(!bPlaying);
    if (bCrossfading && !bPlaying && Fader)
        Fader->SetPaused(true);
    if (bCrossfading && bPlaying && Fader)
        Fader->SetPaused(false);
}

void UBorn2FlapRadioStation::AdjustVolume(float Delta)
{
    Volume = FMath::Clamp(Volume + Delta, 0.0f, 1.0f);
    if (Active)
        Active->SetVolumeMultiplier(Volume);
    if (bCrossfading && Fader)
        Fader->SetVolumeMultiplier(FMath::Clamp(CrossfadeElapsed / FMath::Max(Crossfade, 0.01f), 0.0f, 1.0f) * Volume);
}

FString UBorn2FlapRadioStation::GetTrackName() const
{
    return Playlist.IsValidIndex(TrackIndex) && Playlist[TrackIndex] ? Playlist[TrackIndex]->GetName() : FString();
}

// ---- HUD -------------------------------------------------------------------

ABorn2FlapRadioHUD::ABorn2FlapRadioHUD()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ABorn2FlapRadioHUD::BeginPlay()
{
    Super::BeginPlay();
    Renderer = NewObject<UBorn2FlapUIRenderer>(this);
}

void ABorn2FlapRadioHUD::SetStation(UBorn2FlapRadioStation* InStation)
{
    Station = InStation;
    if (Station && Renderer)
        BuildPanel();
}

// Semantic tree (paths from the Overlay root []):
//   [0]     the station panel
//   [0,0]   track banner (text)
//   [0,1]   volume stat (value)
void ABorn2FlapRadioHUD::BuildPanel()
{
    if (!Renderer || !Station)
        return;

    FNode Root = Nd("Overlay", P({ {"align", S("right")}, {"valign", S("bottom")} }),
    {
        Nd("Panel", P({ {"title", S(TCHAR_TO_UTF8(*Station->GetStationName()))}, {"tone", S("accent")}, {"spacing", N(4)} }),
        {
            Nd("Banner", P({ {"text", S("—")}, {"tone", S("normal")} })),
            Nd("Stat",  P({ {"label", L("radio.vol")}, {"value", N(50)}, {"unit", S(" %")}, {"tone", S("info")} })),
        }),
    });

    FOp Mount;
    Mount.op = EOp::CreateInstance;
    Mount.path = FPath();
    Mount.node = std::move(Root);

    TArray<FOp> Ops;
    Ops.Add(MoveTemp(Mount));
    Renderer->ApplyOps(Ops);
    LastTrackLine.Empty();
    LastVolumePct = -1;
}

void ABorn2FlapRadioHUD::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Refresh();
}

void ABorn2FlapRadioHUD::Refresh()
{
    if (!Renderer || !Station)
        return;

    // F7 "remove all UI": hide the radio panel too, matching the cockpit.
    if (!CachedBird)
    {
        if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
            CachedBird = Cast<ABorn2FlapFlightPawn>(PC->GetPawn());
    }
    const bool bShouldHide = CachedBird && CachedBird->IsUIHidden();
    if (bShouldHide != bLastHidden)
    {
        TArray<FOp> HideOps;
        HideOps.Add(UpdateProps(FPath(), P({ {"visible", N(bShouldHide ? 0.0 : 1.0)} })));
        Renderer->ApplyOps(HideOps);
        bLastHidden = bShouldHide;
    }
    if (bShouldHide)
        return;

    TArray<FOp> Ops;

    const FString TrackLine = Station->IsPlaying() ? Station->GetTrackName() : FString(TEXT("(paused)"));
    if (TrackLine != LastTrackLine)
    {
        Ops.Add(UpdateProps(Pth({0, 0}), P({ {"text", S(TCHAR_TO_UTF8(*TrackLine))} })));
        LastTrackLine = TrackLine;
    }

    const int32 VolumePct = FMath::RoundToInt(Station->GetVolume() * 100.f);
    if (VolumePct != LastVolumePct)
    {
        Ops.Add(UpdateProps(Pth({0, 1}), P({ {"value", N((double)VolumePct)} })));
        LastVolumePct = VolumePct;
    }

    if (Ops.Num() > 0)
        Renderer->ApplyOps(Ops);
}