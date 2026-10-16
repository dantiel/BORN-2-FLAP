#include <HsFFI.h>
#include <stdatomic.h>

#if defined(_WIN32)
#define B2F_EXPORT __declspec(dllexport)
#else
#define B2F_EXPORT __attribute__((visibility("default")))
#endif

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
     return 10;
}

B2F_EXPORT HsInt32 b2f_math_runtime_init(void) {
    while (atomic_flag_test_and_set_explicit(&b2f_init_lock, memory_order_acquire)) {}
    if (!b2f_rts_started) {
        /* GHC RTS defaults to an UNLIMITED max heap and reserves its default
         * 1 TiB (RESERVED_MBLOCKS = 1<<20 x 1 MB) of address space up front.
         * On desktop that is a cheap PROT_NONE reservation, but iOS rejects a
         * 1 TiB mmap -> "memory allocation failed (requested 1099512676352
         * bytes)" -> the Haskell backend never starts. Cap the heap (and pin a
         * single capability) via RTS options passed through hs_init's argv. */
        int argc = 6;
        char programName[] = "born2flap_math";
        char rtsOn[] = "+RTS";
        char rtsCapabilities[] = "-N1";
        char rtsMaxHeap[] = "-M512m";
        char rtsNursery[] = "-A16m";
        char rtsOff[] = "-RTS";
        char *args[] = { programName, rtsOn, rtsCapabilities, rtsMaxHeap,
                         rtsNursery, rtsOff, 0 };
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
