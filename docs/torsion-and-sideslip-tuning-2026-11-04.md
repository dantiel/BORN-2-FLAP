# Kestrel torsion and sideslip, 4 November 2026

Small tuning pass on the user's accepted 55 cm half-span model. Geometry, spar lengths/diameters, bend calibration (4), structural damping (0.15), tail trim and player preferences are preserved.

- Torsion calibration: 20 -> 17. The physical torsional stiffness is 15% lower. The renderer already applies `twist_elastic_rad` directly to each Kestrel section; no visual exaggeration is added.
- Kestrel aft-body lateral polar: lift slope 0.45 -> 0.52, crossflow pressure coefficient 2.5 -> 2.875. Both act through the existing surface force and its aft lever arm in glide and flap. The pitch panel and zero-sideslip profile drag are unchanged. Other bird models retain their previous lateral polar.

## Measurement

Snapshot: `.setup/torsion-slip-20261104/user.ini` (50 g, CG -18 mm, tail -7.6 degrees, 750 deg/s servo, 0.74 Nm, wheel 0.04).

Fixed-flow powered comparison at 5 m/s, 5 degrees body airflow angle, throttle 0.72, actual saved wheel 0.04:

| Quantity | Before | After |
|---|---:|---:|
| Mean absolute tip elastic twist | 0.8741 deg | 1.0275 deg |
| Mean absolute tip bending | 22.8333 mm | 22.8165 mm |
| Wing forward force | 0.13809 N | 0.13903 N |
| Wing vertical force | 0.55337 N | 0.55448 N |
| Wing pitch about CG, nose-down positive | 0.034539 Nm | 0.034554 Nm |

In the separate +/-0.5 m/s sideslip fixture at the standard wheel 0.5, opposing lateral force grows from 0.01029 to 0.01140 N in glide and 0.07094 to 0.07199 N powered. Restoring yaw moment magnitude grows from 0.00161 to 0.00197 Nm in glide and 0.00226 to 0.00257 Nm powered. Responses remain mirrored. These are constrained force measurements, not a claim of a particular reduction in free-flight drift distance.

The old derivative fixture's four powered sideslip lift/roll sign gates fail both before and after this change at this profile. They do not fail newly, and do not invalidate the measured opposing side force. No attempt was made to impose a fixed bank response to sideslip or to change the user's yaw control scheme.

## Validation

- Haskell property suite: PASS.
- Native launch at 30 FPS, saved settings: PASS before and after; peak pitch 46.629 -> 46.106 degrees, late mean 8.072 -> 7.854 degrees, no solver errors. Final speed 4.346 -> 4.351 m/s, altitude 104.035 -> 104.036 m.
- Wing/tail geometry against renderer: PASS.
- Angular impulse checks: PASS.

The visible spring response still needs the user's flight assessment. Baseline DLL and evidence are retained under `.setup/torsion-slip-20261104/`.

Installed to `Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll` after the user closed the editor. Installed SHA256 verified against the tested candidate: `2A4C1E4B6C3CB3C80B21C7CD880B5CC7AC875A5E1E4CEDAA560E03912AE84A3F`. Wing and tail geometry checks passed again against the installed DLL.
