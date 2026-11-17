# Remove fictitious dihedral; correct compact-tail lift slope

Removed `effectiveDihedralRad=0.262` and the extra span-flow incidence from
Vehicle.hs. Actual wing geometry, apparent wind and physical camber determine
angle of attack again. Retained the other task's requested softer GJ=5.4;
its unused sectionTwistLever edit does not affect production loads.

The fixed fan was using the higher-aspect-ratio approximation AR/(AR+2).
Replaced its denominator with Helmbold's 2+sqrt(4+AR^2), retaining the same
section efficiency, full 3D flow, pressure law, physical lever arms and CG
mapping. This is a low-aspect-ratio model correction, not a bank-dependent
multiplier, yaw cap or tail control change. Wing lift slopes are unchanged.

Reference: [NACA TN 3911, Appendix A](https://ntrs.nasa.gov/api/citations/19930084818/downloads/19930084818.pdf).
Independent check: previously generated standalone MIT AVL 3.52 fan geometry,
12 chordwise/96 spanwise panels, 3 degrees attached-flow angle of attack.

| In-plane bearing | Before CL | New CL | AVL CL |
|---:|---:|---:|---:|
| 0 | .14142 | .11771 | .12128 |
| 15 | .13657 | .11204 | .11571 |
| 30 | .12700 | .10112 | .10561 |
| 60 | .13212 | .10692 | .11402 |
| 90 | .11012 | .08285 | .09267 |

The new approximation underpredicts this reference by 3-11%, compared with
the former 16-20% overprediction. This validates neither separated flow nor
unsteady wake interactions. A regression checks these independent coefficients
with a declared 12% approximation tolerance; existing physical property tests
also pass, including pressure passivity, tail moment mapping and incidence
sign reversal.

Active saved settings now have tail incidence -12.6 degrees and CG offset 0,
versus -10 degrees / -16 mm in the previous session. Saved model-profile
values differ from the active flight values. No preferences were changed.
At prescribed 8 m/s forward flow with no sink and CG x=0, the tail pitching
moment changes from -0.2864 to -0.2451 Nm (nose-up), a 14.4% reduction including
unchanged pressure and drag. Banking projects this moment into yaw; it does
not multiply its magnitude. The remaining moment is still large for this
50 g aircraft, so this correction is not proof of stable trim or recovery.

Artifacts: `.setup/tail-strength-20261115`. `corrected.dll` removes artificial
dihedral only; `tail.dll` additionally corrects the fan slope. Paired native
tests therefore isolate the tail correction from removing the fake term.
Installed DLL SHA256:
`62A9C1BBAFEC03BDBF1D3A88991422B658C30BD33C577999F3DC692AD6F3DB29`.

## Native runtime validation

The rendered Unreal test loaded ABI 21 from the normal installed ThirdParty
path. The installed hash was rechecked after testing and matches above.
The end screenshot was inspected: the bird remains strongly banked; it is
not evidence of successful recovery. All traced recovery runs reported zero
solver failures. Rendered and headless positive-input runs matched final pose.

Slow (8 s ramp) and fast (0.25 s ramp) loaded glide roll tests both PASS:
body rotations 19.121 and 18.554 degrees respectively. These are constrained
roll tests, not free-flight recovery acceptance.

Paired tests compare removal-only against removal plus the tail correction:

| Test | Removal only | Tail correction |
|---|---|---|
| Handling | FAIL; recovered bank 56.59 / 61.44 deg | FAIL; 58.89 / 56.36 deg |
| Flight, 110 s | FAIL turn and landing; peak altitude 64.527 m | FAIL turn and landing; peak altitude 59.913 m |

Both broad tests report safety=0 and mathFailures=0. Recovery remains mixed,
not fixed: both signs of the combined roll/pitch/yaw pulse pass through an
inverted attitude and retain large angular rates after release. At t=7 s,
the negative-input run has altitude 11.79 -> 9.28 m despite a more upright
wing normal; the positive-input run has 24.27 -> 22.37 m. At t=13 s these
altitudes are 9.25 -> 12.29 m and 21.57 -> 25.91 m. The correction changes
actual flight but does not establish consistently better recovery.
The fixture's RollResponseTest FAIL for these pulse/release experiments is
not used as a recovery metric because its assertion assumes a held roll ramp.

The remaining instability needs further force/trim investigation. No attitude
stabilizer, artificial dihedral, bank-dependent tail gain, or saved tuning
change was introduced to conceal these failures.

Both 180 s soak runs PASS with safety=0 and mathFailures=0. Final altitude:
removal-only 96.88 m; corrected tail 85.21 m. Final speed: 5.11 / 5.53 m/s.
This checks sustained execution, not stable attitude or recovery quality.

## Follow-up: requested torsional stiffness 4.8

Changed Kestrel siTwistScale 5.4 -> 4.8, giving 12.5% greater static
compliance for the same torque. No force or moment multiplier was added.
The current local circulation direction uses sectionVelocity cross oriented
spanAxis; moments use r cross F plus section couples. In the flat-wing case,
velocity (U,0,-w) gives circulation direction proportional to (w,0,U).
More downward speed therefore tilts circulation forward naturally. Changing
that direction to create extra vertical force would require another physical
mechanism, not just a pitch/roll gain. Softer torsion may reduce angle of
attack and lift as well as change pressure direction.

Reanalysed EXISTING .setup/wing-release-20261113/moments-yaw0-candidate.csv,
right wing, t>2 s, rates below -5 / above +5 deg/s. Mean downstroke force:
Fx=0.25483 N, Fz=1.21610 N; upstroke Fx=0.12036 N, Fz=-0.62659 N.
These historical settings/build are not the current candidate. They suggest
upstroke cancellation merits measurement before assuming weak downstroke
lift. This turn did not rerun flight simulations or claim recovery improved.

Build succeeded; candidate ABI 21 initialization and Kestrel creation PASS.
Candidate: .setup/tail-strength-20261115/twist48.dll
SHA256: A6BC2C991D105A316E2D6397364D799EBFFAE7D618C8D689C9516A77988368FF
Installation FAILED due to an open DLL held while UnrealEditor PID 50044 ran.
Installed DLL at that time had changed independently to SHA256
D8B95C9FD0ABDD21F13E5D9DA971EDB454269BF94291CCA8ABDA59BB43F194FE.
Do not confuse its successful initialization with candidate installation.
The source retains the corrected tail slope and has no artificial dihedral.

### Subsequent request: stiffness 4.2 and induced drag

Kestrel siTwistScale now 4.2 (14.3% more static compliance than 4.8).
Build and candidate ABI 21 initialization/Kestrel creation pass. No flight
simulation was run. twist42.dll SHA256:
2D6A5F4573543059C87436D9F10E39598AEEED738E48DE8F311BA60ABA392E41
Installation is still blocked by the open installed DLL; this candidate is
NOT confirmed running. Prior tail correction and fake-dihedral removal remain
in source.

Source inspection: inducedCd=cl^2/(pi*0.82*effectiveAspect), using complete
wing-pair aspect ratio 7.40187 (single-wing area 0.0817361 m^2). Away from
ground, Di/abs(L)=5.24% for Cl=1, 9.96% for Cl=1.9. These are algebraic local
ratios, not measured total body drag or an asymmetric-flapping validation.
No obvious coefficient inflation found. Strip-local application of a steady
finite-wing formula to asymmetric/unsteady circulation remains an unvalidated
approximation; wake interactions are not resolved by this ratio check.
Excess induced drag could cost speed and introduce adverse yaw, but its role
in the reported handling has not been established. Coefficient unchanged.
Reference: https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/induced-drag-coefficient/
