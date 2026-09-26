// Born2FlapUIBridge.cpp — the game-loop "tick wiring" for react-native-umg.
//
// NOTE: like Born2FlapUIRenderer.cpp, this file compiles against the Unreal
// Engine (UE 5.7). The *framing* it relies on is proven headlessly by
// Tools/umg_transport_test.cpp; this actor only adds the engine-side loop that
// tails a frame source and forwards each NDJSON frame to the UMG host.

#include "UI/Born2FlapUIBridge.h"

#include "UI/Born2FlapUIRenderer.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

ABorn2FlapUIBridge::ABorn2FlapUIBridge()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ABorn2FlapUIBridge::BeginPlay()
{
    Super::BeginPlay();

    // Command-line override wins over any editor-configured defaults.
    FParse::Value(FCommandLine::Get(), TEXT("B2FUIFile="), FilePath);
    FParse::Value(FCommandLine::Get(), TEXT("B2FUIBrain="), BrainCommand);

    Renderer = NewObject<UBorn2FlapUIRenderer>(this);

    if (!BrainCommand.IsEmpty())
        SpawnBrain();

    OpenSource();
}

void ABorn2FlapUIBridge::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    DrainSource();
}

void ABorn2FlapUIBridge::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (BrainProcess.IsValid())
        FPlatformProcess::CloseProc(BrainProcess);
    BrainProcess.Reset();

    if (FileHandle)
    {
        delete FileHandle;
        FileHandle = nullptr;
    }

    Super::EndPlay(EndPlayReason);
}

// ---- helpers ---------------------------------------------------------------

void ABorn2FlapUIBridge::OpenSource()
{
    if (FilePath.IsEmpty())
        return;

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    FileHandle = PlatformFile.OpenRead(*FilePath);
    if (!FileHandle)
    {
        UE_LOG(LogTemp, Warning, TEXT("Born2FlapUIBridge: cannot open frame source '%s'"), *FilePath);
        return;
    }

    // Tail from the start. (A FIFO reports size 0 until the writer appears, so
    // the first DrainSource will simply read whatever is available.)
    FileOffset = 0;
    UE_LOG(LogTemp, Display, TEXT("Born2FlapUIBridge: tailing '%s'"), *FilePath);
}

void ABorn2FlapUIBridge::SpawnBrain()
{
    FString Command = BrainCommand.TrimStartAndEnd();
    FString Exe = Command;
    FString Params;

    int32 Space = INDEX_NONE;
    if (Command.FindChar(TEXT(' '), Space))
    {
        Exe = Command.Left(Space);
        Params = Command.Mid(Space + 1).TrimStart();
    }

    uint32 ProcessId = 0;
    BrainProcess = FPlatformProcess::CreateProc(
        *Exe, *Params,
        /*bLaunchDetached=*/false, /*bLaunchHidden=*/false, /*bLaunchReallyHidden=*/true,
        &ProcessId, /*PriorityModifier=*/0, /*OptionalWorkingDirectory=*/nullptr,
        /*PipeWriteChild=*/nullptr, /*PipeReadChild=*/nullptr);

    if (BrainProcess.IsValid())
        UE_LOG(LogTemp, Display, TEXT("Born2FlapUIBridge: launched brain pid=%u"), ProcessId);
    else
        UE_LOG(LogTemp, Warning, TEXT("Born2FlapUIBridge: failed to launch '%s'"), *Command);
}

void ABorn2FlapUIBridge::DrainSource()
{
    if (!Renderer || !FileHandle)
        return;

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    const int64 Size = PlatformFile.FileSize(*FilePath);
    if (Size <= FileOffset)
        return;

    const int64 ToRead = FMath::Min<int64>(Size - FileOffset, 64 * 1024);
    TArray<uint8> Chunk;
    Chunk.SetNumUninitialized((int32)ToRead);

    FileHandle->Seek(FileOffset);
    FileHandle->Read(Chunk.GetData(), ToRead);
    FileOffset += ToRead;

    for (uint8 Byte : Chunk)
    {
        if (Byte == '\n')
        {
            ApplyFrame(LineBuffer);
            LineBuffer.Reset();
        }
        else if (Byte != '\r')
        {
            LineBuffer.Add(Byte);
        }
    }
}

void ABorn2FlapUIBridge::ApplyFrame(const TArray<uint8>& Bytes)
{
    if (Bytes.Num() == 0)
        return;

    // Null-terminate for UTF8_TO_TCHAR; NDJSON frames are ASCII in practice but
    // the conversion keeps multi-byte prop values safe.
    TArray<uint8> NullTerminated = Bytes;
    NullTerminated.Add(0);
    Renderer->ApplyOpsJson(FString(UTF8_TO_TCHAR((const char*)NullTerminated.GetData())));
}
