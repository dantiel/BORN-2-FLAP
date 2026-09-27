#include "Racing/Born2FlapRacing.h"
#include "Racing/Born2FlapGhost.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Game/Born2FlapGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

namespace
{
constexpr uint32 GhostMagic = 0x32474642u; // "BFG2"
constexpr uint32 GhostVersion = 1;
constexpr int32 MaxGhosts = 12;            // keep every round playable, bounded
constexpr double MinRoundSeconds = 1.0;
} // namespace

bool FB2FGhostRecording::Sample(double Time, FVector &OutPos, FQuat &OutRot, float &OutLFlap, float &OutRFlap) const
{
    if (Frames.Num() < 2)
        return false;
    const double T = FMath::Clamp(Time, 0.0, Duration);
    // Binary search for the bracketing frames (time is monotonic).
    int32 Lo = 0, Hi = Frames.Num() - 1;
    while (Lo + 1 < Hi)
    {
        const int32 Mid = (Lo + Hi) / 2;
        if (Frames[Mid].Time <= T)
            Lo = Mid;
        else
            Hi = Mid;
    }
    const FB2FGhostFrame &A = Frames[Lo];
    const FB2FGhostFrame &B = Frames[Hi];
    const double Span = B.Time - A.Time;
    const double Alpha = Span > UE_SMALL_NUMBER ? FMath::Clamp((T - A.Time) / Span, 0.0, 1.0) : 0.0;
    OutPos = A.Position + (B.Position - A.Position) * Alpha;
    OutRot = FQuat::Slerp(A.Rotation, B.Rotation, Alpha);
    OutLFlap = FMath::Lerp(A.LeftFlap, B.LeftFlap, float(Alpha));
    OutRFlap = FMath::Lerp(A.RightFlap, B.RightFlap, float(Alpha));
    return true;
}

bool SaveGhostRecording(FB2FGhostRecording &Recording, const FString &Path)
{
    TArray<uint8> Bytes;
    FMemoryWriter Ar(Bytes, /*bIsPersistent=*/true);
    uint32 Magic = GhostMagic, Version = GhostVersion;
    Ar << Magic;
    Ar << Version;
    Ar << Recording.Name;
    Ar << Recording.Duration;
    Ar << Recording.Distance;
    Ar << Recording.MaxSpeed;
    Ar << Recording.MaxAltitude;
    Ar << Recording.GatesPassed;
    int32 Count = Recording.Frames.Num();
    Ar << Count;
    for (FB2FGhostFrame &F : Recording.Frames)
    {
        Ar << F.Time;
        Ar << F.Position;
        Ar << F.Rotation;
        Ar << F.LeftFlap;
        Ar << F.RightFlap;
    }
    Ar.Close();
    return FFileHelper::SaveArrayToFile(Bytes, *Path);
}

bool LoadGhostRecording(const FString &Path, FB2FGhostRecording &Recording)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path))
        return false;
    FMemoryReader Ar(Bytes, /*bIsPersistent=*/true);
    uint32 Magic = 0, Version = 0;
    Ar << Magic;
    if (Magic != GhostMagic)
        return false;
    Ar << Version;
    if (Version != GhostVersion)
        return false;
    Ar << Recording.Name;
    Ar << Recording.Duration;
    Ar << Recording.Distance;
    Ar << Recording.MaxSpeed;
    Ar << Recording.MaxAltitude;
    Ar << Recording.GatesPassed;
    int32 Count = 0;
    Ar << Count;
    if (Count < 0 || Count > 1000000)
        return false;
    Recording.Frames.Reset(Count);
    for (int32 I = 0; I < Count; ++I)
    {
        FB2FGhostFrame F;
        Ar << F.Time;
        Ar << F.Position;
        Ar << F.Rotation;
        Ar << F.LeftFlap;
        Ar << F.RightFlap;
        Recording.Frames.Add(F);
    }
    Ar.Close();
    return Recording.IsValid();
}

ABorn2FlapRacingManager::ABorn2FlapRacingManager()
{
    PrimaryActorTick.bCanEverTick = true;
    // Read the final, physics-integrated pose each frame so ghosts trace the
    // exact flown path, not the pre-physics command pose.
    PrimaryActorTick.TickGroup = TG_PostPhysics;
}

void ABorn2FlapRacingManager::BeginPlay()
{
    Super::BeginPlay();
    bEnabled = !FParse::Param(FCommandLine::Get(), TEXT("B2FFlightTest")) &&
               !FParse::Param(FCommandLine::Get(), TEXT("B2FSoakTest")) &&
               !FParse::Param(FCommandLine::Get(), TEXT("B2FRavenFlightTest")) &&
               !FParse::Param(FCommandLine::Get(), TEXT("B2FHandlingTest")) &&
               !FParse::Param(FCommandLine::Get(), TEXT("B2FDesktopInputTest")) &&
               !FParse::Param(FCommandLine::Get(), TEXT("B2FAudioTest")) &&
               !FParse::Param(FCommandLine::Get(), TEXT("B2FNoRacing"));
    GhostDir = FPaths::ProjectSavedDir() / TEXT("Racing/Ghosts");
    IFileManager::Get().MakeDirectory(*GhostDir, /*Tree=*/true);
    if (bEnabled)
        LoadAllGhosts();
}

void ABorn2FlapRacingManager::LoadAllGhosts()
{
    LoadedGhosts.Reset();
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(GhostDir / TEXT("*.b2fg")), /*Files=*/true, /*Directories=*/false);
    Files.Sort(); // YYYYMMDD_HHMMSS filenames sort chronologically
    for (const FString &File : Files)
    {
        FB2FGhostRecording Recording;
        if (LoadGhostRecording(GhostDir / File, Recording))
        {
            Recording.Name = FPaths::GetBaseFilename(File);
            LoadedGhosts.Add(MoveTemp(Recording));
        }
    }
    // Trim the oldest while always keeping the champion.
    int32 Champion = 0;
    for (int32 I = 1; I < LoadedGhosts.Num(); ++I)
        if (LoadedGhosts[I].Distance > LoadedGhosts[Champion].Distance)
            Champion = I;
    while (LoadedGhosts.Num() > MaxGhosts)
    {
        int32 Victim = (Champion > 0) ? 0 : 1; // drop the non-champion oldest
        if (Victim == Champion)
            Victim = (Champion + 1) % LoadedGhosts.Num();
        LoadedGhosts.RemoveAt(Victim);
        if (Victim < Champion)
            --Champion;
    }
    BestDistance = BestDuration = 0;
    for (const auto &G : LoadedGhosts)
        if (G.Distance > BestDistance)
        {
            BestDistance = G.Distance;
            BestDuration = G.Duration;
        }
}

void ABorn2FlapRacingManager::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bEnabled)
        return;
    if (!Bird.IsValid())
    {
        Bird = Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
        if (!Bird.IsValid())
            return;
    }
    const bool Flying = Bird->IsFlying();
    if (Flying && !bRecording)
        BeginRound();
    else if (!Flying && bRecording)
        EndRound();
    if (bRecording)
    {
        RoundTime += DeltaSeconds;
        RecordFrame(Bird.Get());
    }
    for (const auto &Weak : GhostActors)
        if (auto *Ghost = Weak.Get())
            Ghost->Advance(DeltaSeconds);
}

void ABorn2FlapRacingManager::BeginRound()
{
    ActiveRecording = FB2FGhostRecording{};
    ActiveRecording.Name = FString::Printf(TEXT("Runde %02d"), LoadedGhosts.Num() + 1);
    RoundTime = 0;
    LastPosition = Bird->GetActorLocation();
    bRecording = true;
    SpawnGhosts();
    UE_LOG(LogTemp, Display, TEXT("RacingRoundBegin ghosts=%d rounds=%d"), GhostActors.Num(), LoadedGhosts.Num());
}

void ABorn2FlapRacingManager::RecordFrame(const ABorn2FlapFlightPawn *Pawn)
{
    FB2FGhostFrame F;
    F.Time = RoundTime;
    F.Position = Pawn->GetActorLocation();
    F.Rotation = Pawn->GetActorQuat();
    const FVector2D Wings = Pawn->GetWingAngles();
    F.LeftFlap = Wings.X;
    F.RightFlap = Wings.Y;
    const double StepM = (F.Position - LastPosition).Size() / 100.0;
    if (StepM < 50) // ignore teleport-sized jumps
        ActiveRecording.Distance += StepM;
    LastPosition = F.Position;
    ActiveRecording.Duration = RoundTime;
    ActiveRecording.MaxSpeed = FMath::Max(ActiveRecording.MaxSpeed, Pawn->GetSpeed());
    ActiveRecording.MaxAltitude = FMath::Max(ActiveRecording.MaxAltitude, Pawn->GetAltitude());
    ActiveRecording.Frames.Add(MoveTemp(F));
}

void ABorn2FlapRacingManager::EndRound()
{
    const bool WorthKeeping = ActiveRecording.IsValid() && ActiveRecording.Duration >= MinRoundSeconds;
    if (WorthKeeping)
    {
        if (auto *Mode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
            ActiveRecording.GatesPassed = Mode->GetGatesPassed();
        const FString Path = GhostDir / (FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")) + TEXT(".b2fg"));
        if (SaveGhostRecording(ActiveRecording, Path))
        {
            const bool NewBest = ActiveRecording.Distance > BestDistance;
            LoadedGhosts.Add(ActiveRecording);
            if (NewBest)
            {
                BestDistance = ActiveRecording.Distance;
                BestDuration = ActiveRecording.Duration;
            }
            UE_LOG(LogTemp, Display,
                   TEXT("RacingRoundEnd dist=%.1fm dur=%.1fs gates=%d ghosts=%d%s"), ActiveRecording.Distance,
                   ActiveRecording.Duration, ActiveRecording.GatesPassed, LoadedGhosts.Num(),
                   NewBest ? TEXT(" NEW_BEST") : TEXT(""));
        }
    }
    ActiveRecording = FB2FGhostRecording{};
    bRecording = false;
    RoundTime = 0;
    ClearGhostActors();
}

void ABorn2FlapRacingManager::SpawnGhosts()
{
    ClearGhostActors();
    if (LoadedGhosts.Num() == 0)
        return;
    int32 Champion = 0;
    for (int32 I = 1; I < LoadedGhosts.Num(); ++I)
        if (LoadedGhosts[I].Distance > LoadedGhosts[Champion].Distance)
            Champion = I;
    for (int32 I = 0; I < LoadedGhosts.Num(); ++I)
        if (I != Champion)
            SpawnGhost(LoadedGhosts[I], false);
    SpawnGhost(LoadedGhosts[Champion], true);
}

void ABorn2FlapRacingManager::SpawnGhost(const FB2FGhostRecording &Recording, bool bChampion)
{
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (auto *Ghost = GetWorld()->SpawnActor<ABorn2FlapGhost>(ABorn2FlapGhost::StaticClass(), FVector::ZeroVector,
                                                              FRotator::ZeroRotator, Params))
    {
        Ghost->SetupGhost(Recording, bChampion);
        GhostActors.Add(Ghost);
    }
}

void ABorn2FlapRacingManager::ClearGhostActors()
{
    for (const auto &Weak : GhostActors)
        if (auto *Ghost = Weak.Get())
            Ghost->Destroy();
    GhostActors.Reset();
}

FString ABorn2FlapRacingManager::GetRacingStatus() const
{
    if (bRecording)
        return FString::Printf(TEXT("RACING  Runde %d  ·  %.1f s  ·  %.0f m"), GetRoundCount() + 1, RoundTime,
                               ActiveRecording.Distance);
    return FString::Printf(TEXT("RACING  %d Runde(n) gespeichert  ·  SPACE starten"), GetRoundCount());
}

FString ABorn2FlapRacingManager::GetBestLine() const
{
    if (BestDistance <= 0)
        return FString::Printf(TEXT("Geister %d fliegen mit"), GetGhostCount());
    return FString::Printf(TEXT("Best  %.0f m in %.1f s  ·  Geister %d"), BestDistance, BestDuration, GetGhostCount());
}