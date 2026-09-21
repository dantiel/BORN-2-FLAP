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

B2F_EXPORT HsWord32 b2f_math_abi_version(void) {
    /* Version probing must be safe before the RTS starts. */
    return 2;
}

B2F_EXPORT HsInt32 b2f_math_runtime_init(void) {
    while (atomic_flag_test_and_set_explicit(&b2f_init_lock, memory_order_acquire)) {}
    if (!b2f_rts_started) {
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
