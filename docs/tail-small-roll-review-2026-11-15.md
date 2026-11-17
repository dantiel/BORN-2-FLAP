# Tail small-roll sensitivity and concurrent physics review, 2026-11-15

The user reports excessive response during MOUSE GLIDE. The mouse sends roll
and yaw, but the glide branch ignores yaw; the fixed Kestrel fan consumes no
rudder mix. Therefore powered differential-amplitude yaw is not the explanation.
At saved AileronScale=100 and Expo=0.42, 1% raw mouse stick is 0.580042% after
expo and commands +/-0.464 degrees shoulder differential (before saturation).
No near-centre discontinuity was found. No input scheme or sensitivity changed.

Active settings when inspected: tail -7.9 degrees, CG 0, 50 g. A prescribed
8 m/s, zero-sink, zero-rate tail sample from the installed ABI 21 DLL gives:
- circulation Fz=-0.39697 N;
- normal-pressure Fz=-0.04809 N (Cp=2 baseline);
- moment My=-0.14940 Nm (nose-up).
The aircraft weight is 0.4905 N at g=9.81. This is a highly loaded tail, not
missing broadside pressure. These are constrained flow measurements, NOT a
trimmed glide prediction. Net yaw also depends on the opposing wing moments.
A 1 degree body bank projects this tail moment into -0.002607 Nm world yaw;
a 1% roll input with identical pose/flow leaves the tail load unchanged.

## Independently supported tail correction

Earlier AVL checks validated only CL. The saved standalone AVL 3.52 forward
flow case (alpha 3 degrees, fan-beta0-c12n96/total.txt) also reports
CL=0.12128, CZ=-0.12123, Cm=-0.03710, Cref=1 m. Its force lever is 0.30603 m.
The code applied circulation at the area centroid (~0.33512 m), overstating
that lever by ~9.5%. This is distinct from finite-tail lift-slope calibration.

Each area patch now assigns circulation and its induced drag to the quarter
chord of its streamwise polygon slice. The slice follows full in-plane flow,
including sideslip/reverse flow. Their velocity is sampled at that actual
point. Broadside pressure and skin friction retain area quadrature and local
velocity there. The physical offset-force couple is transmitted in tfMoment.
The public ABI layout is unchanged; moment_nm must be used rather than
reconstructing r_area cross total force. The native diagnostic was updated
accordingly (source change only; native module rebuild remains pending).

Quarter-chord section placement is a thin-airfoil approximation, not an exact
3D fan solution. Area-weighted forward circulation lever becomes 0.29226 m
(~4.5% short of AVL). Across sideways-flow bearings, independent spanwise
loading differences remain; this does not validate arbitrary stalled wakes.
Reference: https://www.grc.nasa.gov/www/k-12/VirtualAero/BottleRocket/airplane/ac.html

In the short actual-DLL check, -7.9 degree tail moment changes from -0.14940
to -0.13239 Nm: 11.4% less moment with essentially unchanged force. At -12.6
degrees it changes from -0.25263 to -0.22552 Nm. These are force evaluations,
not long simulations or runtime handling acceptance. This cannot establish
that the user's much larger perceived sensitivity has fully disappeared.

## Other-agent changes

- Removed new Cp=2.5 and additional stalled-wing multiplier 1.8 (effective
  Cp=4.5). The broadside law already represents bluff pressure resistance;
  no independent support for doubling it was supplied. It increases upstroke
  resistance as well as downstroke loads. Restored the existing shared Cp=2
  baseline, which itself remains provisional, not a measured universal law.
  The installed DLL sampled above was still on the baseline: the new source
  boost must not be blamed for a symptom already present in that DLL.
- Kramer coefficient 0.785 -> 1.6: real signed pitching-circulation mechanism,
  not an explicit attitude force, but the new value is uncalibrated. Its
  justification mixed published Crot and CL normalizations and ignored that
  local quarter-chord velocity already contains some pitch motion. Retained
  the value for now, removed claims it guarantees downstroke pitch-up. During
  nose-down washout it may REDUCE lift. Independent calibration remains open.
  Reference mechanism: https://pubmed.ncbi.nlm.nih.gov/11919268/
- Oswald e=0.82 -> 0.88 gives 6.8% lower induced drag. Retained as provisional
  tuning; removed the unsupported claim of empirical calibration and the
  reversed statement about ideal elliptical loading (minimum drag, e=1).
- Structural damping 0.12 -> 0.08: legitimate passive parameter, retained.
  Corrected the claim of near-lossless carbon recoil: a linear mode with
  damping ratio 0.08 retains ~36% energy after a damped period, before any
  aerodynamic losses. Material label alone does not determine modal damping.
- Other agent changed torsion 4.2 -> 4.6. Restored the user's explicit 4.2.
- No artificial fuselage inertia gain or tail roll damper added. Tail roll
  resistance comes from its local flow and actual force points. Increased
  inertia reduces angular acceleration; it cannot create a missing pitch-up
  lever. Wings remain mostly aft of CG=0, as the preceding geometry audit
  documents. Thrust creates roll via its vertical/lateral force components
  and coupled motion, not directly via forward Fx in Mx=yFz-zFy.

## Validation status

New independent forward-flow tail Cm test (8% approximation tolerance) passes;
the old centroid placement missed by more than 10%. Tail tests include total
wrench passivity F dot v_ref + (M-r_ref cross F) dot omega, local velocity,
incidence reversal and roll damping. These passed in the first full suite,
which was compiled with the other agent's 4.6 setting.

After restoring 4.2 the full standard suite FAILS its mirrored powered-roll
check: left/right centre-differential moments 0.000860117838 and
-0.000862114420 Nm; sum -0.000001996582 Nm exceeds the existing 1e-6 absolute
tolerance (~0.23% of response). The tolerance was NOT relaxed. The failure's
numerical versus physical origin is unresolved; later assertions in that run
were not reached. Do not call this candidate fully regression-clean.

No Unreal flight tests were launched. Existing independent reference data and
very short prescribed-input DLL evaluations were used. Native diagnostic
rebuild and gameplay acceptance remain outstanding.

Final candidate build succeeds with stiffness 4.2. Short actual-DLL attached
flow sweep completes for alpha -3/0/+3 degrees and bearings 0/15/30/60/90;
reference CL errors remain about 3-11%. Full regression failure above remains.
Candidate: .setup/tail-strength-20261115/tail-centre-final.dll
SHA256 FD9161B38AB53BFE8A1D1DBF29416C7A6F4B8D2F36FB8224CF0D6BE063DDBF5F
Not installed: UnrealEditor PID 2716 holds the current runtime, and this
candidate is not being presented as regression-clean. No runtime acceptance.
