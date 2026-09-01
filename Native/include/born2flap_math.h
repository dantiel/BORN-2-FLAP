#ifndef BORN2FLAP_MATH_H
#define BORN2FLAP_MATH_H

#include <stdint.h>

#if defined(_WIN32)
#  define B2F_API __declspec(dllexport)
#else
#  define B2F_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define B2F_MATH_ABI_VERSION 1u

typedef struct B2F_MathContext B2F_MathContext;

typedef struct B2F_VehicleInput {
    double delta_time_s;
    double linear_velocity_m_s[3];
    double angular_velocity_rad_s[3];
    double throttle;
    double roll;
    double pitch;
    double yaw;
} B2F_VehicleInput;

typedef struct B2F_VehicleOutput {
    double force_n[3];
    double moment_n_m[3];
    double mechanical_power_w;
    double maximum_separation;
    int32_t first_stalled_element;
    uint32_t flags;
} B2F_VehicleOutput;

B2F_API uint32_t b2f_math_abi_version(void);
B2F_API int32_t b2f_math_runtime_init(void);
B2F_API void b2f_math_runtime_shutdown(void);
B2F_API B2F_MathContext* b2f_math_create_default_vehicle(void);
B2F_API void b2f_math_destroy_vehicle(B2F_MathContext* context);
B2F_API int32_t b2f_math_step_vehicle(
    B2F_MathContext* context,
    const B2F_VehicleInput* input,
    B2F_VehicleOutput* output);

#ifdef __cplusplus
}
#endif

#endif
