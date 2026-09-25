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

#define B2F_MATH_ABI_VERSION 3u

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

/* ── Firmware-emulation ABI (logical channels, selectable components) ──
 *
 * These functions run the mixer derived from PteronautOS, extended with
 * aerodynamic glide controls and opposite aileron stroke timing:
 * pilot sticks → firmware mixer → servo actuator (struggling against the
 * aerodynamic hinge torque) → wing aerodynamics, all closed-loop. Input is
 * logical channels (not raw CRSF): throttle 0..1, roll/pitch/yaw -1..1.
 */

typedef struct B2F_PilotInput {
    double throttle;  /* 0..1 flap throttle (glide below firmware threshold) */
    double roll;      /* -1..1 aileron */
    double pitch;     /* -1..1 elevator */
    double yaw;       /* -1..1 rudder */
} B2F_PilotInput;

typedef struct B2F_BodyState {
    double delta_time_s;
    double linear_velocity_m_s[3];
    double angular_velocity_rad_s[3];
} B2F_BodyState;

typedef struct B2F_FirmwareConfig {
    double servo_no_load_speed_deg_s;  /* 0 = default (857) */
    double servo_stall_torque_nm;      /* 0 = default (2.0) */
    double servo_backdrive_deg_s_nm;   /* 0 = default (20) */
    double battery_voltage;            /* 0 = default (7.4) */
    double battery_resistance_ohm;     /* 0 = default (0.05) */
    double battery_capacity_ah;        /* 0 = default (0.35) */
} B2F_FirmwareConfig;

typedef struct B2F_FirmwareOutput {
    double force_n[3];
    double moment_n_m[3];
    double mechanical_power_w;
    double maximum_separation;
    double left_flap_deg;    /* actual left wing flap deviation */
    double right_flap_deg;   /* actual right wing flap deviation */
    double battery_soc;      /* 0..1 */
    double phase_envelope;   /* ONDAS phase-envelope sup-mean (deg) */
    double phase_coverage;   /* ONDAS phase-bin coverage 0..1 */
    uint32_t flags;          /* bit 0 = is_flapping */
} B2F_FirmwareOutput;

B2F_API B2F_MathContext* b2f_math_create_firmware_vehicle(
    const B2F_FirmwareConfig* config);
B2F_API void b2f_math_destroy_firmware_vehicle(B2F_MathContext* context);
B2F_API int32_t b2f_math_step_firmware_vehicle(
    B2F_MathContext* context,
    const B2F_PilotInput* pilot,
    const B2F_BodyState* body,
    B2F_FirmwareOutput* output);

#ifdef __cplusplus
}
#endif

#endif