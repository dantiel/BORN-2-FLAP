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
  , transportAirfields
  ) where

import Born2Flap.Math.LiftField (PressureField(..), transportVector)
import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Firmware
import Born2Flap.Math.Servo
import Born2Flap.Math.RollDynamics (solveImplicitSystemCached, supportedRollSystem)
import Born2Flap.Math.Model (WingModel(..))
import Born2Flap.Math.Planform
  ( shapeFlapInertia, referenceWingMassKg, shapeChord, WingShape(..) )
import Born2Flap.Math.Simulation
import Born2Flap.Math.Waveform (OscillatorState(..), limiarFromFerocities)
import Born2Flap.Math.PhaseEnvelope
import Born2Flap.Math.ResponsePhase
import Born2Flap.Math.Vehicle
  ( VehicleInput(..), VehicleOutput(..)
  , StripState(..), StripResult(..), initialStrips, stripCount
  , WingSystem(..), prepareWingSystem, coupleWingPressure, wingRootHeight, airDensity, surfaceSkinCd, surfacePressure
  , finiteWingLiftSlope, inducedDragCoefficient
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
  }

-- Reconcile the solver's predicted pose with an authoritative host pose.
-- Field positions are anchors in air, so both their direction and their
-- offset from the wing must change basis together.
transportAirfields :: Vec3 -> FirmwareVehicleState -> FirmwareVehicleState
transportAirfields rotation state =
  let rotate=transportVector (-1) rotation
      update s=let patch=stripPressureField s
                   correction=addVec (stripPosition s) (scaleVec (-1) (rotate(stripPosition s)))
               in s {stripPressureField=patch
                 {fieldDisplacement=addVec (rotate(fieldDisplacement patch)) correction
                 ,fieldDirection=rotate(fieldDirection patch)}}
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
          run = stepFirmwareVehicleWithMassInertia totalMass hostInertia coupled rc params servo battery model half bodyVel
          (a, middle) = run bodyRates groundClearance bodyUp state
          middleRates = Vec3 (x bodyRates + half*x (totalMomentNm a)/hostInertia) (y bodyRates) (z bodyRates)
          (b, final) = run middleRates groundClearance bodyUp middle
          average u v = scaleVec 0.5 (addVec u v)
      in if outputFlags a /= 0 || outputFlags b /= 0 then (zeroOutput flag,state)
         else (b { totalForceN = average (totalForceN a) (totalForceN b)
                 , totalMomentNm = average (totalMomentNm a) (totalMomentNm b)
                 , totalMechanicalPowerW = 0.5*(totalMechanicalPowerW a+totalMechanicalPowerW b)
                 , maxSeparation = max (maxSeparation a) (maxSeparation b) }, final)
    transaction = do
      old <- getState
      let (output, next) = advanceFirmwareVehicle totalMass hostInertia coupled rc params servo battery model dt bodyVel bodyRates groundClearance bodyUp old
          values = components (totalForceN output) ++ components (totalMomentNm output) ++
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
                    ++ components (stripAirVelocity s) ++ components (stripWindDelta s))
              (fvLeftStrips next ++ fvRightStrips next)
      if all finite values then putState next >> pure output else abort 2

finite :: Double -> Bool
finite value = not (isNaN value || isInfinite value)

components :: Vec3 -> [Double]
components (Vec3 a b c) = [a,b,c]

-- Flat-plate added-air inertia. This is an inertial term, never weight
-- support or a multiplier selected by a flight mode.
addedAirGain :: Double
addedAirGain = 1.0

-- Both positive stroke coordinates raise the wing. The left hinge axis is -X.
-- Elastic deformation changes the surface geometry. It cannot discount the
-- external moment transmitted to the hinge; a dynamic spring would need its
-- own inertia and stored-energy state to account for that exchange.
hingeTorque :: Double -> [StripResult] -> Double
hingeTorque side = (* side) . sum . map (\r -> (x (resultMoment r) + wingRootHeight * y (resultForce r)))


advanceFirmwareVehicle
  :: Double -> Double -> Bool -> RcChannels -> FirmwareParams -> ServoSpec -> BatterySpec -> WingModel
  -> Double -> Vec3 -> Vec3 -> Double -> Vec3 -> FirmwareVehicleState -> (VehicleOutput, FirmwareVehicleState)
advanceFirmwareVehicle totalMass hostInertia coupled rc params servo battery model dt bodyVel bodyRates groundClearance bodyUp state
  | otherwise =
      let -- 1. Firmware mixer: RC → wing servo commands (flap deviation deg).
          (mix, nextFw) = computeServoMixer coupled rc params (fvFirmware state) dt
          -- Real vertical mount angle: a direct wing incidence (nose-up +) about
          -- the span axis, applied to every strip's geometric twist. Distinct from
          -- the flap-stroke-centre (dihedral) trim that shifts the stroke centre.
          mountIncidence = radians (fwVerticalMountIncidenceDeg params)
          -- Wing rotational inertia about the flap hinge, derived from the
          -- model's actual planform: structural mass + entrained-air added mass
          -- (∝ ρ·chord²). Shared by the servo (rate acceleration) and the
          -- inertial body recoil (Newton's 3rd law).
          wingMass = referenceWingMassKg
          wingInertia = shapeFlapInertia wingMass (airDensity * addedAirGain) (wmWing model)
     
          dryWingInertia = shapeFlapInertia wingMass 0 (wmWing model)
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
          -- The servo torque must be a CONSISTENT function of the trial flap
          -- rate (implicit coupling): the reference tracker sees the trial
          -- angle at the end of the step, so the Newton solve can converge to
          -- the equilibrium where the glued wing and the counter-rotating
          -- body track the command together. The old explicit torque (from
          -- the previous servo state) lagged one step and slammed the light
          -- fuselage with stall torque — the runaway roll spin.
          motorTorques l r aeroLoad =
            let atEnd angle old rate=old
                  {servoAngleDeg=(angle+dt*rate)*180/pi,servoRateDegPerSec=rate*180/pi}
                differenceInertia=max 0 (wingInertia-2*wingInertia*wingInertia/lockedInertia)
            in servoPairDriveTorque servo battery voltage dt
                 (mixLeftFlapDevDeg mix,mixRightFlapDevDeg mix)
                 (y aeroLoad,z aeroLoad)
                 (wingInertia,differenceInertia) (atEnd angleL oldL l,atEnd angleR oldR r) (l,r)
          -- Compatibility arguments for the legacy pure-wing API. No load
          -- depends on commanded angle, aircraft weight, or a flap/glide flag.
          centreStroke = 0.5 * (angleL + angleR)
          supportShares = replicate stripCount 0
          leftSystem = prepareWingSystem (mixIsFlapping mix) (-1) angleL (y oldRates) input model mountIncidence (fvLeftStrips state) supportShares centreStroke 0
          rightSystem = prepareWingSystem (mixIsFlapping mix) 1 angleR (z oldRates) input model mountIncidence (fvRightStrips state) supportShares centreStroke 0
          (vbL,vtL) = wingInitialRates leftSystem
          (vbR,vtR) = wingInitialRates rightSystem
          initial = [x oldRates,y oldRates,z oldRates,vbL,vtL,vbR,vtR]
          trial [p,l,r,bL,tL,bR,tR] =
            coupleWingPressure (input {bodyRatesRadS=Vec3 p (y bodyRates) (z bodyRates)}) model
              (fvLeftStrips state) (fvRightStrips state)
              (wingTrialLoads leftSystem p l (bL,tL)) (wingTrialLoads rightSystem p r (bR,tR))
          trial _ = error "coupled coordinate count"
          -- Dry-wing first mass moment from the same planform as hinge inertia.
          wp = wmWing model
          massSamples = [(wsShoulderM wp+wsSpanM wp*f,shapeChord wp f)
                        | i<-[0..127::Int],let f=(fromIntegral i+0.5)/128]
          firstMoment = wingMass * sum [r*c | (r,c)<-massSamples] / sum (map snd massSamples)
          -- Translational added air (rho*pi*c^2/4 per unit span) and its first
          -- moment about the body centreline (c^2 weighting pushes the centroid
          -- outboard where the chord is deeper). This is the fluid inertia the
          -- wing must accelerate when it translates normally through the air;
          -- rotational added air is already inside wingInertia/lockedInertia.
          dfrac = 1/128
          addedMass = airDensity * addedAirGain * (pi/4) * (wsSpanM wp) * sum [c*c*dfrac | (_,c)<-massSamples]
          addedFirstMoment = airDensity * addedAirGain * (pi/4) * (wsSpanM wp) * sum [r*c*c*dfrac | (r,c)<-massSamples]
          (supportMass,supportLoad,supportRecoil,massRatio) = supportedRollSystem totalMass addedMass lockedInertia
            dryWingInertia wingInertia reflectedDriveInertia firstMoment addedFirstMoment wingRootHeight (angleL,angleR) oldRates
          aeroAt rates (ls,rs) =
            let candidateBodyRates = Vec3 (x rates) (y bodyRates) (z bodyRates)
                tailPanels = panelsAt candidateBodyRates
                tailF = foldr (addVec . fst) zeroVec tailPanels
                tailM = foldr (addVec . snd) zeroVec tailPanels
                forceAt = foldr (addVec . resultForce)
                  (addVec bodyDrag (addVec bodySideForce (addVec bodyTopForce tailF))) (ls++rs)
                torqueAt = Vec3 (sum (zipWith (\l r -> x (resultMoment l) + x (resultMoment r)) ls rs)
                                + x tailM + x bodySideMoment + x bodyTopMoment)
                            (hingeTorque (-1) ls) (hingeTorque 1 rs)
            in (addVec torqueAt (supportLoad forceAt),forceAt)
          aero0 = fst (aeroAt oldRates (trial initial))
          residual coordinates@[p,l,r,bL,tL,bR,tR] =
            let rates=Vec3 p l r
                (ls,rs)=trial coordinates
                aero=fst(aeroAt rates (ls,rs))
                drag load rate=servoBackdriveTorque servo battery voltage load rate
                (driveL,driveR)=motorTorques l r aero
                load=addVec aero (Vec3 0 (driveL+drag (y aero0) l) (driveR+drag (z aero0) r))
                delta=addVec rates (scaleVec (-1) oldRates)
                (row0,row1,row2)=supportMass
                dot a b=x a*x b+y a*y b+z a*z b
                impulse=addVec (Vec3 (dot row0 delta) (dot row1 delta) (dot row2 delta)) (scaleVec (-dt) load)
                constrained angle rate err=rate-max ((-radians 80-angle)/dt) (min ((radians 80-angle)/dt) (rate-err))
                (ebL,etL)=wingElasticResidual leftSystem (bL,tL) ls
                (ebR,etR)=wingElasticResidual rightSystem (bR,tR) rs
            in [x impulse/x row0,constrained angleL l (y impulse/y row1),constrained angleR r (z impulse/z row2),ebL,etL,ebR,etR]
          residual _ = error "coupled coordinate count"
          solved = solveImplicitSystemCached (fvTangent state) residual initial
          coordinatesNext = case solved of Just (values,_) -> values; Nothing -> replicate 7 (0/0)
          ratesNext = case coordinatesNext of p:l:r:_ -> Vec3 p l r; _ -> error "coupled coordinate count"
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
          input = VehicleInput dt bodyVel bodyRates 0 (crsfToNorm (rcAileron rc)) elevatorNorm 0 groundClearance bodyUp
          (leftRaw,rightRaw) = trial coordinatesNext
          (leftResults,leftNext) = wingCommit leftSystem leftRaw
          (rightResults,rightNext) = wingCommit rightSystem rightRaw
          wingResults = leftResults ++ rightResults

          -- 4. Generalized torque conjugate to each wing's flap coordinate.
          leftHinge = hingeTorque (-1) leftResults
          rightHinge = hingeTorque 1 rightResults

          -- 5. Assemble full-vehicle forces (wings + body drag + tail).
          --    Every control moment is a force acting at a physical lever arm.
          --    Tail flow includes body angular velocity, providing aerodynamic
          --    pitch/yaw damping instead of an artificial attitude torque.
          elevatorNorm = crsfToNorm (rcElevator rc)
          rudderNorm = (mixRudderUs mix - 1500) / 500
          wingForce = foldr (addVec . resultForce) zeroVec wingResults
          wingMoment = foldr (addVec . resultMoment) zeroVec wingResults
          speed = magnitude bodyVel
          -- Body parasitic drag area (CdA). The reference kestrel body is a
          -- 0.42 m × 0.03 m × 0.03 m streamlined fuselage: frontal area ~9e-4 m²
          -- at Cd ≈ 0.44 gives CdA ≈ 4e-4 m². The old 0.011 m² (~27× too high)
          -- made the body alone eat a third of the bird's weight at 7 m/s.
          bodyDrag = scaleVec (-0.5 * airDensity * 0.0004 * speed) bodyVel
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
          bodySideFlow = addVec bodyVel (crossVec bodyRates fuselageAftArm)
          (bodySideChord, bodySideNormal) = panelSurface bodyLiftPolar fuselageAftArea 0 (x bodySideFlow) (y bodySideFlow)
          bodySideForce = addVec (Vec3 bodySideChord 0 0) (Vec3 0 bodySideNormal 0)
          bodySideMoment = crossVec fuselageAftArm bodySideForce
          bodyTopFlow = addVec bodyVel (crossVec bodyRates fuselageAftArm)
          (bodyTopChord, bodyTopNormal) = panelSurface bodyLiftPolar fuselageAftArea 0 (x bodyTopFlow) (z bodyTopFlow)
          bodyTopForce = addVec (Vec3 bodyTopChord 0 0) (Vec3 0 0 bodyTopNormal)
          bodyTopMoment = crossVec fuselageAftArm bodyTopForce
          -- Two real inverted-V panels with mixed ruddervators. Each panel sees
          -- its own local flow and produces force at its own arm; the resulting
          -- pitch/yaw coupling is geometric, not an added torque. The cant sets
          -- the yaw/pitch split: a steeper V (32°) trades some pitch authority
          -- for the vertical tail area that damps yaw — the nose's side-to-side
          -- Dutch-roll sway is killed by the tail's lateral lift + crossflow,
          -- never by the wings. Roll stays the wings' job (differential-dihedral
          -- lift tilt via the flap-centre deviation in the mixer).
          cant = radians 32
          -- A direct flap-power/ruddervator mix balances the wing drive's changing
          -- pitching moment. Glide returns to its shallow incidence; no attitude,
          -- altitude or speed target is involved.
          -- Re-trim for the shared finite-wing lift slope. Keeping the old
          -- incidence after correcting the tail's weak lift slope doubled
          -- the nose-up trim moment and drove repeated climb/stall cycles.
          tailTrim = -2.3 - 1.1 * mixThrottlePct mix * mixThrottlePct mix
          tailIncidence = tailTrim - 18 * elevatorNorm
          tailPanel panelBodyRates side =
            let -- Tail geometry is the reference kestrel's fixed arm and panel
                -- area (0.85 m arm, 0.0242 m² panel); it is part of the bird,
                -- not scaled per planform.
                arm = Vec3 (-0.85) (side * 0.12) (-0.12 * tan cant)
                normal = Vec3 0 (side * sin cant) (cos cant)
                flow = addVec bodyVel (crossVec panelBodyRates arm)
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
          panelsAt rates = map (tailPanel rates) [-1,1]
          -- Include the tail in the same implicit roll solve as the wings.
          -- Holding its damping at the old rate can reverse roll too far when
          -- the small fuselage inertia is exposed during a wing transition.
          panels = panelsAt (Vec3 (x ratesNext) (y bodyRates) (z bodyRates))
          tailForce = foldr (addVec . fst) zeroVec panels
          tailMoment = foldr (addVec . snd) zeroVec panels
          externalForce = addVec wingForce (addVec bodyDrag (addVec bodySideForce (addVec bodyTopForce tailForce)))
          acceleration = scaleVec (1/dt) (addVec ratesNext (scaleVec (-1) oldRates))
          recoilForce = supportRecoil acceleration
          -- The host integrates translation with its own dry mass, which does
          -- not include translational added air. Scale the lateral/heave force
          -- by massRatio and add the added-air inertial recoil so the host's
          -- dry-mass integration reproduces the correct fluid-inertia response.
          -- Forward (streamwise) force is unaffected: the wing is edge-on in X.
          force = Vec3 (x externalForce)
                       (massRatio * y externalForce + y recoilForce)
                       (massRatio * z externalForce + z recoilForce)
          aeroMoment = addVec wingMoment (addVec bodySideMoment (addVec bodyTopMoment tailMoment))
          -- Equivalent host torque reproduces the solved roll impulse in
          -- Chaos. Recoil and added-air inertia are already in the solve;
          -- adding either again here would double count them.
          moment = Vec3 (hostInertia * (x ratesNext - x bodyRates) / dt)
                        (y aeroMoment) (z aeroMoment)

          separations = map (stripSeparation . resultState) wingResults
          maximumSeparation = maximum (0 : separations)
          power = sum (map resultPower wingResults)

          -- Charge the actual motor torque, including acceleration and reversal,
          -- rather than only the aerodynamic hinge load. The 5 A stall-current
          -- assumption remains provisional until measured servo data is supplied.
          (torqueL,torqueR) = motorTorques (y ratesNext) (z ratesNext) (fst(aeroAt ratesNext (leftResults,rightResults)))
          motorPower = max 0 (torqueL*y ratesNext) + max 0 (torqueR*z ratesNext)
          torqueCurrent = (abs torqueL + abs torqueR) / max 0.01 (servoStallTorqueNm servo / 5)
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
            , fvLeftHingeTorqueNm = leftHinge + y (supportLoad externalForce)
            , fvRightHingeTorqueNm = rightHinge + z (supportLoad externalForce)
            , fvMotorCurrentA = motorCurrent
            , fvBatterySoc = nextSoc
            , fvBatteryBoost = boostNext
            , fvPhaseEnvelope = peNext
            , fvResponsePhase = nextResponsePhase
            , fvFlapHz = mixFlapHz mix
            , fvTailIncidenceDeg = tailIncidence
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
tailSurface area incidence chordVelocity normalVelocity =
  let speed = sqrt (chordVelocity*chordVelocity + normalVelocity*normalVelocity)
      alpha = atan2 (sin (incidence + atan2 (-normalVelocity) chordVelocity))
                    (cos (incidence + atan2 (-normalVelocity) chordVelocity))
      -- Static separation of a rigid panel. The wing additionally carries
      -- unsteady separation, elastic twist and camber states.
      t = max 0 (min 1 ((abs alpha-radians 13)/radians 7))
      separation = t*t*(3-2*t)
      -- Panel centres are +/-0.12 m: each half spans 0.24 m. Use the
      -- full pair's aspect ratio, just as the wing uses its full span/area.
      aspect = 0.48*0.48/(2*area)
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

-- | Round lifting-body polar. A streamlined body has a much weaker circulation
-- lift than a flat plate (its sides/top are curved, so the Kutta condition is
-- soft) but it still weathervanes through the crossflow pressure term. Lift
-- slope ≈0.45 is about half the flat plate, matching slender-body theory.
bodyLiftPolar :: PanelPolar
bodyLiftPolar = PanelPolar 0.45 0.034 0.10 2.5

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
