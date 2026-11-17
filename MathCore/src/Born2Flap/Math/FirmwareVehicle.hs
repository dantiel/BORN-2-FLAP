{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | The full "simulation in the simulation": firmware mixer → servo actuator
-- → wing aerodynamics → aerodynamic hinge torque feeding back into the servo.
--
-- The mixer derived from PteronautOS, with simulator glide/timing extensions,
-- reacts to RC pilot input; its wing-servo
-- commands are realised by a servo model that *struggles* against the real
-- aerodynamic hinge torque the wings produce; and the resulting actual flap
-- angle drives the wing physics. Servo, wing size, and battery are all
-- selectable. Coefficients and actuator parameters remain provisional and
-- require calibration against physical measurements.
module Born2Flap.Math.FirmwareVehicle
  ( FirmwareVehicleState(..)
  , defaultFirmwareVehicleState
  , stepFirmwareVehicle
  , stepFirmwareVehicleWithMassInertia
  , stepFirmwareVehicleWithInertia
  , hingeTorque
  , tailSurface
  , TailField(..), kestrelTailLoads
  , transportAirfields
  ) where

import Born2Flap.Math.LiftField (PressureField(..), transportVector)
import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Firmware
import Born2Flap.Math.Servo
import Born2Flap.Math.RollDynamics (solveImplicitSystemCached)
import Born2Flap.Math.SpatialDynamics (spatialSystem, tailFluidInertia, multiply)
import Born2Flap.Math.Model (WingModel(..))
import Born2Flap.Math.Airframe (airframeMasses, tailRootX, tailOutline, tailSamples)
import Born2Flap.Math.Planform
  ( referenceWingMassKg, shapeAspectRatio, WingShape(..) )
import Born2Flap.Math.Simulation
import Born2Flap.Math.Waveform (OscillatorState(..), limiarFromFerocities)
import Born2Flap.Math.PhaseEnvelope
import Born2Flap.Math.ResponsePhase
import Born2Flap.Math.Vehicle
  ( VehicleInput(..), VehicleOutput(..)
  , StripState(..), StripResult(..), initialStrips
  , WingSystem(..), prepareWingSystem, wingRootHeight, airDensity, surfaceSkinCd, surfacePressure
  , finiteWingLiftSlope, rollLiftSlopeRatio, inducedDragCoefficient
  , addVec, crossVec, scaleVec, magnitude, zeroVec, radians )

data FirmwareVehicleState = FirmwareVehicleState
  { fvTangent         :: !(Maybe [[Double]])
  , fvFirmware        :: !FirmwareState
  , fvServoLeft       :: !ServoState
  , fvServoRight      :: !ServoState
  , fvLeftStrips      :: ![StripState]
  , fvRightStrips     :: ![StripState]
  , fvLeftStripForceMag  :: ![Double]  -- ^ per-strip |aero force| [N] root→tip, last step (audio drive)
  , fvRightStripForceMag :: ![Double]
  , fvLeftHingeTorqueNm  :: !Double   -- ^ aero load fed to the servo next step
  , fvRightHingeTorqueNm :: !Double
  , fvMotorCurrentA   :: !Double     -- ^ previous solved motor current, for supply sag
  , fvBatterySoc      :: !Double     -- ^ 0..1, drains under servo current
  , fvBatteryBoost    :: !Double     -- ^ 0..1, LiPo burst reservoir (C-rating headroom)
  , fvPhaseEnvelope   :: !PhaseEnvelopeState  -- ^ ONDAS A-layer
  , fvResponsePhase   :: !ResponsePhaseState  -- ^ read-only response-phase diagnostic (no controller output)
  , fvFlapHz          :: !Double              -- ^ commanded flap cadence [Hz] from the mixer (servo-capped); glide reports the soar-pump Hz
  , fvTailIncidenceDeg :: !Double             -- ^ actual common tail incidence for the rendered fan
  , fvTailFields :: ![TailField]              -- ^ exact solved fixed-fan force diagnostics
  , fvExternalForce :: !Vec3                 -- ^ surface forces before host added-mass/recoil conversion
  , fvSolvedBodyRates :: !Vec3               -- ^ converged spatial rates, before host impulse conversion
  , fvDriveTorques :: !(Double,Double)       -- ^ signed motor torques, including static holding torque
  , fvRollBalance :: ![Double]              -- ^ external, gravity, bias, body, hinges, other inertia, drives
  , fvMassFrame :: !(Maybe (Double,Double,Double)) -- ^ CG x, dry pitch/yaw inertia; absent = fixture fallback (cg 0, host inertia for pitch/yaw)
  , fvWingCamberBias :: !(Double,Double) -- ^ left/right built-in camber addition, fraction of chord
  , fvPreviousDifferenceTarget :: !(Maybe Double) -- ^ previous commanded half-difference, radians
  , fvHangDelta     :: !Double        -- ^ fuselage roll RELATIVE to the wing plane [rad] (shoulder-hinge pendulum)
  , fvHangDeltaRate :: !Double        -- ^ [rad/s]
  } deriving stock (Eq, Show)

defaultFirmwareVehicleState :: FirmwareVehicleState
defaultFirmwareVehicleState = FirmwareVehicleState
  { fvTangent = Nothing
  , fvFirmware = defaultFirmwareState
  , fvServoLeft = defaultServoState
  , fvServoRight = defaultServoState
  , fvLeftStrips = initialStrips
  , fvRightStrips = initialStrips
  , fvLeftStripForceMag = []
  , fvRightStripForceMag = []
  , fvLeftHingeTorqueNm = 0
  , fvRightHingeTorqueNm = 0
  , fvMotorCurrentA = 0
  , fvBatterySoc = 1.0
  , fvBatteryBoost = 1.0
  , fvPhaseEnvelope = defaultPhaseEnvelope
  , fvResponsePhase = defaultResponsePhase
  , fvFlapHz = 0
  , fvTailIncidenceDeg = -2.3
  , fvTailFields = []
  , fvExternalForce = zeroVec
  , fvSolvedBodyRates = zeroVec
  , fvDriveTorques = (0,0)
  , fvRollBalance = replicate 8 0
  , fvMassFrame = Nothing
  , fvWingCamberBias = (0.0025,0.0025)
  , fvPreviousDifferenceTarget = Nothing
  , fvHangDelta = 0
  , fvHangDeltaRate = 0
  }

-- Reconcile the solver's predicted pose with an authoritative host pose.
-- This legacy field telemetry is diagnostic only. There is no air-position
-- spring or bank reference in the aerodynamic force path.
transportAirfields :: Vec3 -> FirmwareVehicleState -> FirmwareVehicleState
transportAirfields rotation state =
  let rotate=transportVector (-1) rotation
      update s=let patch=stripPressureField s
               in s {stripPressureField=patch
                 {fieldDisplacement=zeroVec,fieldDirection=rotate(fieldDirection patch)}}
  in state {fvLeftStrips=map update(fvLeftStrips state),fvRightStrips=map update(fvRightStrips state)}

-- | One closed-loop timestep.
--
--   * @rc@       — CRSF radio channels (pilot input)
--   * @params@   — firmware mixer knobs (flight profile, servo speed, …)
--   * @servo@    — servo selection (speed, stall torque)
--   * @battery@  — battery selection (voltage, resistance, capacity)
--   * @dt@       — timestep [s]
--   * @bodyVel@  — body velocity [m/s]
--   * @bodyRates@— body angular rates [rad/s]
--
-- Returns @(VehicleOutput, next state)@. @VehicleOutput@ carries the total
-- force/moment, total mechanical power, and the peak separation fraction.
stepFirmwareVehicle
  :: Bool -> RcChannels -> FirmwareParams -> ServoSpec -> BatterySpec -> WingModel
  -> Double -> Vec3 -> Vec3 -> Double
  -> FirmwareVehicleState
  -> (VehicleOutput, FirmwareVehicleState)
stepFirmwareVehicle coupled rc params servo battery model dt bodyVel bodyRates groundClearance =
  stepFirmwareVehicleWithInertia 0.008 coupled rc params servo battery model dt bodyVel bodyRates groundClearance

-- Host inertia is the dry, locked-wing roll inertia used by the rigid-body
-- integrator. Added air enters the coupled mass matrix, never the host tensor.
stepFirmwareVehicleWithInertia
  :: Double -> Bool -> RcChannels -> FirmwareParams -> ServoSpec -> BatterySpec -> WingModel
  -> Double -> Vec3 -> Vec3 -> Double -> FirmwareVehicleState
  -> (VehicleOutput, FirmwareVehicleState)
stepFirmwareVehicleWithInertia hostInertia coupled rc params servo battery model dt bodyVel bodyRates groundClearance =
  stepFirmwareVehicleWithMassInertia 0.05 hostInertia coupled rc params servo battery model dt bodyVel bodyRates groundClearance (Vec3 0 0 1)

stepFirmwareVehicleWithMassInertia
  :: Double -> Double -> Bool -> RcChannels -> FirmwareParams -> ServoSpec -> BatterySpec -> WingModel
  -> Double -> Vec3 -> Vec3 -> Double -> Vec3 -> FirmwareVehicleState
  -> (VehicleOutput, FirmwareVehicleState)
stepFirmwareVehicleWithMassInertia totalMass hostInertia coupled rc params servo battery model dt bodyVel bodyRates groundClearance bodyUp state
  | not (finite totalMass) || totalMass <= 2*referenceWingMassKg || not (finite hostInertia) || hostInertia <= 0 = (zeroOutput 2, state)
  | not (finite dt) || dt <= 0 || dt > 0.05 = (zeroOutput 1, state)
  | not (all finite (components bodyVel ++ components bodyRates ++
      [rcAileron rc, rcElevator rc, rcThrottle rc, rcRudder rc, rcArm rc, rcFreq rc, rcProfile rc])) =
      (zeroOutput 2, state)
  | otherwise = case runSimulation transaction state of
      Left flag | dt > 1/7680 -> subdivide flag
                | otherwise -> (zeroOutput flag, state)
      Right result -> result
  where
    -- Sharp stall/feather transitions can make one implicit step non-convergent.
    -- Retry two smaller physical steps, including body recoil feedback; never
    -- silently accept an unconverged solve or commit only half of a step.
    subdivide flag =
      let half = dt/2
          run velocity rates = stepFirmwareVehicleWithMassInertia totalMass hostInertia coupled rc params servo battery model half velocity rates groundClearance bodyUp
          (a, middle) = run bodyVel bodyRates state
          (cgX,iy,iz) = maybe (0,0,0) id (fvMassFrame state)
          cg = Vec3 cgX 0 0
          momentCg = addVec (totalMomentNm a) (scaleVec (-1) (crossVec cg (fvExternalForce middle)))
          middleRates = Vec3 (x bodyRates + half*x (totalMomentNm a)/hostInertia)
            (y bodyRates + if iy>0 then half*y momentCg/iy else 0)
            (z bodyRates + if iz>0 then half*z momentCg/iz else 0)
          translation = case fvMassFrame state of
            Nothing -> zeroVec
            Just _ -> scaleVec half (addVec (scaleVec (1/totalMass) (totalForceN a)) (scaleVec (-9.81) bodyUp))
          middleVelocity = addVec translation (addVec bodyVel (crossVec (addVec bodyRates (scaleVec (-1) middleRates)) cg))
          (b, final) = run middleVelocity middleRates middle
          average u v = scaleVec 0.5 (addVec u v)
      in if outputFlags a /= 0 || outputFlags b /= 0 then (zeroOutput flag,state)
         else (b { totalForceN = average (totalForceN a) (totalForceN b)
                 , totalMomentNm = average (totalMomentNm a) (totalMomentNm b)
                 , totalMechanicalPowerW = 0.5*(totalMechanicalPowerW a+totalMechanicalPowerW b)
                 , maxSeparation = max (maxSeparation a) (maxSeparation b) },
              final {fvExternalForce=average (fvExternalForce middle) (fvExternalForce final)})
    transaction = do
      old <- getState
      let (output, next) = advanceFirmwareVehicle totalMass hostInertia coupled rc params servo battery model dt bodyVel bodyRates groundClearance bodyUp old
          values = components (totalForceN output) ++ components (totalMomentNm output) ++ components (fvExternalForce next) ++
            [totalMechanicalPowerW output, maxSeparation output, fvTailIncidenceDeg next, fvBatterySoc next, fvMotorCurrentA next,
             fvBatteryBoost next,
             fvLeftHingeTorqueNm next, fvRightHingeTorqueNm next,
             servoAngleDeg (fvServoLeft next), servoAngleDeg (fvServoRight next),
             servoRateDegPerSec (fvServoLeft next), servoRateDegPerSec (fvServoRight next),
             phaseEnvelope (fvPhaseEnvelope next), phaseCoverage (fvPhaseEnvelope next)] ++
            concatMap (\s -> [stripSeparation s,stripPreviousAlpha s,stripCamber s,
                    stripBendM s,stripBendSlope s,stripTwistAero s,
                    stripBendRateMS s,stripTwistRateRadS s,stripBendSlopeRate s,fieldLoad (stripPressureField s)]
                    ++ components (fieldDisplacement (stripPressureField s))
                    ++ components (fieldDirection (stripPressureField s))
                    ++ components (stripPosition s) ++ components (stripNormal s)
                    ++ components (stripLift s) ++ components (stripPressure s)
                    ++ components (stripDrag s) ++ components (stripMoment s)
                    ++ components (stripAirVelocity s) ++ components (stripWindDelta s))
              (fvLeftStrips next ++ fvRightStrips next)
      if all finite values then putState next >> pure output else abort 2

finite :: Double -> Bool
finite value = not (isNaN value || isInfinite value)

components :: Vec3 -> [Double]
components (Vec3 a b c) = [a,b,c]

clamp :: Ord a => a -> a -> a -> a
clamp low high = max low . min high

-- Both positive stroke coordinates raise the wing. The left hinge axis is -X.
-- Elastic deformation changes the surface geometry. It cannot discount the
-- external moment transmitted to the hinge; a dynamic spring would need its
-- own inertia and stored-energy state to account for that exchange.
hingeTorque :: Double -> [StripResult] -> Double
hingeTorque = hingeTorqueAt wingRootHeight

hingeTorqueAt :: Double -> Double -> [StripResult] -> Double
hingeTorqueAt height side = (* side) . sum . map (\r -> (x (resultMoment r) + height * y (resultForce r)))

-- | Rescale the anti-symmetric (roll/dihedral) circulation lift to its own
-- slope. The strip solver charges every strip's lift through the symmetric 3D
-- Helmbold slope, but the anti-symmetric channel (opposite circulation on the
-- two wings) has its mutual trailing-vortex downwash cancelled, so it responds
-- at the higher 'rollLiftSlope'. Decompose the two wings' lift at each span
-- station into mirror-symmetric and mirror-odd parts and rescale only the odd
-- part. This is Galilean-invariant because it acts on the NET lift, not on how
-- the flow was composed. Pressure and drag are untouched; only the lift force
-- and its lever moment change.
rescaleRollLift :: Double -> ([StripResult],[StripResult]) -> ([StripResult],[StripResult])
rescaleRollLift ratio (ls,rs) = (map fst pairs, map snd pairs)
  where
    pairs = zipWith split ls rs
    mirror (Vec3 fx fy fz) = Vec3 fx (-fy) fz
    framePos (pos,_,_,_,_,_) = pos
    gain = 0.5 * (ratio - 1)
    split l r =
      let lLift = resultLift l
          rLift = resultLift r
          lAnti = scaleVec gain (addVec lLift (scaleVec (-1) (mirror rLift)))
          rAnti = scaleVec gain (addVec rLift (scaleVec (-1) (mirror lLift)))
          posL = framePos (resultFrame l)
          posR = framePos (resultFrame r)
      in ( l { resultForce = addVec (resultForce l) lAnti
             , resultMoment = addVec (resultMoment l) (crossVec posL lAnti) }
         , r { resultForce = addVec (resultForce r) rAnti
             , resultMoment = addVec (resultMoment r) (crossVec posR rAnti) } )


advanceFirmwareVehicle
  :: Double -> Double -> Bool -> RcChannels -> FirmwareParams -> ServoSpec -> BatterySpec -> WingModel
  -> Double -> Vec3 -> Vec3 -> Double -> Vec3 -> FirmwareVehicleState -> (VehicleOutput, FirmwareVehicleState)
advanceFirmwareVehicle totalMass hostInertia coupled rc params servo battery model dt bodyVel bodyRates groundClearance bodyUp state
  | otherwise =
      let -- 1. Firmware mixer: RC → wing servo commands (flap deviation deg).
          -- Real mass frame from the host (CG x, dry pitch/yaw inertia).
          -- Without one (test fixtures), fall back to CG at the origin and
          -- the host roll inertia for pitch/yaw — still the full spatial
          -- solve, never a decoupled legacy fixture.
          (cgX,pitchInertia,yawInertia) = maybe (0,hostInertia,hostInertia) id (fvMassFrame state)
          cg = Vec3 cgX 0 0
          -- Anti-symmetric lift slope ratio (roll/dihedral channel) for this
          -- planform: the factor by which the strip solver's roll lift is
          -- rescaled to use the downwash-free anti-symmetric slope.
          rollRatio = rollLiftSlopeRatio (shapeAspectRatio (wmWing model))
          -- Body orientation is the root coordinate; both shoulders articulate
          -- relative to it. There is no additional wing-plane/fuselage pendulum
          -- coordinate. Body, tail and wings share the spatial mass matrix;
          -- actuator recoil reaches the body through the shoulder coupling.
          -- The obsolete hang state remains zero for ABI compatibility.
          -- Wing rotational inertia about the flap hinge, derived from the
          -- model's actual planform: structural mass + entrained-air added mass
          -- (∝ ρ·chord²). Shared by the servo (rate acceleration) and the
          -- inertial body recoil (Newton's 3rd law).
          wingMass = referenceWingMassKg
          wingInertia = wmFlapInertiaWet model
          originVelocity rates = addVec bodyVel (crossVec (addVec bodyRates (scaleVec (-1) rates)) cg)
          (mix, nextFw) = computeServoMixer coupled rc params (fvFirmware state) dt
          -- Real vertical mount angle: a direct wing incidence (nose-up +) about
          -- the span axis, applied to every strip's geometric twist. Distinct from
          -- the flap-stroke-centre (dihedral) trim that shifts the stroke centre.
          mountIncidence = radians (fwVerticalMountIncidenceDeg params)

          dryWingInertia = wmFlapInertiaDry model
          -- Relative hinge coordinates require full locked inertia on the body
          -- diagonal: E = Ib*p^2/2 + Iw*((l-p)^2+(r+p)^2)/2 (plus rotor energy).
          -- Off-diagonal recoil terms do not replace the wing diagonal terms.
          lockedInertia = hostInertia + 2 * (wingInertia - dryWingInertia)
          currentA = fvMotorCurrentA state
          -- LiPo burst reservoir: the servo may exceed the continuous C-rating
          -- only momentarily; each pause between strokes refills the burst a bit.
          contA = batteryCRating battery * batteryCapacityAh battery
          burstA = max contA (batteryBurstCRating battery * batteryCapacityAh battery)
          overdrawA = max 0 (currentA - contA)
          boostTauDown = 0.08   -- a hard punch empties the burst in ~80 ms
          boostTauUp = 0.30     -- a few inter-stroke pauses refill it
          boostNext = if overdrawA > 0
            then max 0 (fvBatteryBoost state - (overdrawA / max 0.01 (burstA - contA)) * dt / boostTauDown)
            else min 1 (fvBatteryBoost state + dt / boostTauUp)
          liveBattery = battery { batteryStateOfCharge = fvBatterySoc state }
          voltage = batteryVoltageUnderLoadBoost liveBattery currentA (fvBatteryBoost state)
          oldL = fvServoLeft state
          oldR = fvServoRight state
          angleL = radians (servoAngleDeg oldL)
          angleR = radians (servoAngleDeg oldR)
          oldRates = Vec3 (x bodyRates) (radians (servoRateDegPerSec oldL)) (radians (servoRateDegPerSec oldR))
          differenceTarget = radians ((mixRightFlapDevDeg mix-mixLeftFlapDevDeg mix)/2)
          differenceRate = maybe 0 (\previous -> (differenceTarget-previous)/dt) (fvPreviousDifferenceTarget state)
          -- The servo torque must be a CONSISTENT function of the trial flap
          -- rate (implicit coupling): the reference tracker sees the trial
          -- angle at the end of the step, so the Newton solve can converge to
          -- the equilibrium where the wing and the counter-rotating body
          -- track the command together. The old explicit torque (from the
          -- previous servo state) lagged one step and slammed the light
          -- fuselage with stall torque — the runaway roll spin.
          motorTorques l r aeroLoad =
            let atEnd angle old rate=old
                  {servoAngleDeg=(angle+dt*rate)*180/pi,servoRateDegPerSec=rate*180/pi}
                differenceInertia=max 0 (wingInertia-2*wingInertia*wingInertia/lockedInertia)
            in servoPairDriveTorqueWithRate servo battery voltage dt differenceRate
                 (mixLeftFlapDevDeg mix,mixRightFlapDevDeg mix)
                 (y aeroLoad,z aeroLoad)
                 (wingInertia,differenceInertia) (atEnd angleL oldL l,atEnd angleR oldR r) (l,r)
          -- No static weight support: the wings lift the aircraft ONLY through
          -- the air they move (circulation, rotational, LEV, pressure, added
          -- mass). Gravity is carried by the vehicle mass budget in the
          -- spatial solve; the aileron differential rolls the bird through
          -- the wings' own tilted aerodynamic forces.
          (camberBiasL,camberBiasR)=fvWingCamberBias state
          leftSystem = prepareWingSystem (mixIsFlapping mix) (-1) angleL (y oldRates) input
            model mountIncidence camberBiasL (fvLeftStrips state)
          rightSystem = prepareWingSystem (mixIsFlapping mix) 1 angleR (z oldRates) input
            model mountIncidence camberBiasR (fvRightStrips state)
          (vbL,vtL) = wingInitialRates leftSystem
          (vbR,vtR) = wingInitialRates rightSystem
          initial = [x oldRates,y oldRates,z oldRates,vbL,vtL,vbR,vtR,y bodyRates,z bodyRates,0,0,0]
          trial [p,l,r,bL,tL,bR,tR,q,s,dvx,dvy,dvz] =
            let motion = Vec3 p q s
                common = 0.5 * (l + r)
                deltaVelocity = addVec (Vec3 dvx dvy dvz) (crossVec (addVec bodyRates (scaleVec (-1) motion)) cg)
            in (wingTrialMotion leftSystem motion deltaVelocity l common (bL,tL), wingTrialMotion rightSystem motion deltaVelocity r common (bR,tR))
          trial _ = error "coupled coordinate count"
          cgVelocity = addVec bodyVel (crossVec bodyRates cg)
          oldSpatial = components cgVelocity ++ components bodyRates ++ [y oldRates,z oldRates]
          (baseSpatialMass,baseSpatialBias,spatialGravity) = spatialSystem model totalMass cgX
            (hostInertia,pitchInertia,yawInertia) airDensity (angleL,angleR) oldSpatial (scaleVec (-9.81) bodyUp)
          (tailMass,tailBias) = if fixedFan
            then tailFluidInertia cgX airDensity (radians tailIncidence) oldSpatial
            else (replicate 8 (replicate 8 0),replicate 8 0)
          spatialMass=zipWith (zipWith (+)) baseSpatialMass tailMass
          spatialBias=zipWith (+) baseSpatialBias tailBias
          -- Rigid fuselage: the roll row carries the whole bird; the body and
          -- tail see the body's own angular rates (no separate hang DOF).
          wingPlaneMass = spatialMass
          aeroAt candidateBodyRates deltaVelocity (ls,rs) =
            let (ls',rs') = rescaleRollLift rollRatio (ls,rs)
                fuselageRates = candidateBodyRates
                (bodyF,bodyM) = bodyLoadsAt fuselageRates deltaVelocity
                tailPanels = panelsAt fuselageRates deltaVelocity
                tailF = foldr (addVec . fst) zeroVec tailPanels
                tailM = foldr (addVec . snd) zeroVec tailPanels
                wingF = foldr (addVec . resultForce) zeroVec (ls'++rs')
                wingsM = foldr (addVec . resultMoment) zeroVec (ls'++rs')
                fuseM = addVec bodyM tailM
                forceAt = addVec wingF (addVec bodyF tailF)
                torqueAt = Vec3 (sum (zipWith (\l r -> x (resultMoment l) + x (resultMoment r)) ls' rs')
                                + x tailM + x bodyM)
                            (hingeTorqueAt (wsRootHeightM (wmWing model)) (-1) ls') (hingeTorqueAt (wsRootHeightM (wmWing model)) 1 rs')
                jointLoad = addVec torqueAt (Vec3 0 (spatialGravity!!6) (spatialGravity!!7))
            in (jointLoad,forceAt,wingsM,fuseM)
          (aero0,_,_,_) = aeroAt bodyRates zeroVec (trial initial)
          residual coordinates@[p,l,r,bL,tL,bR,tR,q,s,dvx,dvy,dvz] =
            let (ls,rs)=trial coordinates
                (aero,forceAt,wingsM,fuseM)=aeroAt (Vec3 p q s) (Vec3 dvx dvy dvz) (ls,rs)
                -- Rigid roll: the full aero roll moment (wings + body + tail)
                -- acts on the one roll DOF; gravity acts at the CG through
                -- the spatial mass matrix. No pendulum couple is invented.
                cgShift = scaleVec (-1) (crossVec cg forceAt)
                momentCg=Vec3 (x wingsM + x fuseM)
                              (y wingsM + y fuseM + y cgShift)
                              (z wingsM + z fuseM + z cgShift)
                drag load rate=servoBackdriveTorque servo battery voltage load rate
                (driveL,driveR)=motorTorques l r aero
                constrained angle rate err=rate-max ((-radians 80-angle)/dt) (min ((radians 80-angle)/dt) (rate-err))
                (ebL,etL)=wingElasticResidual leftSystem (bL,tL) ls
                (ebR,etR)=wingElasticResidual rightSystem (bR,tR) rs
                spatialDelta = [dvx,dvy,dvz,p-x bodyRates,q-y bodyRates,s-z bodyRates,l-y oldRates,r-z oldRates]
                applied = components forceAt
                  ++ (x momentCg : [y momentCg, z momentCg])
                  ++ [hingeTorqueAt (wsRootHeightM (wmWing model)) (-1) ls+driveL+drag (y aero0) l,hingeTorqueAt (wsRootHeightM (wmWing model)) 1 rs+driveR+drag (z aero0) r]
                net = zipWith (-) (zipWith (+) applied spatialGravity) spatialBias
                spatialImpulse = zipWith (-) (multiply wingPlaneMass spatialDelta) (map (*dt) net)
                e = zipWith (/) spatialImpulse [wingPlaneMass!!i!!i | i<-[0..7]]
            in [e!!3,constrained angleL l (e!!6),constrained angleR r (e!!7),ebL,etL,ebR,etR,e!!4,e!!5,e!!0,e!!1,e!!2]
          residual _ = error "coupled coordinate count"
          solved = solveImplicitSystemCached (fvTangent state) residual initial
          coordinatesNext = case solved of Just (values,_) -> values; Nothing -> replicate 12 (0/0)
          ratesNext = case coordinatesNext of p:l:r:_ -> Vec3 p l r; _ -> error "coupled coordinate count"
          bodyRatesNext = Vec3 (x ratesNext) (coordinatesNext!!7) (coordinatesNext!!8)
          velocityDeltaNext = Vec3 (coordinatesNext!!9) (coordinatesNext!!10) (coordinatesNext!!11)
          makeServo angle rate = ServoState ((angle+dt*rate)*180/pi) (rate*180/pi)
          nextServoL = makeServo angleL (y ratesNext)
          nextServoR = makeServo angleR (z ratesNext)

          -- 2b. ONDAS A-injection: golden-angle phase-envelope strobe at reversal.
          prevPhase = oscPhase (fwOscillator (fvFirmware state))
          limiar = limiarFromFerocities (mixStrokeFerL mix) (mixReturnFerL mix)
          reversal = mixIsFlapping mix
                     && detectReversal prevPhase (mixPhase mix) limiar
          -- Signed servo tracking error (commanded flap deviation vs. actual
          -- servo angle): its magnitude feeds the phase-envelope strobe below,
          -- its signed value feeds the read-only response-phase diagnostic.
          signedTrackErr = mixLeftFlapDevDeg mix - servoAngleDeg nextServoL
          trackErr = abs signedTrackErr
          peNext = if reversal then phaseStrobe trackErr (fvPhaseEnvelope state)
                                else fvPhaseEnvelope state

          -- 2c. Read-only response-phase diagnostic. It estimates the servo's
          -- response lag vs. its flap command but writes NO controller output:
          -- physical resonance is EMERGENT from the wing's elastic spring model
          -- (Structure/Vehicle), never injected as a phase-lock/phase-advance
          -- demand on the oscillator.
          omega = oscCadence (fwOscillator nextFw)
          nextResponsePhase =
            stepResponsePhase (mixLeftFlapDevDeg mix) (servoAngleDeg nextServoL)
                              (oscPhase (fwOscillator nextFw)) omega dt
                              (fvResponsePhase state)

          -- 3. Actual flap deviation → wing physics.
          -- No stick commands enter the wing strip solver: roll and pitch
          -- emerge implicitly from the dihedral differential and the tail
          -- incidence the servos actually reach.
          input = VehicleInput dt bodyVel bodyRates 0 0 0 0 groundClearance bodyUp
          (leftRaw,rightRaw) = trial coordinatesNext
          (leftResults,leftNext) = wingCommit leftSystem leftRaw
          (rightResults,rightNext) = wingCommit rightSystem rightRaw
          wingResults = leftResults ++ rightResults

          -- 4. Generalized torque conjugate to each wing's flap coordinate.
          leftHinge = hingeTorqueAt (wsRootHeightM (wmWing model)) (-1) leftResults
          rightHinge = hingeTorqueAt (wsRootHeightM (wmWing model)) 1 rightResults

          -- 5. Assemble full-vehicle forces (wings + body drag + tail).
          --    Every control moment is a force acting at a physical lever arm.
          --    Tail flow includes body angular velocity, providing aerodynamic
          --    pitch/yaw damping instead of an artificial attitude torque.
          rudderNorm = (mixRudderUs mix - 1500) / 500
          wingForce = foldr (addVec . resultForce) zeroVec wingResults
          -- Body parasitic drag area (CdA). The reference kestrel body is a
          -- 0.42 m × 0.03 m × 0.03 m streamlined fuselage: frontal area ~9e-4 m²
          -- at Cd ≈ 0.44 gives CdA ≈ 4e-4 m². The old 0.011 m² (~27× too high)
          -- made the body alone eat a third of the bird's weight at 7 m/s.
          -- Fuselage aft-body = lifting body + vertical stabiliser: the part
          -- of the body BEHIND the CG is a rigid, low-aspect-ratio lifting
          -- surface (see 'bodyLiftPolar'). Two fixed panels share the aft
          -- centre of pressure ('fuselageAftArm') and the SAME rigid-panel
          -- polar as the tail ('panelSurface') — NOT the wing's flapping /
          -- aeroelastic strip solver, because the body geometry is fixed (no
          -- membrane camber, no spar, no LEV). Side panel (normal ±y) = lift
          -- in YAW plus the crossflow pressure drag that weathervanes the nose
          -- into the wind. Top panel (normal +z) = lift in PITCH; its aft CP
          -- yields a nose-down (restoring) moment, so it is pitch-stabilising.
          --
          -- FUTURE (annotated, not modelled): wing-induced flow blockage — the
          -- wings shadow the body/tail at some bank and flap-phase angles, so
          -- the body's dynamic pressure should scale by a direction-dependent
          -- shadow factor (a function of the wing strips' instantaneous
          -- silhouette against the body). That silhouette is not tracked here;
          -- until then the body sees the freestream everywhere, a small
          -- over-estimate of body drag/lift when the wing is in front.
          bodyLoadsAt rates deltaVelocity =
            let velocity = addVec deltaVelocity (originVelocity rates)
                drag = scaleVec (-0.5*airDensity*0.0004*magnitude velocity) velocity
                flow = addVec velocity (crossVec rates fuselageAftArm)
                sidePolar = if fixedFan then kestrelSidePolar else bodyLiftPolar
                (sx,sy) = panelSurface sidePolar fuselageAftArea 0 (x flow) (y flow)
                (tx,tz) = panelSurface bodyLiftPolar fuselageAftArea 0 (x flow) (z flow)
                panelsForce = Vec3 (sx+tx) sy tz
            in (addVec drag panelsForce,crossVec fuselageAftArm panelsForce)
          -- Legacy non-Kestrel inverted-V approximation. Each panel sees
          -- its own local flow and produces force at its own arm; the resulting
          -- pitch/yaw coupling is geometric, not an added torque. The cant sets
          -- the yaw/pitch split: a steeper V (32°) trades some pitch authority
          -- for the vertical tail area that damps yaw — the nose's side-to-side
          -- sway is resisted by the tail's lateral lift and crossflow. Its
          -- offset force also contributes roll and can oppose wing roll.
          cant = radians 32
          -- FIXED-TAIL DOCTRINE: the tail is a passive stabiliser rigid with
          -- the fuselage — its incidence is the mounting trim ONLY. The
          -- PITCH stick never actuates the tail: it shifts the WINGS' common
          -- flap-stroke centre (dihedral/anhedral, see Firmware.hs), and the
          -- pitch moment emerges implicitly from the wing forces at their
          -- real lever arms.
          tailMount = profTailElevatorAngleDeg (fwProfile params)
          fixedFan = wmName model `elem` ["kestrel", "kestrel-light"]
          tailIncidence = tailMount
          tailPanel panelBodyRates deltaVelocity side =
            let -- Legacy tail geometry retains its fixed arm and panel
                -- area (0.85 m arm, 0.0242 m² panel); it is part of the bird,
                -- not scaled per planform.
                arm = Vec3 (-0.85) (side * 0.12) (-0.12 * tan cant)
                normal = Vec3 0 (side * sin cant) (cos cant)
                flow = addVec deltaVelocity (addVec (originVelocity panelBodyRates) (crossVec panelBodyRates arm))
                normalFlow = y flow * y normal + z flow * z normal
                -- Ruddervator deflection is a rotation about the spanwise hinge,
                -- i.e. a *direct* change of panel incidence (no sin/cos here).
                -- The projection onto body yaw/pitch axes is already done by the
                -- panel normal below; folding sin(cant)/cos(cant) into the
                -- incidence double-projects and throttles the rudder by sin²(cant).
                incidence = radians (tailIncidence
                                      - side * 30 * rudderNorm)
                (fx, fn) = tailSurface 0.0242 incidence (x flow) normalFlow
                panelForce = addVec (Vec3 fx 0 0) (scaleVec fn normal)
            in (panelForce, crossVec arm panelForce)
          fanAt rates deltaVelocity = kestrelTailLoads tailIncidence (addVec deltaVelocity (originVelocity rates)) rates
          panelsAt rates deltaVelocity = if fixedFan
            then [(tailFieldForce f,tfMoment f) | f <- fanAt rates deltaVelocity]
            else map (tailPanel rates deltaVelocity) [-1,1]
          -- Include the tail in the same implicit roll solve as the wings.
          -- Holding its damping at the old rate can reverse roll too far when
          -- the small fuselage inertia is exposed during a wing transition.
          panels = panelsAt bodyRatesNext velocityDeltaNext
          tailForce = foldr (addVec . fst) zeroVec panels
          (bodyForce,_) = bodyLoadsAt bodyRatesNext velocityDeltaNext
          externalForce = addVec wingForce (addVec bodyForce tailForce)
          -- The spatial solve carries the full mass matrix (dry + added air)
          -- and true gravity. The force handed to the host is the solved
          -- inertial acceleration of the TOTAL mass plus its weight: the host
          -- integrates its dry mass and receives the complete exchange — no
          -- separate support/recoil correction exists any more.
          force = addVec (scaleVec (totalMass/dt) velocityDeltaNext) (scaleVec (totalMass*9.81) bodyUp)
          -- Equivalent dry-host loads reproduce the solved spatial impulse.
          -- Fluid inertia, joint recoil and gyroscopic terms are already in
          -- that solve. Preserve the ABI's origin-to-CG shift convention by
          -- adding cg cross surfaceForce here; the host removes it once.
          -- The ROLL component drives the rigid wing plane + fuselage; the
          -- roll impulse is the solved spatial momentum — never added twice.
          -- hostInertia is already about the host CG, exactly as in the
          -- spatial system. A shoulder-height parallel-axis addition here
          -- amplifies the returned acceleration and breaks impulse matching.
          moment = addVec (Vec3 (hostInertia*(x bodyRatesNext-x bodyRates)/dt)
                                 (pitchInertia*(y bodyRatesNext-y bodyRates)/dt)
                                 (yawInertia*(z bodyRatesNext-z bodyRates)/dt)) (crossVec cg externalForce)

          separations = map (stripSeparation . resultState) wingResults
          maximumSeparation = maximum (0 : separations)
          power = sum (map resultPower wingResults)

          -- Charge the actual motor torque, including acceleration and reversal,
          -- rather than only the aerodynamic hinge load. The 5 A stall-current
          -- assumption remains provisional until measured servo data is supplied.
          (aeroNext,_,auditWingMoment,auditBodyMoment) = aeroAt bodyRatesNext velocityDeltaNext (leftResults,rightResults)
          auditDelta = components velocityDeltaNext ++ components (addVec bodyRatesNext (scaleVec (-1) bodyRates))
                       ++ [y ratesNext-y oldRates,z ratesNext-z oldRates]
          auditParts = zipWith (*) (spatialMass!!3) (map (/dt) auditDelta)
          (torqueL,torqueR) = motorTorques (y ratesNext) (z ratesNext) aeroNext
          motorPower = max 0 (torqueL*y ratesNext) + max 0 (torqueR*z ratesNext)
          torqueCurrent = (abs torqueL + abs torqueR) / max 0.01 (servoNominalTorqueNm servo / 5)
          motorCurrent = if voltage <= 0 then 0
                         else max torqueCurrent (motorPower / (0.70 * voltage))
          socDrop = motorCurrent * dt / (3600 * max 0.01 (batteryCapacityAh battery))
          nextSoc = max 0 (fvBatterySoc state - socDrop)

          nextState = state
            { fvTangent = case solved of Just (_,tangent) -> tangent; Nothing -> Nothing
            , fvFirmware = nextFw
            , fvServoLeft = nextServoL
            , fvServoRight = nextServoR
            , fvLeftStrips = leftNext
            , fvRightStrips = rightNext
            , fvLeftStripForceMag = map (magnitude . resultForce) leftResults
            , fvRightStripForceMag = map (magnitude . resultForce) rightResults
            , fvLeftHingeTorqueNm = leftHinge
            , fvRightHingeTorqueNm = rightHinge
            , fvMotorCurrentA = motorCurrent
            , fvBatterySoc = nextSoc
            , fvBatteryBoost = boostNext
            , fvPhaseEnvelope = peNext
            , fvResponsePhase = nextResponsePhase
            , fvFlapHz = mixFlapHz mix
            , fvTailIncidenceDeg = tailIncidence
            , fvTailFields = if fixedFan then fanAt bodyRatesNext velocityDeltaNext else []
            , fvExternalForce = externalForce
            , fvSolvedBodyRates = bodyRatesNext
            , fvDriveTorques = (torqueL,torqueR)
            , fvRollBalance = [x auditWingMoment+x auditBodyMoment,spatialGravity!!3,spatialBias!!3,
                               auditParts!!3,auditParts!!6+auditParts!!7,
                               sum [auditParts!!i | i<-[0,1,2,4,5]],torqueL,torqueR]
            , fvPreviousDifferenceTarget = Just differenceTarget
            , fvHangDelta = 0
            , fvHangDeltaRate = 0
            }

          output = VehicleOutput force moment power maximumSeparation (-1) 0
      in (output, nextState)

zeroOutput :: Int -> VehicleOutput
zeroOutput flags = VehicleOutput zeroVec zeroVec 0 0 (-1) flags

-- | Coefficients of a rigid lifting-panel polar. This is the SAME abstraction
-- the wing's chord strips reduce to when the geometry is fixed and aeroelastic
-- state is frozen: a surface with a lift curve (perpendicular to local flow)
-- and a drag polar (opposing it), both charged at the panel's own lever arm so
-- every moment is a real force × arm — never an added torque. The wing's
-- flapping/aeroelastic 'stepStrip' is the general case (16 strips + membrane
-- camber + LEV/rotational lift); a rigid tail or body panel is the degenerate
-- case (one strip, frozen shape) and only needs this slender-body polar.
data PanelPolar = PanelPolar
  { ppClSlope     :: Double -- ^ sin(2α) lift-curve amplitude (≈1.1 flat plate, ≈0.45 round body)
  , ppCdProfile   :: Double -- ^ profile / skin-friction drag
  , ppCdInducedK  :: Double -- ^ drag-due-to-lift factor (multiplies cl²)
  , ppCdPressureK :: Double -- ^ crossflow pressure-drag factor (multiplies sin²α)
  }

-- | Rigid lifting-panel force. Returns @(chordForce, normalForce)@ for a panel
-- of area @area@ at incidence @incidence@ seeing chordwise flow @chordVelocity@
-- and normal flow @normalVelocity@. cl = clSlope·sin(2α); cd = profile +
-- induced·cl² + pressure·sin²α. The pressure term is REAL drag — charged as
-- drag (opposing flow), never folded into the lift, so a passive surface
-- cannot create energy.
panelSurface :: PanelPolar -> Double -> Double -> Double -> Double -> (Double, Double)
panelSurface polar area incidence chordVelocity normalVelocity =
  let speed = sqrt (chordVelocity * chordVelocity + normalVelocity * normalVelocity)
      alpha = incidence + atan2 (-normalVelocity) chordVelocity
      cl = ppClSlope polar * sin (2 * alpha)
      cd = ppCdProfile polar + ppCdInducedK polar * cl * cl + ppCdPressureK polar * sin alpha * sin alpha
      q = 0.5 * airDensity * area * speed * speed
      u = chordVelocity / max 1.0e-9 speed
      w = normalVelocity / max 1.0e-9 speed
  in (q * (-cd * u - cl * w), q * (-cd * w + cl * u))

tailSurface :: Double -> Double -> Double -> Double -> (Double, Double)
tailSurface area = tailSurfaceForAspect (0.48*0.48/(2*area)) area

tailSurfaceForAspect :: Double -> Double -> Double -> Double -> Double -> (Double, Double)
tailSurfaceForAspect aspect area incidence chordVelocity normalVelocity =
  let speed = sqrt (chordVelocity*chordVelocity + normalVelocity*normalVelocity)
      alpha = atan2 (sin (incidence + atan2 (-normalVelocity) chordVelocity))
                    (cos (incidence + atan2 (-normalVelocity) chordVelocity))
      -- Static separation of a rigid panel. The wing additionally carries
      -- unsteady separation, elastic twist and camber states.
      t = max 0 (min 1 ((abs alpha-radians 13)/radians 7))
      separation = t*t*(3-2*t)
      -- Use the whole tail's aspect ratio, not the area of one quadrature
      -- patch, just as the wing uses its full span/area.
      attached = max (-1.9) (min 1.9 (finiteWingLiftSlope aspect*alpha))
      cl = (1-separation)*attached
      cd = surfaceSkinCd + inducedDragCoefficient aspect cl
      q = 0.5*airDensity*area*speed*speed
      u = chordVelocity/max 1e-9 speed
      w = normalVelocity/max 1e-9 speed
      nx = -sin incidence
      nz = cos incidence
      pressure = surfacePressure area (nx*chordVelocity+nz*normalVelocity)
  in (q*(-cd*u-cl*w)+pressure*nx, q*(-cd*w+cl*u)+pressure*nz)

-- Fixed Kestrel fan: same polygon, root and triangulation as the visible mesh.
-- Incidence is a mounting angle, not a pitch/throttle/yaw actuator. Body roll
-- rotates its negative lift into a lateral force in world space; no ruddervator
-- or additional yaw/roll torque is injected.
data TailField = TailField
  { tfPosition :: !Vec3, tfNormal :: !Vec3, tfVelocity :: !Vec3
  , tfLift :: !Vec3, tfPressure :: !Vec3, tfDrag :: !Vec3, tfMoment :: !Vec3
  } deriving (Eq, Show)

tailFieldForce :: TailField -> Vec3
tailFieldForce f = addVec (tfLift f) (addVec (tfPressure f) (tfDrag f))

kestrelTailLoads :: Double -> Vec3 -> Vec3 -> [TailField]
kestrelTailLoads incidenceDeg velocity rates = map at tailSamples
  where
    incidence = radians incidenceDeg
    ci = cos incidence; si = sin incidence
    normal = Vec3 (-si) 0 ci
    area = sum [a | (a,_,_) <- tailSamples]
    point px py = Vec3 (tailRootX+ci*(px-tailRootX)) py (si*(px-tailRootX))
    outline = [point px py | (px,py)<-tailOutline]
    dot (Vec3 a b c) (Vec3 d e f) = a*d+b*e+c*f
    at (patchArea,px,py) =
      let position = point px py
          flow = addVec velocity (crossVec rates position)
          -- Broadside pressure acts at the area quadrature point. Attached
          -- circulation acts at the quarter chord of the local streamwise
          -- slice, not at that area's centroid. Locate that point on the real
          -- polygon, including sideways/reverse flow, and sample its velocity.
          pressureNormalSpeed = dot flow normal
          sliceFlow = addVec flow (scaleVec (-pressureNormalSpeed) normal)
          sliceSpeed = magnitude sliceFlow
          sliceTangent = if sliceSpeed>1e-9 then scaleVec (1/sliceSpeed) sliceFlow else Vec3 ci 0 si
          sliceSpan = crossVec normal sliceTangent
          sliceY = dot position sliceSpan
          cuts = [dot a sliceTangent + f*dot (addVec b (scaleVec (-1) a)) sliceTangent
                 | (a,b)<-zip outline (drop 1 outline++take 1 outline),
                   let ya=dot a sliceSpan, let dy=dot b sliceSpan-ya,
                   abs dy>1e-12, let f=(sliceY-ya)/dy, f>=0, f<=1]
          acX = if null cuts then dot position sliceTangent else 0.75*maximum cuts+0.25*minimum cuts
          liftPosition = addVec position (scaleVec (acX-dot position sliceTangent) sliceTangent)
          liftFlow = addVec velocity (crossVec rates liftPosition)
          -- A finite fan is a lifting surface in ANY in-plane flow direction.
          -- Dropping body-Y velocity spuriously stalls/unloads it in a slip.
          -- Resolve flow against the physical surface, not the body XZ plane.
          normalSpeed = dot liftFlow normal
          tangentFlow = addVec liftFlow (scaleVec (-normalSpeed) normal)
          tangentSpeed = magnitude tangentFlow
          tangent = if tangentSpeed>1e-9 then scaleVec (1/tangentSpeed) tangentFlow else Vec3 ci 0 si
          speed = magnitude liftFlow
          alpha = atan2 (-normalSpeed) tangentSpeed
          -- Project the real polygon perpendicular to the in-plane flow.
          -- At zero sideslip this is the original span^2/area; at 90 degrees
          -- the finite fan's length becomes its aerodynamic span.
          spanDirection = crossVec normal tangent
          spans = map (dot spanDirection) outline
          width = maximum spans-minimum spans
          aspect = width*width/area
          t = max 0 (min 1 ((abs alpha-radians 13)/radians 7))
          separation = t*t*(3-2*t)
          -- The compact fan has low aspect ratio in either flow direction.
          -- Use Helmbold's lifting-surface denominator, 2+sqrt(4+AR^2),
          -- instead of the higher-AR lifting-line approximation AR+2.
          -- Preserve the existing section efficiency, without fitting a gain
          -- to the desired handling. Compare against the independent AVL fan.
          fanSlope = finiteWingLiftSlope aspect * (aspect+2)/(2+sqrt(4+aspect*aspect))
          cl = (1-separation)*max (-1.9) (min 1.9 (fanSlope*alpha))
          q = 0.5*airDensity*patchArea*speed*speed
          lift = scaleVec (q*cl/max 1e-9 speed)
                   (addVec (scaleVec tangentSpeed normal) (scaleVec (-normalSpeed) tangent))
          pressure = scaleVec (surfacePressure patchArea pressureNormalSpeed) normal
          induced = scaleVec (-0.5*airDensity*patchArea*speed*inducedDragCoefficient aspect cl) liftFlow
          skin = scaleVec (-0.5*airDensity*patchArea*magnitude flow*surfaceSkinCd) flow
          drag = addVec induced skin
          -- Preserve the ABI's area reference point and transmit the physical
          -- couple from the displaced circulation force. Moment is no longer
          -- just r_area cross totalForce. Wrench power includes this couple.
          moment = addVec (crossVec position (addVec pressure skin))
                          (crossVec liftPosition (addVec lift induced))
      in TailField position normal flow lift pressure drag moment

-- | Round lifting-body polar.
-- lift than a flat plate (its sides/top are curved, so the Kutta condition is
-- soft) but it still weathervanes through the crossflow pressure term. Lift
-- slope ≈0.45 is about half the flat plate, matching slender-body theory.
bodyLiftPolar :: PanelPolar
bodyLiftPolar = PanelPolar 0.45 0.034 0.10 2.5

-- The aft-fuselage side panel (fixed geometry behind the CG). Its side force
-- is kept small for the same reason as the fan's: the body must not act like
-- a fin. Its force at fuselageAftArm gives a mild yaw-rate damping through
-- the crossflow pressure term without steering the nose into every sideslip.
kestrelSidePolar :: PanelPolar
kestrelSidePolar = bodyLiftPolar { ppClSlope = 0.15, ppCdPressureK = 1.20 }


-- | Centre of pressure of the fuselage aft-body + tail boom, BEHIND the CG.
-- Everything aft acts like a wing fence / vertical stabiliser; this lever arm
-- turns both the lift and the pressure drag above into a weathervaning yaw
-- moment and a nose-down (restoring) pitch moment.
fuselageAftArm :: Vec3
fuselageAftArm = Vec3 (-0.28) 0 0

-- | Aft-body side/top projected area [m²] used as the lifting-body reference
-- (≈0.21 m behind the CG × 0.03 m body height/width; the cross-section is
-- ~square so side and top are equal).
fuselageAftArea :: Double
fuselageAftArea = 0.0063