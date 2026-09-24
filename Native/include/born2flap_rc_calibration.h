#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace born2flap
{
// Device axes are normalized to [0,1] before this hardware-independent layer.
// Channels are throttle, roll, pitch, yaw. Physical transmitters stay linear;
// keyboard slew/expo is deliberately not applied a second time here.
struct RcAxisCalibration
{
    int axis = -1;
    double low = 0, centre = .5, high = 1;
    bool inverted = false;
    double deadzone = .025;
    bool Valid(bool throttle) const
    {
        return axis >= 0 && axis < 8 && std::isfinite(low) && std::isfinite(high) && std::isfinite(centre) &&
               std::isfinite(deadzone) && low >= 0 && high <= 1 && high - low >= .2 && deadzone >= 0 &&
               deadzone <= .2 && (throttle || (centre > low + .05 && centre < high - .05));
    }
    double Map(double value, bool throttle) const
    {
        if (!Valid(throttle) || !std::isfinite(value))
            return 0;
        if (throttle)
        {
            double result = std::clamp((value - low) / (high - low), 0.0, 1.0);
            return inverted ? 1 - result : result;
        }
        double result = value < centre ? (value - centre) / (centre - low) : (value - centre) / (high - centre);
        result = std::clamp(result, -1.0, 1.0);
        result = std::copysign(std::max(0.0, std::abs(result) - deadzone) / (1 - deadzone), result);
        return inverted ? -result : result;
    }
};
struct RcCalibration
{
    std::array<RcAxisCalibration, 4> channels;
    bool Valid() const
    {
        for (int i = 0; i < 4; ++i)
        {
            if (!channels[i].Valid(i == 0))
                return false;
            for (int j = 0; j < i; ++j)
                if (channels[i].axis == channels[j].axis)
                    return false;
        }
        return true;
    }
    std::array<double, 4> Map(const std::array<double, 8> &axes) const
    {
        std::array<double, 4> result{};
        if (Valid())
            for (int i = 0; i < 4; ++i)
                result[i] = channels[i].Map(axes[channels[i].axis], i == 0);
        return result;
    }
};
struct RcSignalGate
{
    bool armed = false;
    std::array<double, 4> Filter(bool ready, const std::array<double, 4> &channels)
    {
        if (!ready || !std::all_of(channels.begin(), channels.end(), [](double v) { return std::isfinite(v); }))
        {
            armed = false;
            return {};
        }
        if (channels[0] <= .04)
            armed = true;
        return armed ? channels : std::array<double, 4>{};
    }
};
} // namespace born2flap
