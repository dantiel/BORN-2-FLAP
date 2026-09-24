#include "born2flap_rc_input.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
int main()
{
    using born2flap::RcKeyboard;
    for (int fps : {30, 60, 144})
    {
        RcKeyboard sticks;
        const double dt = 1.0 / fps;
        // A 100 ms yaw tap should be a small, independent rudder deflection.
        double elapsed = 0;
        while (elapsed < .1 - 1e-9)
        {
            const double step = std::min(dt, .1 - elapsed);
            sticks.Step(step, .72, 0, 0, 1);
            elapsed += step;
        }
        require(std::abs(sticks.yaw - .125) < 1e-8, "tap slew depends on frame rate");
        require(RcKeyboard::Expo(sticks.yaw) < .05, "short tap gives excessive rudder");
        require(sticks.roll == 0 && sticks.pitch == 0, "yaw tap leaks into other RC channels");
        for (int i = 0; i < fps; ++i)
            sticks.Step(dt, 1, 1, -1, -1);
        require(sticks.throttle == 1 && sticks.roll > .999 && sticks.pitch < -.999 && sticks.yaw < -.999,
                "held controls must reach full stick travel, including reversal");
        for (int i = 0; i < fps; ++i)
            sticks.Step(dt, 0, 0, 0, 0);
        require(sticks.throttle == 0 && sticks.roll == 0 && sticks.pitch == 0 && sticks.yaw == 0,
                "released sticks must return to neutral");
    }
    std::cout << "RC keyboard taps, held sticks, release and frame-rate independence passed\n";
}
