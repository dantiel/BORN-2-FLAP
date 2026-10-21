#include <HsFFI.h>
#include <stdatomic.h>

#if defined(_WIN32)
#define B2F_EXPORT __declspec(dllexport)
#else
#define B2F_EXPORT __attribute__((visibility("default")))
#endif

/* -------------------------------------------------------------------------
 * iOS RTS link-time overrides.
 *
 * The ghc-ios toolchain ships GHC's RTS with strong definitions of
 * mprotectForLinker (MMap.o) and osReserveHeapMemory (OSMem.o). On iOS both
 * must be replaced; Tools/weaken-rts-symbols.py marks the RTS definitions
 * N_WEAK_DEF so these strong definitions win at link time.
 *
 *   mprotectForLinker   -> no-op. GHC's linker makes pages executable with
 *                          mprotect(PROT_EXEC); iOS W^X forbids runtime JIT
 *                          (MAP_JIT) and kills the process with "Invalid Page".
 *                          A static build has no dynamically-loaded code, so
 *                          this call is unnecessary and a no-op is correct.
 *
 *   osReserveHeapMemory  -> capped reservation. GHC defaults to reserving
 *                          1 TiB of address space up front; iOS grants ~1 GB
 *                          and rejects the giant mmap. Cap it to a device-
 *                          friendly size instead.
 *
 * Only compiled for the iOS static-library build (Tools/build-math-ios.sh
 * passes -DB2F_IOS_RTS=1). Desktop/macOS/Windows builds keep the RTS defaults.
 * ------------------------------------------------------------------------- */
#if defined(B2F_IOS_RTS)

#include <stdint.h>
#include <stddef.h>
#include <sys/mman.h>
#include <unistd.h>

void mprotectForLinker(void *start, size_t len, int mode) {
    (void)start; (void)len; (void)mode;
}

void *osReserveHeapMemory(void *startAddressPtr, uintptr_t *len) {
    const uintptr_t maxHeap = (uintptr_t)512 * 1024 * 1024; /* 512 MiB */

    uintptr_t size = *len;
    if (size > maxHeap) {
        size = maxHeap;
    }

    uintptr_t pageSize = (uintptr_t)sysconf(_SC_PAGESIZE);
    if (pageSize == 0 || pageSize == (uintptr_t)-1) {
        pageSize = 4096;
    }
    size &= ~(pageSize - 1);
    if (size < pageSize) {
        size = pageSize;
    }

    /* Reserve address space without committing (PROT_NONE); the RTS commits
     * pages later with osCommitMemory (RW, not RWX — no W^X violation). */
    void *base = mmap(startAddressPtr, size, PROT_NONE,
                      MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED && startAddressPtr != NULL) {
        /* The hint (often a high address) may be unmappable on iOS. */
        base = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    }
    if (base == MAP_FAILED) {
        return NULL; /* the RTS caller barfs with a useful message */
    }

    *len = size;
    return base;
}

/* -------------------------------------------------------------------------
 * RTS diagnostic routing.
 *
 * GHC's RtsMessages.c defines barf/errorBelch/sysErrorBelch/debugBelch as
 * strong symbols whose default implementations fprintf(stderr, ...). On iOS
 * there is no terminal, so a runtime panic's message goes nowhere and the
 * RTS then calls stg_exit() silently. The ghc-ios toolchain documents this as
 * the third required link-time override: "barf and friends route through
 * os_log, so runtime panics are readable in Console".
 *
 * Tools/weaken-rts-symbols.py marks the RTS definitions in RtsMessages.o
 * N_WEAK_DEF so these strong overrides win at link time. Each override also
 * writes to stderr so `devicectl device process launch --console` captures it.
 * ------------------------------------------------------------------------- */
#include <os/log.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static void b2f_rts_emit(const char *tag, const char *fmt, va_list ap)
{
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    fprintf(stderr, "[ghc-rts:%s] %s\n", tag, buf);

    static os_log_t log;
    if (!log) {
        log = os_log_create("com.darographix.Born2Flap", "ghc-rts");
    }
    os_log_error(log, "[%{public}s] %{public}s", tag, buf);
}

void debugBelch(const char *s, ...)
{
    va_list ap; va_start(ap, s); b2f_rts_emit("debug", s, ap); va_end(ap);
}

int vdebugBelch(const char *s, va_list ap)
{
    b2f_rts_emit("debug", s, ap);
    return 0;
}

void errorBelch(const char *s, ...)
{
    va_list ap; va_start(ap, s); b2f_rts_emit("error", s, ap); va_end(ap);
}

void verrorBelch(const char *s, va_list ap)
{
    b2f_rts_emit("error", s, ap);
}

void sysErrorBelch(const char *s, ...)
{
    va_list ap; va_start(ap, s); b2f_rts_emit("syserror", s, ap); va_end(ap);
}

void vsysErrorBelch(const char *s, va_list ap)
{
    b2f_rts_emit("syserror", s, ap);
}

void barf(const char *s, ...)
{
    va_list ap; va_start(ap, s); b2f_rts_emit("barf", s, ap); va_end(ap);
    exit(1);
}

void vbarf(const char *s, va_list ap)
{
    b2f_rts_emit("barf", s, ap);
    exit(1);
}

#endif /* B2F_IOS_RTS */

static int b2f_rts_started = 0;
static atomic_flag b2f_init_lock = ATOMIC_FLAG_INIT;

extern HsWord32 hs_b2f_math_abi_version(void);
extern HsInt32 hs_b2f_math_runtime_init(void);
extern void hs_b2f_math_runtime_shutdown(void);
extern HsPtr hs_b2f_math_create_default_vehicle(void);
extern void hs_b2f_math_destroy_vehicle(HsPtr context);
extern HsInt32 hs_b2f_math_step_vehicle(HsPtr context, HsPtr input, HsPtr output);
extern HsPtr hs_b2f_math_create_firmware_vehicle(HsPtr config);
extern void hs_b2f_math_destroy_firmware_vehicle(HsPtr context);
extern HsInt32 hs_b2f_math_step_firmware_vehicle(HsPtr context, HsPtr pilot, HsPtr body, HsPtr output);
extern HsInt32 hs_b2f_math_set_stabilization(HsPtr context, HsWord32 enabled);
extern HsInt32 hs_b2f_math_set_wind_phase_noise(HsPtr context, HsDouble noise);
extern HsInt32 hs_b2f_math_reconfigure_firmware_vehicle(HsPtr context, HsPtr tuning);
extern HsInt32 hs_b2f_math_get_wing_shape(HsPtr context, HsWord32 capacity, HsPtr left, HsPtr right);
extern HsInt32 hs_b2f_math_get_wing_forces(HsPtr context, HsWord32 capacity, HsPtr left, HsPtr right);

B2F_EXPORT HsInt32 b2f_math_get_wing_shape(HsPtr context, HsWord32 capacity, HsPtr left, HsPtr right) {
    return hs_b2f_math_get_wing_shape(context, capacity, left, right);
}
B2F_EXPORT HsInt32 b2f_math_get_wing_forces(HsPtr context, HsWord32 capacity, HsPtr left, HsPtr right) {
    return hs_b2f_math_get_wing_forces(context, capacity, left, right);
}
B2F_EXPORT HsWord32 b2f_math_abi_version(void) {
    /* Version probing must be safe before the RTS starts, so this is a plain
     * constant instead of a call into the (uninitialised) Haskell RTS.
      * KEEP IN SYNC with B2F_MATH_ABI_VERSION in Native/include/born2flap_math.h
      * and b2f_math_abi_version in MathCore/src/Born2Flap/Math/FFI.hs. */
     return 14;
}

B2F_EXPORT HsInt32 b2f_math_runtime_init(void) {
    while (atomic_flag_test_and_set_explicit(&b2f_init_lock, memory_order_acquire)) {}
    if (!b2f_rts_started) {
        /* The ghc-ios RTS is built single-threaded and hs_init() runs with
         * RtsOptsSafeOnly. Under that mode -M/-A are OPTION_UNSAFE and -N
         * requires a -threaded build, so procRtsOpts() calls stg_exit() ->
         * exit() for ANY of +RTS -N1 -M512m -A16m -RTS, and exit() then crashes
         * Unreal via re-entrant C++ destructors during BeginPlay.
         *
         * Do NOT pass RTS flags here. The 1 TiB default reservation is already
         * capped to 512 MiB by the osReserveHeapMemory override above, and the
         * non-threaded RTS defaults to a single capability. A clean argv makes
         * hs_init() use those safe defaults. */
        int argc = 1;
        char programName[] = "born2flap_math";
        char *args[] = { programName, 0 };
        char **argv = args;
        hs_init(&argc, &argv);
        b2f_rts_started = 1;
    }
    atomic_flag_clear_explicit(&b2f_init_lock, memory_order_release);
    return 1;
}

B2F_EXPORT void b2f_math_runtime_shutdown(void) {
    /* Process-lifetime RTS: destroy contexts individually. GHC cannot safely
       restart after hs_exit. Hosts MUST keep this library loaded until exit. */
}

B2F_EXPORT HsPtr b2f_math_create_default_vehicle(void) {
    return hs_b2f_math_create_default_vehicle();
}

B2F_EXPORT void b2f_math_destroy_vehicle(HsPtr context) {
    hs_b2f_math_destroy_vehicle(context);
}

B2F_EXPORT HsInt32 b2f_math_step_vehicle(HsPtr context, HsPtr input, HsPtr output) {
    return hs_b2f_math_step_vehicle(context, input, output);
}

B2F_EXPORT HsPtr b2f_math_create_firmware_vehicle(HsPtr config) {
    return hs_b2f_math_create_firmware_vehicle(config);
}

B2F_EXPORT void b2f_math_destroy_firmware_vehicle(HsPtr context) {
    hs_b2f_math_destroy_firmware_vehicle(context);
}

B2F_EXPORT HsInt32 b2f_math_step_firmware_vehicle(HsPtr context, HsPtr pilot, HsPtr body, HsPtr output) {
    return hs_b2f_math_step_firmware_vehicle(context, pilot, body, output);
}

B2F_EXPORT HsInt32 b2f_math_set_stabilization(HsPtr context, HsWord32 enabled) {
    return hs_b2f_math_set_stabilization(context, enabled);
}

B2F_EXPORT HsInt32 b2f_math_set_wind_phase_noise(HsPtr context, HsDouble noise) {
    return hs_b2f_math_set_wind_phase_noise(context, noise);
}

B2F_EXPORT HsInt32 b2f_math_reconfigure_firmware_vehicle(HsPtr context, HsPtr tuning) {
    return hs_b2f_math_reconfigure_firmware_vehicle(context, tuning);
}
extern HsInt32 hs_b2f_math_get_mass_properties(HsPtr context, HsDouble cg_x, HsPtr output);
B2F_EXPORT HsInt32 b2f_math_get_mass_properties(HsPtr context, HsDouble cg_x, HsPtr output) {
    return hs_b2f_math_get_mass_properties(context, cg_x, output);
}