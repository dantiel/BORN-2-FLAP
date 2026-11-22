// Born2FlapMrubyHost.cpp — in-process mruby embed for the Ruby Brain UI.
//
// Only compiled into a functional VM on iOS, where the static libmruby.a is
// linked (see Born2Flap.Build.cs). On other platforms the class is a stub so
// the UIBridge keeps a single member type; desktop keeps the child-process
// Brain (real Ruby interpreter) for the dev workflow.

#include "UI/Born2FlapMrubyHost.h"

#if PLATFORM_IOS

#include <stdio.h>

#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/string.h>
#include <mruby/error.h>

// Opaque implementation (defined here only, so the header never includes mruby).
struct FBorn2FlapMrubyImpl
{
    mrb_state* Mrb = nullptr;
    TArray<uint8> FrameBuffer; // accumulated by B2F_frames_write during Tick
};

// ---- C bridge functions (file-local; exposed to Ruby as B2FHost.*) --------

static mrb_value B2F_frames_write(mrb_state* mrb, mrb_value self)
{
    mrb_value str;
    mrb_get_args(mrb, "S", &str);
    FBorn2FlapMrubyImpl* Impl = static_cast<FBorn2FlapMrubyImpl*>(mrb->ud);
    if (Impl)
        Impl->FrameBuffer.Append(reinterpret_cast<const uint8*>(RSTRING_PTR(str)), static_cast<int32>(RSTRING_LEN(str)));
    return mrb_fixnum_value(static_cast<mrb_int>(RSTRING_LEN(str)));
}

static mrb_value B2F_log(mrb_state* mrb, mrb_value self)
{
    mrb_value str;
    mrb_get_args(mrb, "S", &str);
    UE_LOG(LogTemp, Log, TEXT("[mruby] %s"), UTF8_TO_TCHAR(mrb_str_to_cstr(mrb, str)));
    return mrb_nil_value();
}

// ---- mrb_protect_error bodies --------------------------------------------
// NOTE: mrb_protect (mrb_value body) lives in the mruby-error mrbgem, which the
// iOS build omits. mrb_protect_error (void* body) is in core src/error.c and is
// what we link — same exception-trapping semantics, different body signature.

static mrb_value LoadFileBody(mrb_state* mrb, void* userdata)
{
    const char* path = reinterpret_cast<const char*>(userdata);
    FILE* f = fopen(path, "rb");
    if (!f)
        mrb_raise(mrb, E_RUNTIME_ERROR, "B2F: cannot open script");

    mrbc_context* cxt = mrbc_context_new(mrb);
    mrbc_filename(mrb, cxt, path); // sets __FILE__ so the caller-based __dir__ shim resolves
    mrb_load_file_cxt(mrb, f, cxt);
    mrbc_context_free(mrb, cxt);
    fclose(f);
    return mrb_nil_value();
}

static mrb_value TickBody(mrb_state* mrb, void* userdata)
{
    const mrb_value telemetry = *static_cast<const mrb_value*>(userdata);
    struct RClass* Born2Flap = mrb_module_get(mrb, "Born2Flap");
    if (!Born2Flap)
        mrb_raise(mrb, E_RUNTIME_ERROR, "Born2Flap module missing");
    struct RClass* Embed = mrb_module_get_under(mrb, Born2Flap, "Embed");
    if (!Embed)
        mrb_raise(mrb, E_RUNTIME_ERROR, "Born2Flap::Embed missing");
    return mrb_funcall(mrb, mrb_obj_value(Embed), "tick", 1, telemetry);
}

// ---- host -----------------------------------------------------------------

FBorn2FlapMrubyHost::FBorn2FlapMrubyHost() = default;

FBorn2FlapMrubyHost::~FBorn2FlapMrubyHost()
{
    Shutdown();
}

void FBorn2FlapMrubyHost::Shutdown()
{
    if (Impl)
    {
        if (Impl->Mrb)
            mrb_close(Impl->Mrb);
        delete Impl;
        Impl = nullptr;
    }
}

bool FBorn2FlapMrubyHost::IsActive() const
{
    return Impl && Impl->Mrb != nullptr;
}

bool FBorn2FlapMrubyHost::Initialize(const FString& BrainRoot)
{
    Shutdown();
    LastError.Empty();

    Impl = new FBorn2FlapMrubyImpl();
    Impl->Mrb = mrb_open();
    if (!Impl->Mrb)
    {
        LastError = TEXT("mrb_open failed");
        Shutdown();
        return false;
    }
    Impl->Mrb->ud = Impl; // the C bridge callbacks recover the impl from mrb_state

    // C bridge module. Kept out of the Ruby `Born2Flap` namespace to avoid any
    // collision with Brain-defined constants/methods.
    struct RClass* B2FHost = mrb_define_module(Impl->Mrb, "B2FHost");
    mrb_define_const(Impl->Mrb, B2FHost, "BRAIN_ROOT", mrb_str_new_cstr(Impl->Mrb, TCHAR_TO_UTF8(*BrainRoot)));
    mrb_define_module_function(Impl->Mrb, B2FHost, "frames_write", B2F_frames_write, MRB_ARGS_REQ(1));
    mrb_define_module_function(Impl->Mrb, B2FHost, "log", B2F_log, MRB_ARGS_REQ(1));

    const bool bCompat = LoadScript(BrainRoot / TEXT("mruby/compat.rb"));
    const bool bHost = bCompat && LoadScript(BrainRoot / TEXT("mruby/embed_host.rb"));
    if (!bHost)
    {
        Shutdown();
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("Born2FlapMrubyHost: Ruby Brain loaded in-process from '%s'"), *BrainRoot);
    return true;
}

bool FBorn2FlapMrubyHost::LoadScript(const FString& AbsolutePath)
{
    if (!FPaths::FileExists(AbsolutePath))
    {
        LastError = FString::Printf(TEXT("missing script '%s'"), *AbsolutePath);
        return false;
    }

    FTCHARToUTF8 PathUtf8(*AbsolutePath); // lives through the protect call below
    int Ai = mrb_gc_arena_save(Impl->Mrb);
    mrb_bool bState = 0;
    mrb_protect_error(Impl->Mrb, LoadFileBody, const_cast<char*>(PathUtf8.Get()), &bState);
    if (bState)
    {
        mrb_print_error(Impl->Mrb);
        LastError = FString::Printf(TEXT("Ruby error loading '%s'"), *AbsolutePath);
    }
    mrb_gc_arena_restore(Impl->Mrb, Ai);
    return !bState;
}

bool FBorn2FlapMrubyHost::Tick(const FString& TelemetryJson, TArray<uint8>& OutFrames)
{
    if (!IsActive())
        return false;

    Impl->FrameBuffer.Reset();

    mrb_value Telemetry = mrb_nil_value();
    if (!TelemetryJson.IsEmpty())
        Telemetry = mrb_str_new_cstr(Impl->Mrb, TCHAR_TO_UTF8(*TelemetryJson));

    int Ai = mrb_gc_arena_save(Impl->Mrb);
    mrb_bool bState = 0;
    mrb_protect_error(Impl->Mrb, TickBody, static_cast<void*>(&Telemetry), &bState);
    if (bState)
    {
        mrb_print_error(Impl->Mrb);
        LastError = TEXT("Ruby Brain raised during tick");
        mrb_gc_arena_restore(Impl->Mrb, Ai);
        return false;
    }
    mrb_gc_arena_restore(Impl->Mrb, Ai);

    OutFrames = MoveTemp(Impl->FrameBuffer);
    return OutFrames.Num() > 0;
}

#else // !PLATFORM_IOS — stub (libmruby is linked on iOS only)

FBorn2FlapMrubyHost::FBorn2FlapMrubyHost() = default;
FBorn2FlapMrubyHost::~FBorn2FlapMrubyHost() = default;
void FBorn2FlapMrubyHost::Shutdown() {}
bool FBorn2FlapMrubyHost::IsActive() const { return false; }
bool FBorn2FlapMrubyHost::Initialize(const FString&) { LastError = TEXT("mruby host is iOS-only"); return false; }
bool FBorn2FlapMrubyHost::Tick(const FString&, TArray<uint8>&) { return false; }

#endif // PLATFORM_IOS