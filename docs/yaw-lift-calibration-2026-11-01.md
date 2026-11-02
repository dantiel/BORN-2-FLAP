# Yaw lift reversal: diagnosis and calibration

The observed reversal was real. The earlier yaw check tested the initial load
after a yaw step, before the elastic wing had adapted. It missed the opposite
response over sustained flap cycles.

## Cause

The local velocity sign was already correct: `omega cross position` makes the
left/outside wing faster during a right yaw. Every strip's force and lever arm
already contribute to its moment.

Two effects reversed the loaded response:

1. The Kestrel's torsional calibration was too compliant. As speed increased,
   outboard washout redistributed lift inward enough to reverse the lift's
   rolling moment, even when total wing lift increased.
2. With `FlapCentre=0`, downstroke ferocity 0 and return ferocity 50, the loaded
   servo motion was biased below the shoulders. The effective anhedral made
   sideslip oppose the intended bank during powered flight.

This is why increasing lift globally or swapping the yaw sign would have been
incorrect. The relevant quantities are the distribution of signed strip forces,
their individual moments, and the changing surface angles.

The interpretation of yaw-induced spanwise loading is consistent with
[NASA's rotating-wing measurements](https://ntrs.nasa.gov/citations/19930091451).
These references support the mechanism; the game's numerical calibration below
is not a measurement of a real Kestrel airframe.

## Changes

- Kestrel torsion scale: **5 to 20**, in `Model.hs`. Bending stiffness is unchanged.
  The calibration is specific to the normal 50 g Kestrel; Raven, Prototype,
  Turtle and the light Kestrel retain their existing spar parameters.
- Current saved stroke trim: **FlapCentre=-12**, **ReturnFerocity=0**. This raises
  the commanded stroke centre by 12 degrees and smooths the return stroke.
  Downstroke ferocity remains 0. Other saved values are unchanged.
- No lift/drag multiplier, yaw-sign reversal or attitude target was introduced.
  The elastic solver, servo torque limits and common glide/flap force equations
  remain active.
- F5 now includes signed world-vertical lift, body roll moment and mean strip
  forward airspeed for each wing, exponentially smoothed with a 0.5 s time
  constant. The averages are updated at physics rate while F5 is enabled.
  It identifies the outside wing from actual yaw rate. Instantaneous vector
  arrows remain available.
- The Flap Centre tooltip now describes dihedral/anhedral correctly; its old
  nose-up/nose-down description confused stroke position with incidence.

The old preferences are backed up at
`.setup/yaw-sign-20261101/preferences-before.ini`. A tracked reproduction profile
is [KestrelBalanced.ini](../Unreal/Born2Flap/Config/FlightProfiles/KestrelBalanced.ini).
It does not automatically overwrite profiles on other installations.

## Before and after

At 8 m/s, 72% throttle, right yaw rate 0.6 rad/s, a constrained wind tunnel leaves
both servos and elastic wings active and averages complete measured flap cycles:

| Quantity | Before | Corrected |
| --- | ---: | ---: |
| Outside minus inside vertical lift | -0.0267 N | +0.1270 N |
| Lift rolling moment about body X | +0.0139 Nm | -0.0363 Nm |

With +X forward, +Y right and +Z up, a negative X moment raises the left/outside
wing when upright. Reversing yaw mirrors these signs.

The original calibration fails the new regression. The corrected calibration
passes all 57 checks at each tested speed (5, 8 and 12 m/s), covering glide, 72%
and full throttle, both directions, isolated forward-speed differences, actual
yaw, and sideslip. The 8 m/s case also passes at 480 Hz. These are specified trim
checks, not an assertion that anhedral or stalled/inverted flight must always
produce the same response.

An intermediate torsion scale of 8 corrected the 8 m/s derivatives but retained
reversal at 12 m/s and failed the existing full-flight pull-up criterion. It was
not retained. Increasing mount incidence also failed full-flight checks and was
not retained.

## Runtime validation

- Actual yaw-stick tests, with no roll command and free flight under gravity:
  right command changes heading +165.52 degrees and gives +32.86 degrees mean
  Unreal bank; left command gives -165.51 / -32.85 degrees. Both pass with zero
  solver failures or safety resets. Unreal Euler roll has the opposite sign to
  the body-axis angular moment convention above.
- Installed DLL, saved profile, 110-second flight test: **PASS**, including
  climb, coast, turn, pull-up, landing, reset and relaunch. Pull-up speed falls
  from 6.24 to 3.48 m/s while altitude rises from 12.40 to 13.96 m.
- Recovery test: **PASS**, residual measured banks 0.79 / 0.47 degrees, zero
  solver failures or safety resets.
- MathCore conservation/passivity, geometry and inverted-reflection properties:
  **PASS**. Actual DLL diagnostics/force closure: **PASS**.
- Both F5 modes were captured and inspected in Murex during the yaw test. Images
  and logs are under `.setup/yaw-sign-20261101/`.

Once the bird is already rolling, the rising wing's angle of attack changes.
Roll damping can temporarily give it less lift despite greater forward speed.
Likewise the inside wing normally moves more slowly in a turn. Neither an
instantaneous arrow nor forward speed alone establishes a sign error. Compare
the signed force histories and resulting motion. The corrected tests cover the
initial speed contribution, sustained compliant-wing response and actual turn.

The tail retains the pre-existing simplified inverted-V aerodynamic model;
the rendered Kestrel fan is flat. Reconciling that geometry remains separate
work. No tail coefficients were changed to conceal the wing reversal. There is
still no explicit shed-wake/tandem-wing interaction solver.

## Reproduction

```powershell
python Tools/test-yaw-trim.py --preferences Unreal/Born2Flap/Config/FlightProfiles/KestrelBalanced.ini
python Tools/test-yaw-trim.py --preferences Unreal/Born2Flap/Config/FlightProfiles/KestrelBalanced.ini --speed 5
python Tools/test-yaw-trim.py --preferences Unreal/Born2Flap/Config/FlightProfiles/KestrelBalanced.ini --speed 12
python Tools/test-yaw-trim.py --preferences Unreal/Born2Flap/Config/FlightProfiles/KestrelBalanced.ini --hz 480
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/test-yaw-bank.ps1 -Preferences Unreal/Born2Flap/Config/FlightProfiles/KestrelBalanced.ini
powershell -NoProfile -ExecutionPolicy Bypass -File Unreal/Born2Flap/Tools/test-flight.ps1 -FrameRates 60 -SkipSoak -BirdModel 2 -Preferences Unreal/Born2Flap/Config/FlightProfiles/KestrelBalanced.ini -PhysicsHz 240
```
