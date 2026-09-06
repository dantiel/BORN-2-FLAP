#include "born2flap_math.h"
#include <algorithm>
#include <cmath>
#include <new>

struct B2F_MathContext {
    double integral = 0.0;
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
