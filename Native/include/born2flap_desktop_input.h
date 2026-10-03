#pragma once
#include "born2flap_rc_input.h"

namespace born2flap
{
// Desktop transmitter: independent keyboard and mouse steering, one throttle owner.
struct DesktopInput
{
    static constexpr double MouseTravel = 1800.0; // pixels from centre to full stick
    RcKeyboard keyboard;
    double speedModifier = 0.5; // 0..1 flapping speed modifier (mouse wheel / RC CH6)
    double mouseRoll = 0, mousePitch = 0, mouseYaw = 0;
    bool previousMuteYaw = false, previousMuteRoll = false;
    double throttle = 0, roll = 0, pitch = 0, yaw = 0;

    static double Expo(double Stick, double Amount)
    {
        const double E=std::clamp(Amount,0.0,1.0);
        return (1-E)*Stick+E*Stick*Stick*Stick;
    }

    static double KeyboardThrottle(bool w, bool control, bool shift)
    {
        return !w ? 0 : control ? .32 : shift ? 1 : .72;
    }

    void Step(double dt, double keyThrottle, bool wDown, double keyRoll, double keyPitch, double keyYaw,
              double mouseX, double mouseY, double wheel, bool muteYaw, bool muteRoll, bool resetMouse = false,
              double rollGain = 1, double pitchGain = 1, double yawGain = 1, double expo = .65)
    {
        if (dt <= 0) return;
        // The wheel drives the flapping speed modifier, not throttle. Keyboard
        // owns throttle; the RC CH6 knob overrides this value while connected.
        (void)wDown;
        if (wheel != 0)
            speedModifier = std::clamp(speedModifier + wheel * .02, 0.0, 1.0);
        keyboard.Step(dt, keyThrottle, keyRoll, keyPitch, keyYaw);

        // Relative displacement moves a persistent virtual stick; stopping never centres it.
        // Per-frame mouse deltas already integrate motion, so do not scale them by dt.
        const bool clicked = resetMouse || (muteYaw && !previousMuteYaw) || (muteRoll && !previousMuteRoll);
        previousMuteYaw = muteYaw;
        previousMuteRoll = muteRoll;
        if (clicked)
            mouseRoll = mousePitch = mouseYaw = 0;
        else
        {
            mouseRoll = muteRoll ? 0 : std::clamp(mouseRoll + mouseX * rollGain / MouseTravel, -1.0, 1.0);
            mouseYaw = muteYaw ? 0 : std::clamp(mouseYaw + mouseX * yawGain / MouseTravel, -1.0, 1.0);
            mousePitch = std::clamp(mousePitch + mouseY * pitchGain / MouseTravel, -1.0, 1.0);
        }
        throttle = keyboard.throttle;
        // Expo each device before summing: opposing inputs cancel and either can add authority.
        roll = std::clamp(Expo(keyboard.roll,expo) + Expo(mouseRoll,expo), -1.0, 1.0);
        pitch = std::clamp(Expo(keyboard.pitch,expo) + Expo(mousePitch,expo), -1.0, 1.0);
        yaw = std::clamp(Expo(keyboard.yaw,expo) + Expo(mouseYaw,expo), -1.0, 1.0);
    }
};
}