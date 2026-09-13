#include <HsFFI.h>

#if defined(_WIN32)
#define B2F_EXPORT __declspec(dllexport)
#else
#define B2F_EXPORT __attribute__((visibility("default")))
#endif

static int b2f_rts_started = 0;

extern HsWord32 hs_b2f_math_abi_version(void);
extern HsInt32 hs_b2f_math_runtime_init(void);
extern void hs_b2f_math_runtime_shutdown(void);
extern HsPtr hs_b2f_math_create_default_vehicle(void);
extern void hs_b2f_math_destroy_vehicle(HsPtr context);
extern HsInt32 hs_b2f_math_step_vehicle(HsPtr context, HsPtr input, HsPtr output);

B2F_EXPORT HsWord32 b2f_math_abi_version(void) {
    return hs_b2f_math_abi_version();
}

B2F_EXPORT HsInt32 b2f_math_runtime_init(void) {
    if (!b2f_rts_started) {
        int argc = 1;
        char programName[] = "born2flap_math";
        char *args[] = { programName, 0 };
        char **argv = args;
        hs_init(&argc, &argv);
        b2f_rts_started = 1;
    }
    return hs_b2f_math_runtime_init();
}

B2F_EXPORT void b2f_math_runtime_shutdown(void) {
    hs_b2f_math_runtime_shutdown();
    if (b2f_rts_started) {
        hs_exit();
        b2f_rts_started = 0;
    }
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
