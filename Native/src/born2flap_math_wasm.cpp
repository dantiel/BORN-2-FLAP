// Born2FlapMathWasmBackend — static b2f_math_* backend over wasm3.
//
// Implements the SAME C ABI as the desktop Haskell dylib
// (Native/include/born2flap_math.h) so Born2FlapMathBridge.cpp can bind it
// statically on iOS/Android where dlopen is forbidden. The Haskell MathCore is
// compiled once to born2flap_math.wasm (Tools/build-math-wasm.sh) and embedded
// here; wasm3 interprets it (no JIT → iOS-legal).
//
// wasm3 dependency (MIT):
//   git submodule add https://github.com/wasm3/wasm3.git Native/ThirdParty/wasm3
#include "born2flap_math.h"

#if defined(__wasm__) || defined(B2F_WASM_BACKEND_ENABLED)

#include "wasm3.h"
#include "m3_env.h"
#include "m3_api_wasi.h"

#include <cstdint>
#include <cstring>

// Embedded module bytes. Tools/build-math-wasm.sh emits
// MathCore/dist-newstyle/born2flap_math_wasm.inc (xxd -i -n born2flap_math_wasm);
// Tools/build-wasm3-android.sh -include's it so born2flap_math_wasm[] and
// born2flap_math_wasm_len are visible in this translation unit.
static const uint8_t* const kBorn2FlapMathWasm = born2flap_math_wasm;
static const uint32_t kBorn2FlapMathWasmLen = born2flap_math_wasm_len;

namespace
{
IM3Environment GEnv = nullptr;
IM3Runtime GRuntime = nullptr;
IM3Module GModule = nullptr;

IM3Function FnMalloc = nullptr;
IM3Function FnFree = nullptr;
IM3Function FnRuntimeInit = nullptr;
IM3Function FnRuntimeShutdown = nullptr;
IM3Function FnAbiVersion = nullptr;
IM3Function FnInitialize = nullptr;
IM3Function FnCreateDefaultVehicle = nullptr;
IM3Function FnDestroyVehicle = nullptr;
IM3Function FnStepVehicle = nullptr;
IM3Function FnCreateFirmwareVehicle = nullptr;
IM3Function FnDestroyFirmwareVehicle = nullptr;
IM3Function FnStepFirmwareVehicle = nullptr;
IM3Function FnSetStabilization = nullptr;
IM3Function FnSetWindPhaseNoise = nullptr;
IM3Function FnReconfigureFirmwareVehicle = nullptr;
IM3Function FnSetWingModel = nullptr;
IM3Function FnGetModelMassKg = nullptr;
IM3Function FnGetWingShape = nullptr;
IM3Function FnGetWingForces = nullptr;
IM3Function FnGetWingFields = nullptr;
IM3Function FnGetWingLoads = nullptr;
IM3Function FnSetStripWind = nullptr;
IM3Function FnTransportAirfields = nullptr;
IM3Function FnGetTailIncidence = nullptr;
IM3Function FnGetTailFields = nullptr;
IM3Function FnGetExternalForce = nullptr;
IM3Function FnSetMassFrame = nullptr;
IM3Function FnSetWingCamberBias = nullptr;
IM3Function FnGetMassProperties = nullptr;

M3Result Find(const char* name, IM3Function* out)
{
    M3Result r = m3_FindFunction(out, GRuntime, name);
    if (r) { /* log */ }
    return r;
}

// Opaque pointer type inside the wasm32 linear memory. Contexts are GHC
// StablePtrs returned by the Haskell side and passed back untouched.
using WasmPtr = uint32_t;

uint8_t* WasmMemory(size_t* outSize)
{
    size_t size = 0;
    uint8_t* mem = m3_GetMemory(GModule, &size, 0);
    if (outSize) *outSize = size;
    return mem;
}

WasmPtr AllocWasm(size_t bytes)
{
    WasmPtr p = 0;
    if (FnMalloc && bytes > 0)
    {
        m3_GetResultsV(FnMalloc, &p);
        m3_CallV(FnMalloc, static_cast<uint32_t>(bytes));
    }
    return p;
}

// Allocate a POD struct in the WASM linear memory and copy host bytes in.
template <typename T>
WasmPtr Push(const T& value)
{
    WasmPtr p = AllocWasm(sizeof(T));
    if (!p) return 0;
    size_t size = 0;
    uint8_t* mem = WasmMemory(&size);
    if (!mem || p + sizeof(T) > size) return 0;
    std::memcpy(mem + p, &value, sizeof(T));
    return p;
}

WasmPtr PushBytes(const void* data, size_t bytes)
{
    WasmPtr p = AllocWasm(bytes);
    if (!p || !data || bytes == 0) return p;
    size_t size = 0;
    uint8_t* mem = WasmMemory(&size);
    if (!mem || p + bytes > size) return 0;
    std::memcpy(mem + p, data, bytes);
    return p;
}

// Copy a POD struct back out of the WASM linear memory.
template <typename T>
void Pull(WasmPtr p, T& out)
{
    size_t size = 0;
    uint8_t* mem = WasmMemory(&size);
    if (p && mem && p + sizeof(T) <= size)
        std::memcpy(&out, mem + p, sizeof(T));
}

void PullBytes(WasmPtr p, void* out, size_t bytes)
{
    size_t size = 0;
    uint8_t* mem = WasmMemory(&size);
    if (p && mem && out && p + bytes <= size)
        std::memcpy(out, mem + p, bytes);
}

void Free(WasmPtr p)
{
    if (FnFree && p)
        m3_CallV(FnFree, p);
}

bool LoadWasm()
{
    if (GEnv || kBorn2FlapMathWasmLen == 0)
        return false;
    GEnv = m3_NewEnvironment();
    GRuntime = m3_NewRuntime(GEnv, 256 * 1024, nullptr);
    if (!GRuntime) return false;
    if (m3_ParseModule(GEnv, &GModule, kBorn2FlapMathWasm, kBorn2FlapMathWasmLen)) return false;
    if (m3_LoadModule(GRuntime, GModule)) return false;
    // GHC's WASI reactor needs WASI imports (clock, fd_write, …).
    m3_LinkWASI(GModule);
    // A WASI reactor may export `_initialize`; if present it MUST be called
    // exactly once before any other export (it runs the libc constructors).
    // GHC's wasm32-wasi-ghc reactor exports it, and hs_init/malloc assume the
    // ctors have already run. wasmtime auto-calls it (that's why the WSL smoke
    // test passed); wasm3 does NOT, so we must call it explicitly here — this
    // is the whole reason the on-device backend never produced thrust/flap.
    Find("_initialize", &FnInitialize);
    if (FnInitialize)
        m3_CallV(FnInitialize);

    Find("malloc", &FnMalloc);
    Find("free", &FnFree);
    Find("b2f_math_runtime_init", &FnRuntimeInit);
    Find("b2f_math_runtime_shutdown", &FnRuntimeShutdown);
    Find("b2f_math_abi_version", &FnAbiVersion);
    Find("b2f_math_create_default_vehicle", &FnCreateDefaultVehicle);
    Find("b2f_math_destroy_vehicle", &FnDestroyVehicle);
    Find("b2f_math_step_vehicle", &FnStepVehicle);
    Find("b2f_math_create_firmware_vehicle", &FnCreateFirmwareVehicle);
    Find("b2f_math_destroy_firmware_vehicle", &FnDestroyFirmwareVehicle);
    Find("b2f_math_step_firmware_vehicle", &FnStepFirmwareVehicle);
    Find("b2f_math_set_stabilization", &FnSetStabilization);
    Find("b2f_math_set_wind_phase_noise", &FnSetWindPhaseNoise);
    Find("b2f_math_reconfigure_firmware_vehicle", &FnReconfigureFirmwareVehicle);
    Find("b2f_math_set_wing_model", &FnSetWingModel);
    Find("b2f_math_get_model_mass_kg", &FnGetModelMassKg);
    Find("b2f_math_get_wing_shape", &FnGetWingShape);
    Find("b2f_math_get_wing_forces", &FnGetWingForces);
    Find("b2f_math_get_wing_fields", &FnGetWingFields);
    Find("b2f_math_get_wing_loads", &FnGetWingLoads);
    Find("b2f_math_set_strip_wind", &FnSetStripWind);
    Find("b2f_math_transport_airfields", &FnTransportAirfields);
    Find("b2f_math_get_tail_incidence", &FnGetTailIncidence);
    Find("b2f_math_get_tail_fields", &FnGetTailFields);
    Find("b2f_math_get_external_force", &FnGetExternalForce);
    Find("b2f_math_set_mass_frame", &FnSetMassFrame);
    Find("b2f_math_set_wing_camber_bias", &FnSetWingCamberBias);
    Find("b2f_math_get_mass_properties", &FnGetMassProperties);

    return FnMalloc && FnFree && FnRuntimeInit;
}
} // namespace

extern "C" {

B2F_API uint32_t b2f_math_abi_version()
{
    if (!FnAbiVersion) return B2F_MATH_ABI_VERSION;
    uint32_t v = 0; m3_GetResultsV(FnAbiVersion, &v); m3_CallV(FnAbiVersion);
    return v;
}

B2F_API int32_t b2f_math_runtime_init()
{
    if (!GRuntime && !LoadWasm()) return 0;
    if (!FnRuntimeInit) return 1;
    int32_t r = 0; m3_GetResultsV(FnRuntimeInit, &r); m3_CallV(FnRuntimeInit);
    return r;
}

B2F_API void b2f_math_runtime_shutdown()
{
    if (FnRuntimeShutdown)
        m3_CallV(FnRuntimeShutdown);
}

B2F_API B2F_MathContext* b2f_math_create_default_vehicle()
{
    if (!GRuntime && !LoadWasm()) return nullptr;
    if (!FnCreateDefaultVehicle) return nullptr;
    WasmPtr ctx = 0;
    m3_GetResultsV(FnCreateDefaultVehicle, &ctx);
    m3_CallV(FnCreateDefaultVehicle);
    return reinterpret_cast<B2F_MathContext*>(static_cast<uintptr_t>(ctx));
}

B2F_API void b2f_math_destroy_vehicle(B2F_MathContext* context)
{
    if (FnDestroyVehicle && context)
        m3_CallV(FnDestroyVehicle, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)));
}

B2F_API int32_t b2f_math_step_vehicle(
    B2F_MathContext* context,
    const B2F_VehicleInput* input,
    B2F_VehicleOutput* output)
{
    if (!context || !FnStepVehicle || !input || !output) return 0;
    WasmPtr in = Push(*input);
    B2F_VehicleOutput out{};
    WasmPtr o = Push(out);
    int32_t r = 0;
    m3_GetResultsV(FnStepVehicle, &r);
    m3_CallV(FnStepVehicle,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), in, o);
    Pull(o, *output);
    Free(in); Free(o);
    return r;
}

B2F_API B2F_MathContext* b2f_math_create_firmware_vehicle(const B2F_FirmwareConfig* config)
{
    if (!GRuntime && !LoadWasm()) return nullptr;
    if (!FnCreateFirmwareVehicle || !config) return nullptr;
    WasmPtr cfg = Push(*config);
    WasmPtr ctx = 0;
    m3_GetResultsV(FnCreateFirmwareVehicle, &ctx);
    m3_CallV(FnCreateFirmwareVehicle, cfg);
    Free(cfg);
    return reinterpret_cast<B2F_MathContext*>(static_cast<uintptr_t>(ctx));
}

B2F_API void b2f_math_destroy_firmware_vehicle(B2F_MathContext* context)
{
    if (FnDestroyFirmwareVehicle && context)
        m3_CallV(FnDestroyFirmwareVehicle, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)));
}

B2F_API int32_t b2f_math_step_firmware_vehicle(
    B2F_MathContext* context,
    const B2F_PilotInput* pilot,
    const B2F_BodyState* body,
    B2F_FirmwareOutput* output)
{
    if (!context || !FnStepFirmwareVehicle || !pilot || !body || !output) return 0;
    WasmPtr p = Push(*pilot);
    WasmPtr b = Push(*body);
    B2F_FirmwareOutput out{};
    WasmPtr o = Push(out);
    int32_t r = 0;
    m3_GetResultsV(FnStepFirmwareVehicle, &r);
    m3_CallV(FnStepFirmwareVehicle,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), p, b, o);
    Pull(o, *output);
    Free(p); Free(b); Free(o);
    return r;
}

B2F_API int32_t b2f_math_set_stabilization(B2F_MathContext* context, uint32_t enabled)
{
    if (!context || !FnSetStabilization) return 0;
    int32_t r = 0;
    m3_GetResultsV(FnSetStabilization, &r);
    m3_CallV(FnSetStabilization,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), enabled);
    return r;
}

B2F_API int32_t b2f_math_set_wind_phase_noise(B2F_MathContext* context, double noiseRadS)
{
    if (!context || !FnSetWindPhaseNoise) return 0;
    int32_t r = 0;
    m3_GetResultsV(FnSetWindPhaseNoise, &r);
    m3_CallV(FnSetWindPhaseNoise,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), noiseRadS);
    return r;
}

B2F_API int32_t b2f_math_reconfigure_firmware_vehicle(
    B2F_MathContext* context, const B2F_TuningConfig* tuning)
{
    if (!context || !FnReconfigureFirmwareVehicle || !tuning) return 0;
    WasmPtr t = Push(*tuning);
    int32_t r = 0;
    m3_GetResultsV(FnReconfigureFirmwareVehicle, &r);
    m3_CallV(FnReconfigureFirmwareVehicle,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), t);
    Free(t);
    return r;
}

B2F_API int32_t b2f_math_set_wing_model(B2F_MathContext* context, uint32_t model_id)
{
    if (!context || !FnSetWingModel) return 0;
    int32_t r = 0;
    m3_GetResultsV(FnSetWingModel, &r);
    m3_CallV(FnSetWingModel,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), model_id);
    return r;
}

B2F_API double b2f_math_get_model_mass_kg(uint32_t model_id)
{
    if (!FnGetModelMassKg) return 0.0;
    double mass = 0.0;
    m3_GetResultsV(FnGetModelMassKg, &mass);
    m3_CallV(FnGetModelMassKg, model_id);
    return mass;
}

B2F_API int32_t b2f_math_get_wing_shape(
    B2F_MathContext* context, uint32_t capacity,
    B2F_WingSection* left, B2F_WingSection* right)
{
    if (!context || !FnGetWingShape || !left || !right) return 0;
    if (capacity > B2F_WING_STATIONS) capacity = B2F_WING_STATIONS;
    size_t bytes = static_cast<size_t>(capacity) * sizeof(B2F_WingSection);
    WasmPtr l = AllocWasm(bytes);
    WasmPtr r = AllocWasm(bytes);
    int32_t n = 0;
    m3_GetResultsV(FnGetWingShape, &n);
    m3_CallV(FnGetWingShape,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), capacity, l, r);
    if (n > 0)
    {
        size_t nbytes = static_cast<size_t>(n) * sizeof(B2F_WingSection);
        PullBytes(l, left, nbytes);
        PullBytes(r, right, nbytes);
    }
    Free(l); Free(r);
    return n;
}

B2F_API int32_t b2f_math_get_wing_forces(
    B2F_MathContext* context, uint32_t capacity,
    double* left, double* right)
{
    if (!context || !FnGetWingForces || !left || !right) return 0;
    if (capacity > B2F_WING_STATIONS) capacity = B2F_WING_STATIONS;
    size_t bytes = static_cast<size_t>(capacity) * sizeof(double);
    WasmPtr l = AllocWasm(bytes);
    WasmPtr r = AllocWasm(bytes);
    int32_t n = 0;
    m3_GetResultsV(FnGetWingForces, &n);
    m3_CallV(FnGetWingForces,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), capacity, l, r);
    if (n > 0)
    {
        size_t nbytes = static_cast<size_t>(n) * sizeof(double);
        PullBytes(l, left, nbytes);
        PullBytes(r, right, nbytes);
    }
    Free(l); Free(r);
    return n;
}

B2F_API int32_t b2f_math_get_wing_fields(
    B2F_MathContext* context, uint32_t capacity,
    B2F_WingField* left, B2F_WingField* right)
{
    if (!context || !FnGetWingFields || !left || !right) return 0;
    if (capacity > B2F_WING_STATIONS) capacity = B2F_WING_STATIONS;
    size_t bytes = static_cast<size_t>(capacity) * sizeof(B2F_WingField);
    WasmPtr l = AllocWasm(bytes);
    WasmPtr r = AllocWasm(bytes);
    int32_t n = 0;
    m3_GetResultsV(FnGetWingFields, &n);
    m3_CallV(FnGetWingFields,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), capacity, l, r);
    if (n > 0)
    {
        size_t nbytes = static_cast<size_t>(n) * sizeof(B2F_WingField);
        PullBytes(l, left, nbytes);
        PullBytes(r, right, nbytes);
    }
    Free(l); Free(r);
    return n;
}

B2F_API int32_t b2f_math_get_wing_loads(
    B2F_MathContext* context, uint32_t capacity,
    B2F_WingLoads* left, B2F_WingLoads* right)
{
    if (!context || !FnGetWingLoads || !left || !right) return 0;
    if (capacity > B2F_WING_STATIONS) capacity = B2F_WING_STATIONS;
    size_t bytes = static_cast<size_t>(capacity) * sizeof(B2F_WingLoads);
    WasmPtr l = AllocWasm(bytes);
    WasmPtr r = AllocWasm(bytes);
    int32_t n = 0;
    m3_GetResultsV(FnGetWingLoads, &n);
    m3_CallV(FnGetWingLoads,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), capacity, l, r);
    if (n > 0)
    {
        size_t nbytes = static_cast<size_t>(n) * sizeof(B2F_WingLoads);
        PullBytes(l, left, nbytes);
        PullBytes(r, right, nbytes);
    }
    Free(l); Free(r);
    return n;
}

B2F_API int32_t b2f_math_set_strip_wind(
    B2F_MathContext* context, uint32_t count,
    const double* left_xyz, const double* right_xyz)
{
    if (!context || !FnSetStripWind || !left_xyz || !right_xyz) return 0;
    if (count > B2F_WING_STATIONS) count = B2F_WING_STATIONS;
    size_t bytes = static_cast<size_t>(count) * 3 * sizeof(double);
    WasmPtr l = PushBytes(left_xyz, bytes);
    WasmPtr r = PushBytes(right_xyz, bytes);
    int32_t rc = 0;
    m3_GetResultsV(FnSetStripWind, &rc);
    m3_CallV(FnSetStripWind,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), count, l, r);
    Free(l); Free(r);
    return rc;
}

B2F_API int32_t b2f_math_transport_airfields(B2F_MathContext* context, const double rotation[3])
{
    if (!context || !FnTransportAirfields || !rotation) return 0;
    WasmPtr rot = PushBytes(rotation, 3 * sizeof(double));
    int32_t rc = 0;
    m3_GetResultsV(FnTransportAirfields, &rc);
    m3_CallV(FnTransportAirfields,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), rot);
    Free(rot);
    return rc;
}

B2F_API int32_t b2f_math_get_tail_incidence(B2F_MathContext* context, double* degrees)
{
    if (!context || !FnGetTailIncidence || !degrees) return 0;
    double d = 0.0;
    WasmPtr p = Push(d);
    int32_t rc = 0;
    m3_GetResultsV(FnGetTailIncidence, &rc);
    m3_CallV(FnGetTailIncidence,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), p);
    Pull(p, *degrees);
    Free(p);
    return rc;
}

B2F_API int32_t b2f_math_get_tail_fields(
    B2F_MathContext* context, uint32_t capacity, B2F_TailField* output)
{
    if (!context || !FnGetTailFields || !output) return 0;
    if (capacity > B2F_TAIL_PATCHES) capacity = B2F_TAIL_PATCHES;
    size_t bytes = static_cast<size_t>(capacity) * sizeof(B2F_TailField);
    WasmPtr o = AllocWasm(bytes);
    int32_t n = 0;
    m3_GetResultsV(FnGetTailFields, &n);
    m3_CallV(FnGetTailFields,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), capacity, o);
    if (n > 0)
        PullBytes(o, output, static_cast<size_t>(n) * sizeof(B2F_TailField));
    Free(o);
    return n;
}

B2F_API int32_t b2f_math_get_external_force(B2F_MathContext* context, double* force_n)
{
    if (!context || !FnGetExternalForce || !force_n) return 0;
    double f[3] = {0.0, 0.0, 0.0};
    WasmPtr p = PushBytes(f, sizeof(f));
    int32_t rc = 0;
    m3_GetResultsV(FnGetExternalForce, &rc);
    m3_CallV(FnGetExternalForce,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), p);
    PullBytes(p, force_n, sizeof(f));
    Free(p);
    return rc;
}

B2F_API int32_t b2f_math_set_mass_frame(
    B2F_MathContext* context, double cg_x_m,
    double pitch_inertia_kg_m2, double yaw_inertia_kg_m2)
{
    if (!context || !FnSetMassFrame) return 0;
    int32_t rc = 0;
    m3_GetResultsV(FnSetMassFrame, &rc);
    m3_CallV(FnSetMassFrame,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)),
        cg_x_m, pitch_inertia_kg_m2, yaw_inertia_kg_m2);
    return rc;
}

B2F_API int32_t b2f_math_set_wing_camber_bias(
    B2F_MathContext* context, double left_fraction, double right_fraction)
{
    if (!context || !FnSetWingCamberBias) return 0;
    int32_t rc = 0;
    m3_GetResultsV(FnSetWingCamberBias, &rc);
    m3_CallV(FnSetWingCamberBias,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)),
        left_fraction, right_fraction);
    return rc;
}

B2F_API int32_t b2f_math_get_mass_properties(
    B2F_MathContext* context, double cg_x_m, B2F_MassProperties* output)
{
    if (!context || !FnGetMassProperties || !output) return 0;
    B2F_MassProperties mp{};
    WasmPtr p = Push(mp);
    int32_t rc = 0;
    m3_GetResultsV(FnGetMassProperties, &rc);
    m3_CallV(FnGetMassProperties,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)), cg_x_m, p);
    Pull(p, *output);
    Free(p);
    return rc;
}

} // extern "C"

#endif // defined(__wasm__) || B2F_WASM_BACKEND_ENABLED