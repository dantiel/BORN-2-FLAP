#include "Racing/Born2FlapRacing.h"
#include "Racing/Born2FlapSpirit.h"
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
constexpr uint32 SpiritMagic = 0x32534642u; // "BFS2"
constexpr uint32 SpiritVersion = 1;
constexpr int32 MaxSpirits = 12;            // keep every round playable, bounded
constexpr double MinRoundSeconds = 1.0;
} // namespace

bool FB2FSpiritRecording::Sample(double Time, FVector &OutPos, FQuat &OutRot, float &OutLFlap, float &OutRFlap) const
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
    const FB2FSpiritFrame &A = Frames[Lo];
    const FB2FSpiritFrame &B = Frames[Hi];
    const double Span = B.Time - A.Time;
    const double Alpha = Span > UE_SMALL_NUMBER ? FMath::Clamp((T - A.Time) / Span, 0.0, 1.0) : 0.0;
    OutPos = A.Position + (B.Position - A.Position) * Alpha;
    OutRot = FQuat::Slerp(A.Rotation, B.Rotation, Alpha);
    OutLFlap = FMath::Lerp(A.LeftFlap, B.LeftFlap, float(Alpha));
    OutRFlap = FMath::Lerp(A.RightFlap, B.RightFlap, float(Alpha));
    return true;
}

bool SaveSpiritRecording(FB2FSpiritRecording &Recording, const FString &Path)
{
    TArray<uint8> Bytes;
    FMemoryWriter Ar(Bytes, /*bIsPersistent=*/true);
    uint32 Magic = SpiritMagic, Version = SpiritVersion;
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
    for (FB2FSpiritFrame &F : Recording.Frames)
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

bool LoadSpiritRecording(const FString &Path, FB2FSpiritRecording &Recording)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path))
        return false;
    FMemoryReader Ar(Bytes, /*bIsPersistent=*/true);
    uint32 Magic = 0, Version = 0;
    Ar << Magic;
    if (Magic != SpiritMagic)
        return false;
    Ar << Version;
    if (Version != SpiritVersion)
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
        FB2FSpiritFrame F;
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
    // Read the final, physics-integrated pose each frame so spirits trace the
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
    // Namespace replays per level (map): only spirits recorded in the current
    // level fly here, so recordings never bleed across Shiomori / Training /
    // Ravenstonefield. GetMapName() is a stable per-level key that is
    // independent of the B2FLevel alias used to launch.
    SpiritDir = FPaths::ProjectSavedDir() / TEXT("Racing/Spirits") / GetWorld()->GetMapName();
    IFileManager::Get().MakeDirectory(*SpiritDir, /*Tree=*/true);
    if (bEnabled)
        LoadAllSpirits();
}

void ABorn2FlapRacingManager::LoadAllSpirits()
{
    LoadedSpirits.Reset();
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(SpiritDir / TEXT("*.b2fs")), /*Files=*/true, /*Directories=*/false);
    Files.Sort(); // YYYYMMDD_HHMMSS filenames sort chronologically
    for (const FString &File : Files)
    {
        FB2FSpiritRecording Recording;
        if (LoadSpiritRecording(SpiritDir / File, Recording))
        {
            Recording.Name = FPaths::GetBaseFilename(File);
            Recording.SourceFile = SpiritDir / File;
            LoadedSpirits.Add(MoveTemp(Recording));
        }
    }
    TrimToLimit();
    BestDistance = BestDuration = 0;
    for (const auto &G : LoadedSpirits)
        if (G.Distance > BestDistance)
        {
            BestDistance = G.Distance;
            BestDuration = G.Duration;
        }
}

void ABorn2FlapRacingManager::TrimToLimit()
{
    if (LoadedSpirits.Num() <= MaxSpirits)
        return;
    // Keep the best rounds and cull the worst attempts first (shortest distance).
    // The champion is always retained — it is the best round by definition.
    LoadedSpirits.Sort([](const FB2FSpiritRecording &A, const FB2FSpiritRecording &B) { return A.Distance > B.Distance; });
    while (LoadedSpirits.Num() > MaxSpirits)
    {
        const FB2FSpiritRecording Victim = LoadedSpirits.Pop(EAllowShrinking::No);
        if (!Victim.SourceFile.IsEmpty())
            IFileManager::Get().Delete(*Victim.SourceFile);
        UE_LOG(LogTemp, Display, TEXT("RacingSpiritCull removed %s (%.1f m)"), *Victim.Name, Victim.Distance);
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
    for (const auto &Weak : SpiritActors)
        if (auto *Spirit = Weak.Get())
            Spirit->Advance(DeltaSeconds);
}

void ABorn2FlapRacingManager::BeginRound()
{
    ActiveRecording = FB2FSpiritRecording{};
    ActiveRecording.Name = FString::Printf(TEXT("Runde %02d"), LoadedSpirits.Num() + 1);
    RoundTime = 0;
    LastPosition = Bird->GetActorLocation();
    bRecording = true;
    SpawnSpirits();
    UE_LOG(LogTemp, Display, TEXT("RacingRoundBegin spirits=%d rounds=%d"), SpiritActors.Num(), LoadedSpirits.Num());
}

void ABorn2FlapRacingManager::RecordFrame(const ABorn2FlapFlightPawn *Pawn)
{
    FB2FSpiritFrame F;
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
        const FString Path = SpiritDir / (FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")) + TEXT(".b2fs"));
        ActiveRecording.SourceFile = Path;
        if (SaveSpiritRecording(ActiveRecording, Path))
        {
            const bool NewBest = ActiveRecording.Distance > BestDistance;
            LoadedSpirits.Add(ActiveRecording);
            if (NewBest)
            {
                BestDistance = ActiveRecording.Distance;
                BestDuration = ActiveRecording.Duration;
            }
            TrimToLimit();
            UE_LOG(LogTemp, Display,
                   TEXT("RacingRoundEnd dist=%.1fm dur=%.1fs gates=%d spirits=%d%s"), ActiveRecording.Distance,
                   ActiveRecording.Duration, ActiveRecording.GatesPassed, LoadedSpirits.Num(),
                   NewBest ? TEXT(" NEW_BEST") : TEXT(""));
        }
    }
    ActiveRecording = FB2FSpiritRecording{};
    bRecording = false;
    RoundTime = 0;
    ClearSpiritActors();
}

void ABorn2FlapRacingManager::SpawnSpirits()
{
    ClearSpiritActors();
    if (LoadedSpirits.Num() == 0)
        return;
    int32 Champion = 0;
    for (int32 I = 1; I < LoadedSpirits.Num(); ++I)
        if (LoadedSpirits[I].Distance > LoadedSpirits[Champion].Distance)
            Champion = I;
    for (int32 I = 0; I < LoadedSpirits.Num(); ++I)
        if (I != Champion)
            SpawnSpirit(LoadedSpirits[I], false);
    SpawnSpirit(LoadedSpirits[Champion], true);
}

void ABorn2FlapRacingManager::SpawnSpirit(const FB2FSpiritRecording &Recording, bool bChampion)
{
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (auto *Spirit = GetWorld()->SpawnActor<ABorn2FlapSpirit>(ABorn2FlapSpirit::StaticClass(), FVector::ZeroVector,
                                                                FRotator::ZeroRotator, Params))
    {
        Spirit->SetupSpirit(Recording, bChampion);
        SpiritActors.Add(Spirit);
    }
}

void ABorn2FlapRacingManager::ClearSpiritActors()
{
    for (const auto &Weak : SpiritActors)
        if (auto *Spirit = Weak.Get())
            Spirit->Destroy();
    SpiritActors.Reset();
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
        return FString::Printf(TEXT("Spirits %d fliegen mit"), GetSpiritCount());
    return FString::Printf(TEXT("Best  %.0f m in %.1f s  ·  Spirits %d"), BestDistance, BestDuration, GetSpiritCount());
}