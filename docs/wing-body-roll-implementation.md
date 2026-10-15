# Coupled wing/body roll implementation

## Runtime changes

`RollDynamics.hs` solves body roll and both parallel wing hinges together.
Internal motor torque and joint-stop impulses preserve the body-coordinate
angular momentum equation. The old post-hoc recoil term is removed.

The existing servo controller supplies a bounded motor-torque demand instead
of prescribing the physical joint trajectory. Its implicit velocity feedback
reproduces the former fixed-base reference when the base is held and the load
is constant, while allowing the free body and opposite wing to participate.
Motor limits still depend on voltage and stall torque. The backdrive setting
controls dissipative resistance under overpowering loads; a dead battery
supplies no motor torque, but airflow can move its wings.

Aerodynamic resistance is linearized with a central-difference Jacobian of
body roll and both wing velocities at the current configuration. The coupled
implicit solve includes all three velocity dependencies. Trial evaluations
skip the structural beam solve; deformation state is committed once afterward.
Unconverged steps retry at smaller time steps, including the intermediate body
roll response, and fail transactionally if subdivision cannot resolve them.
Mirrored strip moments are summed pairwise to avoid seeding artificial roll
from cancellation error in symmetric flight.

ABI 12 adds required `B2F_BodyState.roll_inertia_kg_m2`. Unreal supplies its
actual dry, locked-wing Chaos inertia. The mass matrix adds modeled fluid
inertia once, and the returned roll torque is the equivalent impulse for the
host inertia. It must not receive a second recoil or added-mass correction.
The prediction loop now uses the same CG-adjusted moment as the applied load.
An incompatible DLL is rejected by the existing ABI version check.

## Reproduction

```powershell
$env:CABAL_DIR='V:/Born2FlapTools/cabal'
cabal test math-properties --test-show-details=direct --project-dir=MathCore
& Unreal/Born2Flap/Tools/build.ps1
python Tools/test-roll-coupling.py
```

The DLL probe also accepts `--dll <path>` for isolated builds. It tests invalid
inertial inputs, mirrored roll, finite motion, joint travel, two host inertias,
0/8/15 m/s airflow, and 120/240/480 Hz step sizes. Its translation/pitch/yaw
are constrained like a roll-axis wind-tunnel fixture; it is not full flight.

MathCore tests cover the analytic coupled acceleration, angular momentum,
symmetric strokes, passive dissipation, end-stop energy/momentum, loss of motor
power, controller integration, and aerodynamic roll symmetry. Long integration
tests explicitly reject failed steps rather than averaging rollback outputs.

## Scope and limits

This is a parallel-hinge roll correction, not a complete articulated six-axis
fluid/structure simulator. Fluid heave/pitch/yaw cross-inertia, moving structural
CG, detailed rotor/gear inertia, and exact offset-hinge inertia still need a
shared physical geometry and component mass model. The host's dry inertia is
still derived from its existing collision geometry. No measured mass values
were invented, and wings are not constrained to an artificial world-space pose.

The numerical correction changes handling; existing open-loop maneuver tests
must be reported separately from conservation and roll-fixture results.
Full Unreal verification results are recorded below after execution.
