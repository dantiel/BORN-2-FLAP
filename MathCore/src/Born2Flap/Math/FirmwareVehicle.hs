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
  , stepFirmwareVehicleWithInertia
  , hingeTorque
  ) where

import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Firmware
import Born2Flap.Math.Servo
import Born2Flap.Math.RollDynamics (stepRoll)
import Born2Flap.Math.Model (WingModel(..))
import Born2Flap.Math.Planform
  ( shapeFlapInertia, referenceWingMassKg )
import Born2Flap.Math.Simulation
import Born2Flap.Math.Waveform (OscillatorState(..), limiarFromFerocities)
import Born2Flap.Math.PhaseEnvelope
import Born2Flap.Math.Resonance
import Born2Flap.Math.Vehicle
  ( VehicleInput(..), VehicleOutput(..)
  , StripState(..), StripResult(..), initialStrips
  , stepCoupledWing, prepareWingLoads, wingRootHeight, airDensity
  , addVec, crossVec, scaleVec, magnitude, zeroVec, radians )

data FirmwareVehicleState = FirmwareVehicleState
  { fvFirmware        :: !FirmwareState
  , fvServoLeft       :: !ServoState
  , fvServoRight      :: !ServoState
  , fvLeftStrips      :: ![StripState]
  , fvRightStrips     :: ![StripState]
  , fvLeftStripForceMag  :: ![Double]  -- ^ per-strip |aero force| [N] root→tip, last step (audio drive)
  , fvRightStripForceMag :: ![Double]
  , fvLeftHingeTorqueNm  :: !Double   -- ^ aero load fed to the servo next step
  , fvRightHingeTorqueNm :: !Double
  , fvBatterySoc      :: !Double     -- ^ 0..1, drains under servo current
  , fvPhaseEnvelope   :: !PhaseEnvelopeState  -- ^ ONDAS A-layer
  , fvResonance       :: !ResonanceState      -- ^ ONDAS C-layer
  , fvStabilized      :: !Bool      -- ^ ONDAS stabilized-mode master switch
  , fvWindPhaseNoise  :: !Double    -- ^ wind phase noise η [rad/s] (environmental)
  } deriving stock (Eq, Show)

defaultFirmwareVehicleState :: FirmwareVehicleState
defaultFirmwareVehicleState = FirmwareVehicleState
  { fvFirmware = defaultFirmwareState
  , fvServoLeft = defaultServoState
  , fvServoRight = defaultServoState
  , fvLeftStrips = initialStrips
  , fvRightStrips = initialStrips
  , fvLeftStripForceMag = []
  , fvRightStripForceMag = []
  , fvLeftHingeTorqueNm = 0
  , fvRightHingeTorqueNm = 0
  , fvBatterySoc = 1.0
  , fvPhaseEnvelope = defaultPhaseEnvelope
  , fvResonance = defaultResonance
  , fvStabilized = False
  , fvWindPhaseNoise = 0
  }

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
stepFirmwareVehicle coupled rc params servo battery model =
  stepFirmwareVehicleWithInertia 0.008 coupled rc params servo battery model

-- Host inertia is the dry, locked-wing roll inertia used by the rigid-body
-- integrator. Added air enters the coupled mass matrix, never the host tensor.
stepFirmwareVehicleWithInertia
  :: Double -> Bool -> RcChannels -> FirmwareParams -> ServoSpec -> BatterySpec -> WingModel
  -> Double -> Vec3 -> Vec3 -> Double -> FirmwareVehicleState
  -> (VehicleOutput, FirmwareVehicleState)
stepFirmwareVehicleWithInertia hostInertia coupled rc params servo battery model dt bodyVel bodyRates groundClearance state
  | not (finite hostInertia) || hostInertia <= 0 = (zeroOutput 2, state)
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
          run = stepFirmwareVehicleWithInertia hostInertia coupled rc params servo battery model half bodyVel
          (a, middle) = run bodyRates groundClearance state
          middleRates = Vec3 (x bodyRates + half*x (totalMomentNm a)/hostInertia) (y bodyRates) (z bodyRates)
          (b, final) = run middleRates groundClearance middle
          average u v = scaleVec 0.5 (addVec u v)
      in if outputFlags a /= 0 || outputFlags b /= 0 then (zeroOutput flag,state)
         else (b { totalForceN = average (totalForceN a) (totalForceN b)
                 , totalMomentNm = average (totalMomentNm a) (totalMomentNm b)
                 , totalMechanicalPowerW = 0.5*(totalMechanicalPowerW a+totalMechanicalPowerW b)
                 , maxSeparation = max (maxSeparation a) (maxSeparation b) }, final)
    transaction = do
      old <- getState
      let (output, next) = advanceFirmwareVehicle hostInertia coupled rc params servo battery model dt bodyVel bodyRates groundClearance old
          values = components (totalForceN output) ++ components (totalMomentNm output) ++
            [totalMechanicalPowerW output, maxSeparation output, fvBatterySoc next,
             fvLeftHingeTorqueNm next, fvRightHingeTorqueNm next,
             servoAngleDeg (fvServoLeft next), servoAngleDeg (fvServoRight next),
             servoRateDegPerSec (fvServoLeft next), servoRateDegPerSec (fvServoRight next),
             phaseEnvelope (fvPhaseEnvelope next), phaseCoverage (fvPhaseEnvelope next)]
      if all finite values then putState next >> pure output else abort 2

finite :: Double -> Bool
finite value = not (isNaN value || isInfinite value)

components :: Vec3 -> [Double]
components (Vec3 a b c) = [a,b,c]

-- Both positive stroke coordinates raise the wing. The left hinge axis is -X.
hingeTorque :: Double -> [StripResult] -> Double
hingeTorque side = (* side) . sum . map (\r -> x (resultMoment r) + wingRootHeight * y (resultForce r))

advanceFirmwareVehicle
  :: Double -> Bool -> RcChannels -> FirmwareParams -> ServoSpec -> BatterySpec -> WingModel
  -> Double -> Vec3 -> Vec3 -> Double -> FirmwareVehicleState -> (VehicleOutput, FirmwareVehicleState)
advanceFirmwareVehicle hostInertia coupled rc params servo battery model dt bodyVel bodyRates groundClearance state
  | otherwise =
      let -- 1. Firmware mixer: RC → wing servo commands (flap deviation deg).
          (mix, nextFw) = computeServoMixer coupled rc params (fvFirmware state) dt
     
          -- Wing rotational inertia about the flap hinge, derived from the
          -- model's actual planform: structural mass + entrained-air added mass
          -- (∝ ρ·chord²). Shared by the servo (rate acceleration) and the
          -- inertial body recoil (Newton's 3rd law).
          wingMass = referenceWingMassKg
          wingInertia = shapeFlapInertia wingMass airDensity (wmWing model)
     
          dryWingInertia = shapeFlapInertia wingMass 0 (wmWing model)
          lockedInertia = hostInertia + 2 * (wingInertia - dryWingInertia)
          currentA = min 10 ((abs (fvLeftHingeTorqueNm state) + abs (fvRightHingeTorqueNm state))
                       / max 0.01 (servoStallTorqueNm servo / 5))
          liveBattery = battery { batteryStateOfCharge = fvBatterySoc state }
          voltage = batteryVoltageUnderLoad liveBattery currentA
          oldL = fvServoLeft state
          oldR = fvServoRight state
          angleL = radians (servoAngleDeg oldL)
          angleR = radians (servoAngleDeg oldR)
          oldRates = Vec3 (x bodyRates) (radians (servoRateDegPerSec oldL)) (radians (servoRateDegPerSec oldR))
          motorL = servoDriveTorque servo battery voltage dt (mixLeftFlapDevDeg mix)
                     (fvLeftHingeTorqueNm state) wingInertia oldL
          motorR = servoDriveTorque servo battery voltage dt (mixRightFlapDevDeg mix)
                     (fvRightHingeTorqueNm state) wingInertia oldR
          evaluate rates =
            let p = x rates; l = y rates; r = z rates
                wingInput = input { bodyRatesRadS = Vec3 p (y bodyRates) (z bodyRates) }
                left = stepCoupledWing (mixIsFlapping mix) (-1) angleL l (y oldRates) wingInput model (fvLeftStrips state)
                right = stepCoupledWing (mixIsFlapping mix) 1 angleR r (z oldRates) wingInput model (fvRightStrips state)
            in (left,right)
          leftLoads = prepareWingLoads (mixIsFlapping mix) (-1) angleL (y oldRates) input model (fvLeftStrips state)
          rightLoads = prepareWingLoads (mixIsFlapping mix) 1 angleR (z oldRates) input model (fvRightStrips state)
          aeroAt rates =
            let ls = leftLoads (x rates) (y rates)
                rs = rightLoads (x rates) (z rates)
            in Vec3 (sum (zipWith (\l r -> x (resultMoment l) + x (resultMoment r)) ls rs) + x tailMoment)
                    (hingeTorque (-1) ls) (hingeTorque 1 rs)
          aero0 = aeroAt oldRates
          loads rates =
            let aero = aeroAt rates
                drag load rate = servoBackdriveTorque servo battery voltage load rate
            in addVec aero (Vec3 0 (motorL (y rates) + drag (y aero0) (y rates))
                                   (motorR (z rates) + drag (z aero0) (z rates)))
          solved = stepRoll dt lockedInertia wingInertia reflectedDriveInertia oldRates (angleL,angleR) loads
          -- NaN triggers transactional rollback if Newton cannot converge.
          ratesNext = case solved of Just rates -> rates; Nothing -> Vec3 (0/0) (0/0) (0/0)
          makeServo angle rate = ServoState ((angle+dt*rate)*180/pi) (rate*180/pi)
          nextServoL = makeServo angleL (y ratesNext)
          nextServoR = makeServo angleR (z ratesNext)

          -- 2b. ONDAS A-injection: golden-angle phase-envelope strobe at reversal.
          prevPhase = oscPhase (fwOscillator (fvFirmware state))
          limiar = limiarFromFerocities (mixStrokeFerL mix) (mixReturnFerL mix)
          reversal = mixIsFlapping mix
                     && detectReversal prevPhase (mixPhase mix) limiar
          -- Signed servo tracking error: the Vold–Kalman tracker extracts its
          -- phase-coherent ω-component, so the RESONANCE layer must see the
          -- SIGNED lag (an |abs| would double the frequency and read ~0 at ω).
          signedTrackErr = mixLeftFlapDevDeg mix - servoAngleDeg nextServoL
          trackErr = abs signedTrackErr
          peNext = if reversal then phaseStrobe trackErr (fvPhaseEnvelope state)
                                else fvPhaseEnvelope state

          -- 2c. ONDAS C-injection: Vold–Kalman engagement → phase-lock demand.
          omega = oscCadence (fwOscillator nextFw)
          (kGainModDemand, nextResonance) =
            stepResonance signedTrackErr omega (fvWindPhaseNoise state) dt (fvResonance state)
          -- The stabilized mode closes the A-layer: the golden-angle strobe's
          -- coverage (0..1) gates the resonance demand, so the phase-lock only
          -- acts once the cycle has been read honestly — never on an aliased
          -- overtone. Off-mode holds the oscillator at nominal phase-advance 1.
          aGate = if fvStabilized state
                  then max 0 (min 1 ((phaseCoverage peNext - 0.5) / 0.5))
                  else 0
          kGainModNext = 1 + aGate * (kGainModDemand - 1)
          nextFw' = nextFw { fwKGainMod = kGainModNext }

          -- 3. Actual flap deviation → wing physics.
          input = VehicleInput dt bodyVel bodyRates 0 (crsfToNorm (rcAileron rc)) elevatorNorm 0 groundClearance
          ((leftResults, leftNext), (rightResults, rightNext)) = evaluate ratesNext
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
          -- Fuselage weathervane: a long thin body has near-zero frontal drag
          -- but a large side projected area. In sideslip that side area acts as
          -- a vertical stabiliser — its lateral centre of pressure sits BEHIND
          -- the CG, so a lateral velocity produces (a) a side force opposing the
          -- slip and (b) a yaw moment turning the nose into the relative wind.
          -- This is the aerodynamic yaw damping a bird gets for free, and the
          -- missing term behind the perpetual left-right sway. The side area is
          -- 0.42 m × 0.03 m at Cd ≈ 1.0 (cylinder in crossflow) ≈ 0.012 m².
          bodySideFlow = addVec bodyVel (crossVec bodyRates fuselageSideArm)
          bodySideForce = Vec3 0 (-0.5 * airDensity * fuselageSideCdA * y bodySideFlow * abs (y bodySideFlow)) 0
          bodySideMoment = crossVec fuselageSideArm bodySideForce
          -- Two real inverted-V panels with mixed ruddervators. Each panel sees
          -- its own local flow and produces force at its own arm; the resulting
          -- pitch/yaw coupling is geometric, not an added torque. A shallow cant
          -- (12°) keeps the differential ruddervator a pure YAW surface: roll is
          -- the wings' job (differential-dihedral lift tilt via the flap-centre
          -- deviation in the mixer), never the tail's. The tail only damps and
          -- steers yaw/pitch.
          cant = radians 12
          -- A direct flap-power/ruddervator mix balances the wing drive's changing
          -- pitching moment. Glide returns to its shallow incidence; no attitude,
          -- altitude or speed target is involved.
          tailTrim = -2.0 - 4.0 * mixThrottlePct mix * mixThrottlePct mix
          tailPanel side =
            let -- Tail geometry is the reference kestrel's fixed arm and panel
                -- area (0.85 m arm, 0.0242 m² panel); it is part of the bird,
                -- not scaled per planform.
                arm = Vec3 (-0.85) (side * 0.12) (-0.12 * tan cant)
                normal = Vec3 0 (side * sin cant) (cos cant)
                flow = addVec bodyVel (crossVec bodyRates arm)
                normalFlow = y flow * y normal + z flow * z normal
                -- Ruddervator deflection is a rotation about the spanwise hinge,
                -- i.e. a *direct* change of panel incidence (no sin/cos here).
                -- The projection onto body yaw/pitch axes is already done by the
                -- panel normal below; folding sin(cant)/cos(cant) into the
                -- incidence double-projects and throttles the rudder by sin²(cant).
                incidence = radians (tailTrim - 18 * elevatorNorm
                                      - side * 30 * rudderNorm)
                (fx, fn) = tailSurface 0.0242 incidence (x flow) normalFlow
                panelForce = addVec (Vec3 fx 0 0) (scaleVec fn normal)
            in (panelForce, crossVec arm panelForce)
          panels = map tailPanel [-1,1]
          tailForce = foldr (addVec . fst) zeroVec panels
          tailMoment = foldr (addVec . snd) zeroVec panels
          force = addVec wingForce (addVec bodyDrag (addVec bodySideForce tailForce))
          aeroMoment = addVec wingMoment (addVec bodySideMoment tailMoment)
          -- Equivalent host torque reproduces the solved roll impulse in
          -- Chaos. Recoil and added-air inertia are already in the solve;
          -- adding either again here would double count them.
          moment = Vec3 (hostInertia * (x ratesNext - x bodyRates) / dt)
                        (y aeroMoment) (z aeroMoment)

          separations = map (stripSeparation . resultState) wingResults
          maximumSeparation = maximum (0 : separations)
          power = sum (map resultPower wingResults)

          -- 6. Battery drain: servo current ∝ torque magnitude.
          socDrop = batterySocDrop servo battery (abs leftHinge + abs rightHinge) dt
          nextSoc = max 0 (fvBatterySoc state - socDrop)

          nextState = state
            { fvFirmware = nextFw'
            , fvServoLeft = nextServoL
            , fvServoRight = nextServoR
            , fvLeftStrips = leftNext
            , fvRightStrips = rightNext
            , fvLeftStripForceMag = map (magnitude . resultForce) leftResults
            , fvRightStripForceMag = map (magnitude . resultForce) rightResults
            , fvLeftHingeTorqueNm = leftHinge
            , fvRightHingeTorqueNm = rightHinge
            , fvBatterySoc = nextSoc
            , fvPhaseEnvelope = peNext
            , fvResonance = nextResonance
            }

          output = VehicleOutput force moment power maximumSeparation (-1) 0
      in (output, nextState)

-- | Battery state-of-charge drain per step, from servo torque. Current is
-- approximated as torque ∕ (voltage · torque-per-amp), capacity in Ah.
batterySocDrop :: ServoSpec -> BatterySpec -> Double -> Double -> Double
batterySocDrop servo battery torqueNm dt =
  let -- torque-per-amp: assume stall torque draws ~ 5 A at nominal voltage.
      torquePerAmp = servoStallTorqueNm servo / 5.0
      currentA = min 10 (torqueNm / max 0.01 torquePerAmp)
      ampHours = currentA * (dt / 3600)
  in ampHours / max 0.01 (batteryCapacityAh battery)

zeroOutput :: Int -> VehicleOutput
zeroOutput flags = VehicleOutput zeroVec zeroVec 0 0 (-1) flags

-- Provisional low-aspect-ratio tail polar. Even a deflected surface cannot
-- create translational energy: lift is perpendicular, Cd is nonnegative.
tailSurface :: Double -> Double -> Double -> Double -> (Double, Double)
tailSurface area incidence chordVelocity normalVelocity =
  let speed = sqrt (chordVelocity * chordVelocity + normalVelocity * normalVelocity)
      alpha = incidence + atan2 (-normalVelocity) chordVelocity
      cl = 1.1 * sin (2 * alpha)
      cd = 0.025 + 0.14 * cl * cl + 0.8 * sin alpha * sin alpha
      q = 0.5 * airDensity * area * speed * speed
      u = chordVelocity / max 1.0e-9 speed
      w = normalVelocity / max 1.0e-9 speed
  in (q * (-cd * u - cl * w), q * (-cd * w + cl * u))
-- | Fuselage side projected area × drag coefficient [m²]. The body is a long
-- thin slab: ~0.42 m × 0.03 m side profile at Cd ≈ 1.0 (cylinder in
-- crossflow). This is the vertical-stabiliser/weathervane term, deliberately
-- separate from the frontal parasitic drag (CdA ≈ 4e-4 m²) and from the tail's
-- own polar in 'tailSurface'. Kept as a named constant so a future lift-
-- producing or very-unstable tail model can scale it independently.
fuselageSideCdA :: Double
fuselageSideCdA = 0.012

-- | Lateral centre of pressure of the fuselage + tail boom, aft of the CG.
-- Everything behind the CG acts like a wing fence / vertical stabiliser; this
-- lever arm is what turns the side force above into a weathervaning yaw moment.
fuselageSideArm :: Vec3
fuselageSideArm = Vec3 (-0.15) 0 0