// Born2FlapUIBridge.cpp — the game-loop "tick wiring" for react-native-umg.
//
// NOTE: like Born2FlapUIRenderer.cpp, this file compiles against the Unreal
// Engine (UE 5.7). The *framing* it relies on is proven headlessly by
// Tools/umg_transport_test.cpp; this actor only adds the engine-side loop that
// tails a frame source and forwards each NDJSON frame to the UMG host.

#include "UI/Born2FlapUIBridge.h"

#include "UI/Born2FlapUIRenderer.h"
#include "EngineUtils.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Game/Born2FlapGameMode.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "UnrealClient.h"
#include "Kismet/GameplayStatics.h"

ABorn2FlapUIBridge::ABorn2FlapUIBridge()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ABorn2FlapUIBridge::BeginPlay()
{
    Super::BeginPlay();

    // Command-line override wins over any editor-configured defaults.
    FParse::Value(FCommandLine::Get(), TEXT("B2FUIFile="), FilePath);
    FParse::Value(FCommandLine::Get(), TEXT("B2FTelemetry="), TelemetryPath);
    FParse::Value(FCommandLine::Get(), TEXT("B2FUIBrain="), BrainCommand);

    // Defaults (dev): a per-project temp dir under Saved/ for the two streams.
    if (FilePath.IsEmpty() || TelemetryPath.IsEmpty())
    {
        const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UMGHAML") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
        IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
        PF.CreateDirectoryTree(*Dir);
        if (FilePath.IsEmpty())     FilePath     = Dir / TEXT("frames.ndjson");
        if (TelemetryPath.IsEmpty()) TelemetryPath = Dir / TEXT("telemetry.ndjson");
    }
    // ProjectSavedDir() is relative to the launch dir (the engine binaries CWD
    // when run via play.ps1) and the Ruby Brain inherits that CWD. Make both
    // streams absolute so the bridge and the Brain always touch the same files
    // regardless of the process working directory.
    FilePath     = FPaths::ConvertRelativePathToFull(FilePath);
    TelemetryPath = FPaths::ConvertRelativePathToFull(TelemetryPath);

    Renderer = NewObject<UBorn2FlapUIRenderer>(this);
    Renderer->OnComponentAction.AddDynamic(this, &ABorn2FlapUIBridge::HandleAction);

    // Default brain (dev): run the Ruby Brain from the repo. In a packaged build
    // the script is absent, so the native C++ cockpit remains the fallback.
    if (BrainCommand.IsEmpty())
    {
        FString Script = FPaths::ProjectDir() / TEXT("../../Brain/bin/umghaml_brain");
        FPaths::CollapseRelativeDirectories(Script);
        if(!FPaths::FileExists(Script)) Script=FPaths::ProjectDir()/TEXT("Brain/bin/umghaml_brain");
        if (FPaths::FileExists(Script))
        {
#if PLATFORM_WINDOWS
            // Prefer a self-contained Ruby vendored into the repo (ThirdParty/
            // ruby, fetched by Tools/fetch_portable_ruby.ps1) so the Brain runs
            // without a system install. Fall back to "ruby" on PATH (Unreal's
            // CreateProcess resolves it) for dev machines without the bundle.
            FString Ruby = FPaths::ProjectDir() / TEXT("../../ThirdParty/ruby/bin/ruby.exe");
            FPaths::CollapseRelativeDirectories(Ruby);
            if (!FPaths::FileExists(Ruby)) Ruby=FPaths::ProjectDir()/TEXT("ThirdParty/ruby/bin/ruby.exe");
            if (!FPaths::FileExists(Ruby)) Ruby = TEXT("ruby");

            BrainCommand = FString::Printf(
                TEXT("\"%s\" \"%s\" \"%s\" \"%s\""),
                *Ruby, *Script, *TelemetryPath, *FilePath);
#else
            BrainCommand = FString::Printf(
                TEXT("/usr/bin/env ruby \"%s\" \"%s\" \"%s\""),
                *Script, *TelemetryPath, *FilePath);
#endif
        }
    }

    if (!BrainCommand.IsEmpty())
        SpawnBrain();

    OpenSource();
}

void ABorn2FlapUIBridge::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // UI readouts run at 20 Hz independently of simulation/render FPS.
    const double Now = FPlatformTime::Seconds();
    if (Now >= NextTelemetryTime) {
        NextTelemetryTime = Now + .05;
        if (IsBrainActive()) WriteTelemetry();
        DrainSource();
    }
    MaxBridgeMs=FMath::Max(MaxBridgeMs,(FPlatformTime::Seconds()-Now)*1000.);
    RunUITest(DeltaSeconds);
}

void ABorn2FlapUIBridge::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (BrainProcess.IsValid())
        FPlatformProcess::TerminateProc(BrainProcess, /*bKillTree=*/true);
    BrainProcess.Reset();

    // Remove the Brain-authored UMG host from the viewport immediately, before
    // GC gets around to it. Otherwise the old menu's RootHost lingers in the
    // (persistent) game viewport across the level travel and flashes on top of
    // the destination map the moment the loading screen drops.
    CloseRenderer();

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
    // bAllowWrite=true: the Ruby Brain holds frames.ndjson open in append
    // (write) mode, so the read must grant FILE_SHARE_WRITE or the open fails
    // with a sharing violation (black screen). This is the documented flag for
    // "open files that are currently being written to as well".
    FileHandle = PlatformFile.OpenRead(*FilePath, /*bAllowWrite=*/true);
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

    if (Command.StartsWith(TEXT("\"")))
    {
        // Quoted executable path (e.g. the vendored Ruby under ThirdParty/).
        // Consume up to the closing quote so spaces in the path survive.
        const int32 EndQuote = Command.Find(TEXT("\""), ESearchCase::IgnoreCase,
                                            ESearchDir::FromStart, 1);
        if (EndQuote != INDEX_NONE)
        {
            Exe = Command.Mid(1, EndQuote - 1);
            Params = Command.Mid(EndQuote + 1).TrimStart();
        }
    }
    else
    {
        int32 Space = INDEX_NONE;
        if (Command.FindChar(TEXT(' '), Space))
        {
            Exe = Command.Left(Space);
            Params = Command.Mid(Space + 1).TrimStart();
        }
    }

    uint32 ProcessId = 0;
    BrainProcess = FPlatformProcess::CreateProc(
        *Exe, *Params,
        /*bLaunchDetached=*/false, /*bLaunchHidden=*/false, /*bLaunchReallyHidden=*/true,
        &ProcessId, /*PriorityModifier=*/0, /*OptionalWorkingDirectory=*/nullptr,
        /*PipeWriteChild=*/nullptr, /*PipeReadChild=*/nullptr);

    if (BrainProcess.IsValid())
    {
        UE_LOG(LogTemp, Display, TEXT("Born2FlapUIBridge: launched brain pid=%u"), ProcessId);
    }
    else
    {
#if PLATFORM_WINDOWS
        UE_LOG(LogTemp, Warning,
            TEXT("Born2FlapUIBridge: failed to launch '%s' (is Ruby installed and on PATH?)"), *Command);
#else
        UE_LOG(LogTemp, Warning, TEXT("Born2FlapUIBridge: failed to launch '%s'"), *Command);
#endif
    }
}

void ABorn2FlapUIBridge::DrainSource()
{
    if (!Renderer)
        return;

    // Lazy (re)open: the Brain is spawned immediately before OpenSource() runs
    // and needs a few hundred ms to boot Ruby and create the frames file, so
    // the first open races ahead of the writer. Retry every tick until the
    // file appears instead of giving up permanently (which left the UI black
    // while IsBrainActive() kept the native cockpit dormant).
    if (!FileHandle)
    {
        if (FilePath.IsEmpty())
            return;
        OpenSource();
        if (!FileHandle)
            return;
    }

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    const int64 Size = PlatformFile.FileSize(*FilePath);
    if (Size < FileOffset) { LineBuffer.Reset(); FileOffset = 0; }  // the writer rotated/truncated the frame source
    if (Size <= FileOffset)
        return;

    const int64 ToRead = FMath::Min<int64>(Size - FileOffset, 64 * 1024);
    TArray<uint8> Chunk;
    Chunk.SetNumUninitialized((int32)ToRead);

    // Windows file handles cache the size at open. Reopen the append stream
    // before reading growth; otherwise every frame after the first is lost.
    delete FileHandle;
    FileHandle = PlatformFile.OpenRead(*FilePath, true);
    if (!FileHandle) return;
    FileHandle->Seek(FileOffset);
    if(!FileHandle->Read(Chunk.GetData(), ToRead)) return;
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

void ABorn2FlapUIBridge::ForceTelemetry()
{
    // The `loading` flag flips false→true when ShowLoadingScreen() arms the
    // loader, but without forcing a write the file would keep the menu map's
    // last snapshot (`frontend:true, loading:false`) until the next engine tick
    // — and the engine doesn't tick during the movie. Defeat the dedup so the
    // destination map's fresh Brain reads `loading:true` and paints nothing.
    LastTelemetryJson.Empty();
    WriteTelemetry();
}

void ABorn2FlapUIBridge::CloseRenderer()
{
    if (Renderer)
        Renderer->Close();
}

void ABorn2FlapUIBridge::WriteTelemetry()
{
    if (TelemetryPath.IsEmpty())
        return;

    // The GameMode builds the full telemetry (panel state + menu catalog + POI
    // + radio + RC + splash + flight readouts). Fall back to a minimal flight
    // readout when there is no GameMode (headless/tests).
    FString Json;
    if (ABorn2FlapGameMode* GM = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
    {
        Json = GM->BuildBrainTelemetry();
    }
    else
    {
        ABorn2FlapFlightPawn* Pawn = nullptr;
        TActorIterator<ABorn2FlapFlightPawn> It(GetWorld());
        if (It)
            Pawn = *It;
        if (!Pawn)
            return;
        FString Status = Pawn->GetFlightStatus();
        Status.ReplaceInline(TEXT("\""), TEXT("'"));
        Json = FString::Printf(
            TEXT("{\"altitude\":%.1f,\"climb\":%.1f,\"speed\":%.1f,\"battery\":%.0f,\"throttle\":%.2f,\"status\":\"%s\"}"),
            Pawn->GetAltitude(), Pawn->GetClimbRate(), Pawn->GetSpeed(),
            Pawn->GetBattery() * 100.0f, Pawn->GetEffort(), *Status);
    }

    // Inject pending UI events. The serialize→deserialize→serialize round-trip
    // is only necessary when an event is actually queued (a user click — rare).
    // Skipping it every frame removes a full JSON re-parse + re-serialize of the
    // ~10 KB telemetry from the game thread. When there are no events the
    // `ui_events` field is simply absent; the Ruby Brain treats that as `[]`.
    if (PendingEvents.Num() > 0)
    {
        TSharedPtr<FJsonObject> Telemetry;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Telemetry) && Telemetry.IsValid())
        {
            Telemetry->SetArrayField(TEXT("ui_events"), PendingEvents);
            Json.Reset();
            FJsonSerializer::Serialize(Telemetry.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
        }
    }

    // Atomic telemetry write: the Ruby Brain reads this file every ~16 ms while
    // we truncate + rewrite it each tick. Writing a sibling then renaming it
    // over the live path means the reader always sees a complete object — never
    // a torn write (which surfaced as a permanently empty menu because
    // read_telemetry kept returning nil and prev/next seemed dead).
    if (Json == LastTelemetryJson) return;
    const FString TmpPath = TelemetryPath + TEXT(".tmp");
    if (FFileHelper::SaveStringToFile(Json + LINE_TERMINATOR, *TmpPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
        IFileManager::Get().Move(*TelemetryPath, *TmpPath, true, false, false, /*bDoNotRetryOrError=*/true))
        { LastTelemetryJson = Json; ++TelemetryWrites; } // sharing contention retries next UI tick without blocking
}

void ABorn2FlapUIBridge::HandleAction(const FString& Action, float Value, const FString& Text)
{
    auto Event = MakeShared<FJsonObject>();
    Event->SetNumberField(TEXT("id"), NextEventId++);
    Event->SetStringField(TEXT("action"), Action);
    Event->SetNumberField(TEXT("value"), Value);
    Event->SetStringField(TEXT("text"), Text);
    PendingEvents.Add(MakeShared<FJsonValueObject>(Event));
    NextTelemetryTime = 0;

}

void ABorn2FlapUIBridge::ApplyFrame(const TArray<uint8>& Bytes)
{
    if (Bytes.Num() == 0)
        return;

    // Null-terminate for UTF8_TO_TCHAR; NDJSON frames are ASCII in practice but
    // the conversion keeps multi-byte prop values safe.
    TArray<uint8> NullTerminated = Bytes;
    NullTerminated.Add(0);
    const FString Json(UTF8_TO_TCHAR((const char*)NullTerminated.GetData()));

    // A frame is a JSON array of ops. "command" ops are the Ruby Brain's
    // imperative channel into the game core (flow control, non-time-critical);
    // everything else is a UI op for the renderer. Parse once, split, route.
    TSharedPtr<FJsonValue> RootVal;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, RootVal) || !RootVal.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("Born2FlapUIBridge: failed to parse frame JSON"));
        return;
    }
    const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
    if (!RootVal->TryGetArray(Arr))
    {
        // Not an op array (unexpected) — let the renderer handle/ignore it.
        Renderer->ApplyOpsJson(Json);
        return;
    }

    TArray<TSharedPtr<FJsonValue>> UiOps;
    UiOps.Reserve(Arr->Num());
    for (const TSharedPtr<FJsonValue>& Elem : *Arr)
    {
        const TSharedPtr<FJsonObject>* Obj = nullptr;
        if (Elem.IsValid() && Elem->TryGetObject(Obj))
        {
            FString Op;
            if ((*Obj)->TryGetStringField(TEXT("op"), Op) && Op == TEXT("command"))
            {
                DispatchCommand(**Obj);
                continue;
            }
        }
        UiOps.Add(Elem);
    }

    if (UiOps.Num() > 0)
    {
        FString FilteredJson;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&FilteredJson);
        FJsonSerializer::Serialize(UiOps, Writer);
        Renderer->ApplyOpsJson(FilteredJson);
        ++AppliedFrames;
    }
}

void ABorn2FlapUIBridge::DispatchCommand(const FJsonObject& Cmd)
{
    FString Name;
    if (!Cmd.TryGetStringField(TEXT("name"), Name))
        return;

    const TSharedPtr<FJsonObject>* PayloadPtr = nullptr;
    TSharedPtr<FJsonObject> Payload;
    if (Cmd.TryGetObjectField(TEXT("payload"), PayloadPtr))
        Payload = *PayloadPtr;

    if(Name == TEXT("ui.ack") && Payload.IsValid())
    {
        double Id=0;
        if(Payload->TryGetNumberField(TEXT("id"),Id))
            PendingEvents.RemoveAll([Id](const TSharedPtr<FJsonValue>& E) { return E->AsObject()->GetNumberField(TEXT("id"))<=Id; });
        return;
    }
    if(Name == TEXT("ui.action") && Payload.IsValid())
    {
        FString Action, Text; double Value=0;
        if(!Payload->TryGetStringField(TEXT("action"),Action)) return;
        Payload->TryGetStringField(TEXT("text"),Text);
        Payload->TryGetNumberField(TEXT("value"),Value);
        if(auto* GM=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode())) GM->HandleBrainAction(Action,Value,Text);
        return;
    }
    if (ABorn2FlapGameMode* GM = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
        GM->HandleBrainCommand(Name, Payload);
}
void ABorn2FlapUIBridge::RunUITest(float Dt)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FRubyUIPerfTest"))) {
        UITestElapsed+=Dt;
        if(UITestElapsed<6+UITestStage*3) return;
        auto* Pawn=Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this,0));
        if(!Pawn) { UE_LOG(LogTemp,Error,TEXT("RubyUIPerf FAIL: no pawn")); FPlatformMisc::RequestExit(false); return; }
        if(UITestStage==0) Pawn->ToggleUIHidden();
        if(UITestStage==1) { SampleWrites=TelemetryWrites; SampleFrames=AppliedFrames; MaxBridgeMs=0; }
        if(UITestStage==3) {
            bUITestPassed=TelemetryWrites==SampleWrites && AppliedFrames==SampleFrames;
            UE_LOG(LogTemp,Display,TEXT("RubyUIPerf hidden: writes=%lld frames=%lld maxBridgeMs=%.3f %s"),TelemetryWrites-SampleWrites,AppliedFrames-SampleFrames,MaxBridgeMs,bUITestPassed?TEXT("PASS"):TEXT("FAIL"));
            FScreenshotRequest::RequestScreenshot(TEXT("RUBY_UI_hidden.png"),true,false);
            Pawn->ToggleUIHidden(); SampleFrames=AppliedFrames;
        }
        if(UITestStage==4) {
            bUITestPassed &= AppliedFrames>SampleFrames;
            FScreenshotRequest::RequestScreenshot(TEXT("RUBY_UI_restored.png"),true,false);
            UE_LOG(LogTemp,Display,TEXT("RubyUIPerf restored: frames=%lld %s"),AppliedFrames-SampleFrames,bUITestPassed?TEXT("PASS"):TEXT("FAIL"));
        }
        if(UITestStage++==5) FPlatformMisc::RequestExit(false);
        return;
    }
    const bool Flight=FParse::Param(FCommandLine::Get(),TEXT("B2FRubyFlightUITest"));
    if(!Flight && !FParse::Param(FCommandLine::Get(),TEXT("B2FRubyUITest"))) return;
    UITestElapsed+=Dt;
    if(UITestElapsed<6+UITestStage*2 || !Renderer) return;
    auto* GM=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    auto* Bird=Cast<ABorn2FlapFlightPawn>(UGameplayStatics::GetPlayerPawn(this,0));
    auto Click=[this](const TCHAR* A,float V=0){
        bool OK=Renderer->TestActivateAction(A,V); bUITestPassed &= OK;
        UE_LOG(LogTemp,Display,TEXT("RubyUI relay %s: %s"),A,OK?TEXT("PASS"):TEXT("FAIL"));
    };
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FRubyIngameReview"))) {
        switch(UITestStage) {
        case 0: if(GM) GM->ShowMainMenu(true); break;
        case 1: Click(TEXT("menu.settings")); break;
        case 3: Click(TEXT("editor.close")); break;
        case 4: if(GM) GM->ShowMainMenu(true); break;
        case 5: Click(TEXT("menu.preferences")); break;
        case 6: Click(TEXT("preferences.page"),2); break;
        case 7: Click(TEXT("preferences.close")); break;
        case 8: if(Bird) Bird->ToggleUIHidden(); break;
        case 9: if(Bird) Bird->ToggleUIHidden(); break;
        }
        FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("RUBY_UI_ingame_%d.png"),UITestStage),true,false);
        if(UITestStage++>=11) { UE_LOG(LogTemp,Display,TEXT("RubyUIIngameReview %s"),bUITestPassed?TEXT("PASS"):TEXT("FAIL")); FPlatformMisc::RequestExit(false); }
        return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FRubyUIReview"))) {
        switch(UITestStage) {
        case 1: Click(TEXT("menu.page"),1); break;
        case 2: Click(TEXT("menu.page"),2); break;
        case 3: Click(TEXT("menu.settings")); break;
        case 4: Click(TEXT("settings.page"),1); break;
        case 5: Click(TEXT("settings.page"),4); break;
        case 6: Click(TEXT("editor.close")); break;
        case 7: Click(TEXT("menu.preferences")); break;
        case 8: Click(TEXT("preferences.page"),1); break;
        case 9: Click(TEXT("preferences.page"),2); break;
        case 10: Click(TEXT("preferences.close")); break;
        case 11: Click(TEXT("menu.controls")); break;
        case 12: Click(TEXT("rc.close")); break;
        }
        FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("RUBY_UI_review_%d.png"),UITestStage),true,false);
        if(UITestStage++>=13) { UE_LOG(LogTemp,Display,TEXT("RubyUIReview %s"),bUITestPassed?TEXT("PASS"):TEXT("FAIL")); FPlatformMisc::RequestExit(false); }
        return;
    }
    if(Flight) {
        switch(UITestStage) {
        case 0: if(GM) GM->HandleBrainAction(TEXT("menu.settings"),0,TEXT("")); break;
        case 1: Click(TEXT("bird.select"),2); break;
        case 2: bUITestPassed &= Bird && Bird->GetBirdModel()==2; Click(TEXT("profile.select"),1); break;
        case 3: Click(TEXT("settings.page"),1); break;
        case 4: Click(TEXT("bird.weight"),.42f); break;
        case 5: bUITestPassed &= Bird && FMath::IsNearlyEqual(Bird->GetBodyMassKg(),.42f,.001f); Click(TEXT("settings.page"),4); break;
        case 6: Click(TEXT("tuning.display.FlapCentre"),3); break;
        case 7: bUITestPassed &= Bird && FMath::IsNearlyEqual(Bird->GetTuning(ETuningField::FlapCentre),3.f); Click(TEXT("editor.close")); break;
        case 8: bUITestPassed &= Bird && !Bird->IsFlightSettingsOpen(); break;
        }
    } else {
        if(UITestStage==1) Click(TEXT("menu.nextWorld"));
        if(UITestStage==2) Click(TEXT("menu.page"),1);
        if(UITestStage==3) Click(TEXT("wind.speed"),1.7f);
        if(UITestStage==4) Click(TEXT("wind.reset"));
        if(UITestStage==5) Click(TEXT("menu.page"),0);
    }
    FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("RUBY_UI_%s_%d.png"),Flight?TEXT("flight"):TEXT("menu"),UITestStage),true,false);
    if(UITestStage++ >= (Flight?9:6)) {
        UE_LOG(LogTemp,Display,TEXT("RubyUIIntegration %s"),bUITestPassed?TEXT("PASS"):TEXT("FAIL"));
        FPlatformMisc::RequestExit(false);
    }
}