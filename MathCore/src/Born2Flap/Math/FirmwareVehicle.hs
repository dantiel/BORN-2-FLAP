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
          force = addVec wingForce (addVec bodyDrag (addVec bodySideForce (addVec bodyTopForce tailForce)))
          aeroMoment = addVec wingMoment (addVec bodySideMoment (addVec bodyTopMoment tailMoment))
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

-- | Flat-plate V-tail polar (the reference ruddervator).
tailPolar :: PanelPolar
tailPolar = PanelPolar 1.1 0.025 0.14 0.8

tailSurface :: Double -> Double -> Double -> Double -> (Double, Double)
tailSurface = panelSurface tailPolar

-- | Round lifting-body polar. A streamlined body has a much weaker circulation
-- lift than a flat plate (its sides/top are curved, so the Kutta condition is
-- soft) but it still weathervanes through the crossflow pressure term. Lift
-- slope ≈0.45 is about half the flat plate, matching slender-body theory.
bodyLiftPolar :: PanelPolar
bodyLiftPolar = PanelPolar 0.45 0.02 0.10 0.90

-- | Centre of pressure of the fuselage aft-body + tail boom, BEHIND the CG.
-- Everything aft acts like a wing fence / vertical stabiliser; this lever arm
-- turns both the lift and the pressure drag above into a weathervaning yaw
-- moment and a nose-down (restoring) pitch moment.
fuselageAftArm :: Vec3
fuselageAftArm = Vec3 (-0.15) 0 0

-- | Aft-body side/top projected area [m²] used as the lifting-body reference
-- (≈0.21 m behind the CG × 0.03 m body height/width; the cross-section is
-- ~square so side and top are equal).
fuselageAftArea :: Double
fuselageAftArea = 0.0063