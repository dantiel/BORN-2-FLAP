#include "born2flap_math.h"
#include <algorithm>
#include <cmath>
#include <new>

// C++ reference/fallback implementation of the born2flap_math C ABI. Unreal
// normally loads the Haskell backend from Binaries/ThirdParty; this shared
// library exists so the ABI stays self-contained and testable without GHC.
// The firmware path below is a deliberately simplified stand-in (sinusoidal
// flap, lift ∝ throttle, stick moments) — the full mixer/servo/aero loop
// lives in the Haskell backend.

namespace
{
constexpr double kPi2 = 6.28318530717958647692;
}

struct B2F_MathContext {
    double integral = 0.0;
    // Firmware-emulation state (simplified fallback).
    double flapPhase = 0.0;
    double batterySoc = 1.0;
};

extern "C" B2F_API uint32_t b2f_math_abi_version() { return B2F_MATH_ABI_VERSION; }
extern "C" B2F_API int32_t b2f_math_runtime_init() { return 1; }
extern "C" B2F_API void b2f_math_runtime_shutdown() {}

extern "C" B2F_API B2F_MathContext* b2f_math_create_default_vehicle() {
    return new (std::nothrow) B2F_MathContext{};
}

extern "C" B2F_API void b2f_math_destroy_vehicle(B2F_MathContext* context) {
    delete context;
}

extern "C" B2F_API int32_t b2f_math_step_vehicle(
    B2F_MathContext* context,
    const B2F_VehicleInput* input,
    B2F_VehicleOutput* output) {
    if (!context || !input || !output) return 0;
    *output = {};
    const double dt = std::max(0.0, input->delta_time_s);
    context->integral = std::max(-1.0, std::min(1.0, context->integral + input->throttle * dt));
    // Prototype lift is intentionally sized near the 1.2 kg pawn's weight so
    // W can hold it up while the full wing model is still under construction.
    output->force_n[2] = input->throttle * 15.0;
    output->moment_n_m[0] = input->roll * 2.0;
    output->moment_n_m[1] = input->pitch * 2.0;
    output->moment_n_m[2] = input->yaw * 1.0;
    output->mechanical_power_w = std::abs(input->throttle) * 15.0;
    output->maximum_separation = 0.0;
    output->first_stalled_element = -1;
    return 1;
}

extern "C" B2F_API B2F_MathContext* b2f_math_create_firmware_vehicle(
    const B2F_FirmwareConfig* config) {
    B2F_MathContext* context = new (std::nothrow) B2F_MathContext{};
    (void)config; // Component selection is a no-op in the fallback.
    return context;
}

extern "C" B2F_API void b2f_math_destroy_firmware_vehicle(B2F_MathContext* context) {
    delete context;
}

extern "C" B2F_API int32_t b2f_math_step_firmware_vehicle(
    B2F_MathContext* context,
    const B2F_PilotInput* pilot,
    const B2F_BodyState* body,
    B2F_FirmwareOutput* output) {
    if (!context || !pilot || !body || !output) return 0;
    *output = {};

    const double dt = std::max(0.0, body->delta_time_s);
    const double throttle = std::max(0.0, std::min(1.0, pilot->throttle));

    // Simplified flap: cadence scales with throttle, amplitude caps at 55°.
    const double freqHz = 0.5 + throttle * 3.5;
    context->flapPhase += kPi2 * freqHz * dt;
    if (context->flapPhase > kPi2) context->flapPhase -= kPi2;

    const double flap = throttle * 55.0 * std::sin(context->flapPhase);
    const double rollDiff = std::max(-1.0, std::min(1.0, pilot->roll)) * 10.0;
    output->left_flap_deg = flap + rollDiff;
    output->right_flap_deg = flap - rollDiff;

    output->force_n[2] = throttle * 15.0;
    output->moment_n_m[0] = pilot->roll * 2.0;
    output->moment_n_m[1] = pilot->pitch * 2.0;
    output->moment_n_m[2] = pilot->yaw * 1.0;
    output->mechanical_power_w = std::abs(throttle) * 15.0;
    output->maximum_separation = 0.0;

    // Battery drains gently while flapping (0.1 Ah/s at full throttle).
    context->batterySoc = std::max(0.0, context->batterySoc - throttle * dt * 0.1 / 3600.0);
    output->battery_soc = context->batterySoc;
    output->phase_envelope = 0.0;   /* ONDAS layers: no-op in the fallback */
    output->phase_coverage = 0.0;
    output->flags = throttle > 0.08 ? 1u : 0u;
    return 1;
}