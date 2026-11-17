# Kestrel pitch lever and mount-height correction, 2026-11-15

The active Flight CG setting is 0 mm. Neutral quarter-chord x positions from
actual scaled planform (body +x forward) are +14.24 mm at the root, -15.39 mm
at 25% span, -16.07 mm at 50%, -18.94 mm at 75%, and -11.31 mm at 91.7%.
Thus most attached lift is not ahead of this CG. Increasing it alone would
increase nose-down pitch. The correct pitch identity is
My = (z-zCG)*Fx - (x-xCG)*Fz + section couple; +My is nose-down.

Current native source places Kestrel shoulders at z=2 cm, but aerodynamic,
dry-inertia and spatial-fluid code still assumed z=4 cm. Added mount height
to WingShape and set Kestrel to the rendered 2 cm. Aerodynamic positions and
velocities, generalized hinge loads, dry inertia and spatial added inertia
now read the same field. Other built-in models retain their prior 4 cm
physics height. Existing external model files default to 4 cm; serialization
now preserves root_height_m. Mount height is not scaled when scaling only
the wing outline. No force gain, CG adjustment or forward force-point shift.

A new uniform-flow regression verifies both wings' force remains unchanged
under the 2 cm translation and moment changes exactly by delta-r cross F.
The existing yawed-hinge geometry check now uses each model's mount height.
Legacy diagnostic root-height/hinge helpers refer to the reference Kestrel;
production code uses the selected model.

Reanalysis of historical moments-yaw0-candidate.csv (documented CG x=+16 mm;
see wing-release-and-fix-audit-2026-11-13.md): t>2 s, right-wing downstroke
rate < -5 deg/s. Fz-weighted force location is x=-13.21 mm. At unchanged
loads, moving CG aft by 30 mm changes My by -0.03648 Nm per wing. That is a
moment-reference calculation, NOT a prediction after retrimming. Downstroke
thrust averaged 0.25483 N, so the height correction alone reduces its
nose-down contribution by 0.00510 Nm per wing in this historical case.
It cannot honestly be described as a large new lift lever or solved handling.

Torsional scale remains 4.2; tail slope correction and artificial-dihedral
removal remain. No saved preferences changed; no Unreal flight simulations.

Validation: full math-properties suite PASS, including mount translation,
virtual-work, mass/CG and hinge checks. DLL build PASS; ABI 21 initialization
and Kestrel creation PASS. No runtime flight acceptance claimed.
Candidate .setup/tail-strength-20261115/mount-height.dll SHA256:
6B27F74263515E6E9AF449F12BF80CD96A5FF419313BA387299A1B315D2C0D9F
Installation attempted and blocked by the open installed physics DLL.
