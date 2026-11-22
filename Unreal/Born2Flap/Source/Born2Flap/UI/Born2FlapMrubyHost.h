#pragma once
// Born2FlapMrubyHost.h — the in-process mruby embed for the Ruby Brain UI.
//
// Replaces the child-process + NDJSON file transport on iOS, where the Ruby
// Brain cannot be spawned as a subprocess (no fork/exec, no system Ruby). It
// runs the exact same Brain (Brain/lib/**/*.rb + *.umghaml + i18n *.json) in a
// statically-linked mruby VM, driven tick-by-tick on the game thread:
//
//   Game thread:  BuildBrainTelemetry() ─▶ mruby Embed.tick(json) ─▶ frames
//                 (C++ host)                (Born2Flap::Embed)       (NDJSON)
//
// App Store posture:
//   - statically linked (no dlopen)           - bytecode VM, no JIT
//   - bundled, signed .rb only (no download)  - no subprocess, no eval/socket
//   - in-memory transport (no file writes)    - exceptions are trapped
//
// mruby is NOT thread-safe and NOT re-entrant: call only from the game thread
// (Born2FlapUIBridge::Tick). All mruby types are hidden behind the opaque Impl
// so this header compiles on every platform (the VM is functional on iOS only).

#include "CoreMinimal.h"

struct FBorn2FlapMrubyImpl;

class FBorn2FlapMrubyHost
{
public:
    FBorn2FlapMrubyHost();
    ~FBorn2FlapMrubyHost();

    // Opens a fresh mruby VM, registers the C bridge (B2FHost), and loads
    // mruby/compat.rb + mruby/embed_host.rb under the given Brain/ root.
    // Idempotent (a previous VM is shut down first). Returns false on failure
    // and records a human-readable LastError.
    bool Initialize(const FString& BrainRoot);

    // Closes the VM (idempotent).
    void Shutdown();

    bool IsActive() const;

    // Runs one Brain tick with the current telemetry snapshot. Any NDJSON op
    // frames the Brain emitted are appended to OutFrames (one complete JSON
    // array per line — drain and apply each). Returns false on a trapped Ruby
    // exception (logged; the app keeps running with the last good UI).
    bool Tick(const FString& TelemetryJson, TArray<uint8>& OutFrames);

    const FString& GetLastError() const { return LastError; }

private:
    // Loads one .rb file, trapping Ruby exceptions at the C boundary.
    bool LoadScript(const FString& AbsolutePath);

    FBorn2FlapMrubyImpl* Impl = nullptr;
    FString LastError;
};