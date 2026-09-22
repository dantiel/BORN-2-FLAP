# Firmware feedback corrections

Flap coordinates are positive wing-up on both sides. Their generalized hinge
torques are `side * Mx`, with side -1 left and +1 right; body pitch moment is
not the flap actuator load. This assumes the current root hinge at the origin.

Firmware steps reject nonfinite pilot/body input and invalid timesteps. Failed
output validation rolls back the state using the Simulation monad. The FFI
marshals accepted results before committing and reports rejected steps as 0.

Battery voltage now uses evolving charge and estimated current from previous
hinge loads. This is a delayed empirical current estimate, not a calibrated
motor electrical model. Inertial current and idle losses remain absent. Passive
servo backdrive remains active at zero voltage.

The Haskell RTS and loaded library live for the host process. Aircraft release
their own contexts. `b2f_math_runtime_shutdown` is a compatibility no-op; callers
must keep the library loaded until process exit. Unreal pins its loader handle;
replacing the backend binary requires restarting the editor. Initialization is
serialized in C. Individual vehicle contexts must not be stepped/destroyed
concurrently. Unreal bridge loading belongs on the game thread.

GHC cannot reliably reinitialize after final hs_exit:
https://ghc.gitlab.haskell.org/ghc/doc/users_guide/exts/ffi.html

Run `python3 Native/tests/abi_smoke.py /absolute/path/to/library` to test the
actual Haskell backend from a foreign host, including repeated sessions,
multi-vehicle lifetime and rejected-input rollback. Keep the RTS dependencies
available to the dynamic loader. Pure Haskell tests alone do not cover this.
