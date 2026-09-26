#pragma once
// Born2FlapUIBridge.h — the game-loop "tick wiring" for react-native-umg.
//
// This actor is the process-boundary connection between the Ruby Brain (which
// writes NDJSON op frames — see Born2FlapTransport.h) and the UMG host
// (UBorn2FlapUIRenderer). Every tick it tails a frame source (a file/FIFO that
// the Brain writes to) and applies each complete newline-delimited frame:
//
//   Ruby Brain ── NDJSON file/FIFO ──▶ tick tail ──▶ Renderer::ApplyOpsJson
//
// The transport *medium* is deliberately abstract: the Brain can be launched
// as a child process (BrainCommand) writing to the same file, or run out-of-
// band. The framing itself is the dependency-free Born2FlapTransport.h
// contract, proven headlessly by Tools/umg_transport_test.cpp.
//
// Configure via command line:
//   -B2FUIFile=/path/frames.ndjson     (required: the frame source to tail)
//   -B2FUIBrain="ruby bin/transport_demo /path/frames.ndjson"  (optional)
// Or set the matching UPROPERTYs in the editor / spawned instance.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProcess.h"
#include "Born2FlapUIBridge.generated.h"

class IFileHandle;
class UBorn2FlapUIRenderer;

UCLASS()
class BORN2FLAP_API ABorn2FlapUIBridge : public AActor
{
    GENERATED_BODY()

public:
    ABorn2FlapUIBridge();

    // The NDJSON frame source to tail (a regular file or a named FIFO).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI Bridge")
    FString FilePath;

    // Optional: launch the Ruby Brain as a child process. The command is split
    // on the first space into (executable, arguments); the Brain is expected
    // to write NDJSON frames to FilePath.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI Bridge")
    FString BrainCommand;

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void OpenSource();
    void SpawnBrain();
    void DrainSource();
    void ApplyFrame(const TArray<uint8>& Bytes);

    UPROPERTY() TObjectPtr<UBorn2FlapUIRenderer> Renderer;
    IFileHandle* FileHandle = nullptr;
    int64 FileOffset = 0;
    FProcHandle BrainProcess;
    TArray<uint8> LineBuffer;
};
