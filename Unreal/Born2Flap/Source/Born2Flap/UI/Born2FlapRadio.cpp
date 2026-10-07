// Born2FlapRadio.cpp — the unified radio implementation. See header for notes.

#include "UI/Born2FlapRadio.h"

#include "UI/Born2FlapUIRenderer.h"
#include "UI/Born2FlapI18n.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

using namespace born2flap::ui;

// ---- compact wire-model builders (mirrors Born2FlapFlightHUD.cpp) ----------
namespace {

// The radio volume lives in the same file the flight pawn uses, so wingbeat and
// radio settings sit side by side under [Audio].
FString RadioPrefsPath()
{
    return FPaths::ProjectSavedDir() /
        (FParse::Param(FCommandLine::Get(), TEXT("B2FDesktopInputTest"))
            ? TEXT("Automation/FlightPreferences.ini")
            : TEXT("Config/FlightPreferences.ini"));
}

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

// Fallback shown when a track has no cover art and nothing is auto-detected.
static const TCHAR* kPlaceholderCover = TEXT("/Game/UI/Elements/RadioOn.RadioOn");

// Normalize a package path ("/Game/A/B") into a full object ref ("/Game/A/B.B").
FString ToAssetRef(const FString& Path)
{
    if (Path.IsEmpty())
        return Path;
    int32 Slash = INDEX_NONE;
    Path.FindLastChar('/', Slash);
    const FString Name = Path.Mid(Slash + 1);
    if (Name.Contains(TEXT(".")))
        return Path;
    return Path + TEXT(".") + Name;
}

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
    LoadSavedVolume();

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

    // Resolve cover art for each track: explicit config Covers[i] first, then
    // auto-detect by naming convention, otherwise leave empty (placeholder).
    for (int32 i = 0; i < Playlist.Num(); ++i)
    {
        FString Cover;
        if (Found->Covers.IsValidIndex(i) && !Found->Covers[i].IsEmpty())
            Cover = ToAssetRef(Found->Covers[i]);
        else
            Cover = AutoDetectCover(Found->Tracks[i]);
        if (!Cover.IsEmpty() && !CoverExists(Cover))
            Cover.Empty();
        TrackCovers.Add(Cover);
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

void UBorn2FlapRadioStation::SetPaused(bool bPaused)
{
    if (!Active)
        return;
    // Menu silence: pause/resume both voices so the music only plays inside a
    // level, never behind the level-select screen.
    bPlaying = !bPaused;
    Active->SetPaused(bPaused);
    if (Fader)
        Fader->SetPaused(bPaused);
}

void UBorn2FlapRadioStation::AdjustVolume(float Delta)
{
    SetVolume(Volume + Delta);
}

void UBorn2FlapRadioStation::SetVolume(float NewVolume)
{
    Volume = FMath::Clamp(NewVolume, 0.0f, 1.0f);
    bMuted = false;
    SavedVolume = Volume;
    if (Active)
        Active->SetVolumeMultiplier(Volume);
    if (bCrossfading && Fader)
        Fader->SetVolumeMultiplier(FMath::Clamp(CrossfadeElapsed / FMath::Max(Crossfade, 0.01f), 0.0f, 1.0f) * Volume);
    SaveSavedVolume();
}

void UBorn2FlapRadioStation::ToggleMute()
{
    if (!Active)
        return;
    if (!bMuted)
    {
        SavedVolume = FMath::Max(Volume, 0.001f);
        bMuted = true;
        Volume = 0.0f;
    }
    else
    {
        bMuted = false;
        Volume = SavedVolume;
    }
    Active->SetVolumeMultiplier(Volume);
    if (bCrossfading && Fader)
        Fader->SetVolumeMultiplier(FMath::Clamp(CrossfadeElapsed / FMath::Max(Crossfade, 0.01f), 0.0f, 1.0f) * Volume);
    SaveSavedVolume();
}

void UBorn2FlapRadioStation::LoadSavedVolume()
{
    FConfigFile Config;
    Config.Read(RadioPrefsPath());
    float V = Volume;
    Config.GetFloat(TEXT("Audio"), TEXT("RadioVolume"), V);
    if (FMath::IsFinite(V))
    {
        Volume = FMath::Clamp(V, 0.0f, 1.0f);
        SavedVolume = Volume;
    }
}

void UBorn2FlapRadioStation::SaveSavedVolume()
{
    const FString Path = RadioPrefsPath();
    FConfigFile Config;
    Config.Read(Path);
    Config.SetFloat(TEXT("Audio"), TEXT("RadioVolume"), SavedVolume);
    Config.Write(Path);
}

FString UBorn2FlapRadioStation::GetTrackName() const
{
    return Playlist.IsValidIndex(TrackIndex) && Playlist[TrackIndex] ? Playlist[TrackIndex]->GetName() : FString();
}

FString UBorn2FlapRadioStation::GetCoverPath() const
{
    if (TrackCovers.IsValidIndex(TrackIndex) && !TrackCovers[TrackIndex].IsEmpty())
        return TrackCovers[TrackIndex];
    return kPlaceholderCover;
}

FString UBorn2FlapRadioStation::AutoDetectCover(const FString& TrackPath) const
{
    if (TrackPath.IsEmpty())
        return FString();

    int32 Slash = INDEX_NONE;
    TrackPath.FindLastChar('/', Slash);
    const FString Folder = TrackPath.Left(Slash);
    const FString Name = TrackPath.Mid(Slash + 1);

    // Common conventions: <Track>_Cover, Cover_<Track>, <Track>_Art.
    const TArray<FString> Candidates = {
        Folder / (Name + TEXT("_Cover")),
        Folder / (FString(TEXT("Cover_")) + Name),
        Folder / (Name + TEXT("_Art")),
    };
    for (const FString& C : Candidates)
    {
        const FString Ref = ToAssetRef(C);
        if (CoverExists(Ref))
            return Ref;
    }
    return FString();
}

bool UBorn2FlapRadioStation::CoverExists(const FString& AssetRef)
{
    return LoadObject<UTexture2D>(nullptr, *AssetRef) != nullptr;
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
//   [0]          the station panel
//   [0,0]        cover + text row (cover art left of the title/volume)
//   [0,0,0,0]    cover art image (texture)
//   [0,0,1,0]    track banner (text)
//   [0,0,1,1]    volume stat (value)
void ABorn2FlapRadioHUD::BuildPanel()
{
    if (!Renderer || !Station)
        return;

    FNode Root = Nd("Overlay", P({ {"align", S("right")}, {"valign", S("bottom")} }),
    {
        Nd("Panel", P({ {"title", S(TCHAR_TO_UTF8(*Station->GetStationName()))}, {"tone", S("accent")}, {"spacing", N(4)} }),
        {
            Nd("HorizontalBox", P({ {"valign", S("center")}, {"spacing", N(12)} }),
            {
                Nd("SizeBox", P({ {"width", N(72)}, {"height", N(72)} }),
                {
                    Nd("Image", P({ {"texture", S(TCHAR_TO_UTF8(*Station->GetCoverPath()))} })),
                }),
                Nd("VerticalBox", P({ {"spacing", N(2)} }),
                {
                    Nd("Banner", P({ {"text", S("—")}, {"tone", S("normal")} })),
                    Nd("Stat",  P({ {"label", L("radio.vol")}, {"value", N(50)}, {"unit", S(" %")}, {"tone", S("info")} })),
                }),
            }),
        }),
    });

    FOp Mount;
    Mount.op = EOp::CreateInstance;
    Mount.path = FPath();
    Mount.node = std::move(Root);

    TArray<FOp> Ops;
    Ops.Add(MoveTemp(Mount));
    Renderer->ApplyOps(Ops);
    LastCoverPath.Empty();
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

    const FString CoverPath = Station->GetCoverPath();
    if (CoverPath != LastCoverPath)
    {
        Ops.Add(UpdateProps(Pth({0, 0, 0, 0}), P({ {"texture", S(TCHAR_TO_UTF8(*CoverPath))} })));
        LastCoverPath = CoverPath;
    }

    const FString TrackLine = Station->IsPlaying() ? Station->GetTrackName() : FString(TEXT("(paused)"));
    if (TrackLine != LastTrackLine)
    {
        Ops.Add(UpdateProps(Pth({0, 0, 1, 0}), P({ {"text", S(TCHAR_TO_UTF8(*TrackLine))} })));
        LastTrackLine = TrackLine;
    }

    const int32 VolumePct = FMath::RoundToInt(Station->GetVolume() * 100.f);
    if (VolumePct != LastVolumePct)
    {
        Ops.Add(UpdateProps(Pth({0, 0, 1, 1}), P({ {"value", N((double)VolumePct)} })));
        LastVolumePct = VolumePct;
    }

    if (Ops.Num() > 0)
        Renderer->ApplyOps(Ops);
}