#include "born2flap_rc_calibration.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
int main()
{
    born2flap::RcCalibration calibration;
    require(!calibration.Valid(), "unassigned device must not be enabled");
    for (int i = 0; i < 4; ++i)
        calibration.channels[i] = {i, .1, .45, .9, false, .025};
    require(calibration.Valid(), "valid measured ranges rejected");
    require(calibration.Map({.1, .45, .45, .45}) == std::array<double, 4>{}, "neutral/low throttle is not zero");
    require(calibration.Map({.9, .9, .1, .45}) == std::array<double, 4>{1, 1, -1, 0},
            "endpoints or asymmetric centre incorrect");
    calibration.channels[0].inverted = true;
    calibration.channels[2].inverted = true;
    require(calibration.Map({.9, .451, .9, .45}) == std::array<double, 4>{0, 0, -1, 0},
            "inversion or deadband incorrect");
    calibration.channels[3].axis = 1;
    require(!calibration.Valid(), "two channels cannot silently share an axis");
    calibration.channels[3].axis = 3;
    calibration.channels[1].high = .12;
    require(!calibration.Valid(), "insufficient travel accepted");
    born2flap::RcSignalGate gate;
    require(gate.Filter(true, {1, .5, 0, 0})[0] == 0 && !gate.armed, "hot throttle armed on connect");
    gate.Filter(true, {0, 0, 0, 0});
    require(gate.Filter(true, {.7, .2, -.3, .4}) == std::array<double, 4>{.7, .2, -.3, .4},
            "live physical sticks changed");
    require(gate.Filter(false, {.7, .2, -.3, .4}) == std::array<double, 4>{} && !gate.armed,
            "disconnect retained a command");
    require(gate.Filter(true, {.7, 0, 0, 0})[0] == 0 && !gate.armed, "reconnect bypassed low throttle check");
    gate.Filter(true, {0, 0, 0, 0});
    gate.Filter(true, {std::numeric_limits<double>::quiet_NaN(), 0, 0, 0});
    require(!gate.armed, "nonfinite sample not disarmed");
    std::cout << "RC calibration, endpoint/centre normalization, inversion and reconnect failsafe passed\n";
}
