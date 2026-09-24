#pragma once
#include <algorithm>
#include <cmath>

namespace born2flap
{
// The keyboard moves virtual Mode-2 transmitter sticks. Other input adapters
// can supply the same normalized channels without this keyboard-only slew.
struct RcKeyboard
{
    double throttle = 0, roll = 0, pitch = 0, yaw = 0;

    static double Move(double value, double target, double dt, double speed)
    {
        return value + std::clamp(target - value, -speed * dt, speed * dt);
    }
    static double Expo(double stick) { return .35 * stick + .65 * stick * stick * stick; }
    void Step(double dt, double throttleTarget, double rollTarget, double pitchTarget, double yawTarget)
    {
        dt = std::clamp(dt, 0.0, .1);
        throttle = Move(throttle, std::clamp(throttleTarget, 0.0, 1.0), dt, 2.5);
        auto Axis = [dt](double value, double target) {
            target = std::clamp(target, -1.0, 1.0);
            return Move(value, target, dt, target == 0 ? 2.5 : 1.25);
        };
        roll = Axis(roll, rollTarget);
        pitch = Axis(pitch, pitchTarget);
        yaw = Axis(yaw, yawTarget);
    }
};
} // namespace born2flap
