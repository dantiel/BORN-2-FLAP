module Main (main) where

import Born2Flap.Math.Types
import Born2Flap.Math.Wing
import Born2Flap.Math.Control
import Born2Flap.Math.Vehicle
import Born2Flap.Math.Planform
import Born2Flap.Math.Section
import Born2Flap.Math.Structure
import Born2Flap.Math.Simulation
import Born2Flap.Math.Waveform
  ( OscillatorState(..), defaultOscillator, advanceOscillator
  , limiarFromFerocities, shapeWave, shapeWaveWithDerivative )
import Born2Flap.Math.Firmware
import Born2Flap.Math.Servo
import Born2Flap.Math.FirmwareVehicle

main :: IO ()
main = do
  let strip moment = StripResult emptyStrip (Vec3 0 0 1) moment 0 0
      leftTorque = hingeTorque (-1) [strip (Vec3 (-2) 90 0)]
      rightTorque = hingeTorque 1 [strip (Vec3 2 90 0)]
  if leftTorque == 2 && rightTorque == 2 then pure ()
    else fail "mirrored flap hinges must project signed X torque, ignoring pitch torque"
  let fwStep dt vel s = stepFirmwareVehicle defaultRcChannels defaultFirmwareParams
                        defaultServoSpec defaultBatterySpec dt vel (Vec3 0 0 0) s
      badCases = [(0, Vec3 5 0 0), (0/0, Vec3 5 0 0), (0.01, Vec3 (1/0) 0 0)]
  mapM_ (\(dt, vel) -> let (o,s) = fwStep dt vel defaultFirmwareVehicleState
                       in if outputFlags o /= 0 && s == defaultFirmwareVehicleState
                          then pure () else fail "firmware failure must preserve all state") badCases
  let flap = defaultRcChannels {rcThrottle = 1811}
      batteryStep battery s = stepFirmwareVehicle flap defaultFirmwareParams defaultServoSpec
                               battery (1/240) (Vec3 5 0 0) (Vec3 0 0 0) s
      (_, full) = batteryStep defaultBatterySpec defaultFirmwareVehicleState
      (_, empty) = batteryStep defaultBatterySpec (defaultFirmwareVehicleState {fvBatterySoc = 0})
      loaded = defaultFirmwareVehicleState {fvLeftHingeTorqueNm = 0.5, fvRightHingeTorqueNm = 0.5}
      (_, lowR) = batteryStep (defaultBatterySpec {batteryInternalResistanceOhm = 0}) loaded
      (_, highR) = batteryStep (defaultBatterySpec {batteryInternalResistanceOhm = 0.8}) loaded
  if servoAngleDeg (fvServoLeft empty) == 0 && abs (servoAngleDeg (fvServoLeft full)) > 0
     && abs (servoRateDegPerSec (fvServoLeft highR)) < abs (servoRateDegPerSec (fvServoLeft lowR))
    then pure () else fail "empty battery and voltage sag must reduce powered motion"
  let rollback = runSimulation (putState (99 :: Int) >> abort "failure") 7
                   :: Either String ((), Int)
  if rollback == Left "failure" then pure () else fail "transaction must not expose failed state"
  let MetresPerSecond positive = crossflowBaseline 0.3 (Radians 0.4) (MetresPerSecond 8)
      MetresPerSecond negative = crossflowBaseline 0.3 (Radians (-0.4)) (MetresPerSecond 8)
  if positive > 0 && negative < 0 && abs (positive + negative) < 1.0e-12
    then pure ()
    else fail "crossflow must reverse with signed sweep"
  let initialController = AxisController
        { proportionalGain = 1.0
        , integralGain = 0.2
        , derivativeGain = 0.01
        , integralLimit = 0.5
        , integralState = 0.0
        , previousError = 0.0
        }
      result = stepAxis (Seconds 0.01) (AxisInput 1.0 0.0) initialController
  if command result > 0 && integralState (controller result) <= 0.5
    then pure ()
    else fail "controller must respond positively and respect its integral bound"
  let wing = defaultBirdWing
      wpArea = shapeArea wing
      chords = map (shapeChord wing) [0, 0.25, 0.5, 0.75, 1.0]
      wpAR = shapeAspectRatio wing
      stations = wsStations wing
      rootChord = if null stations then 0 else ssChordM (head stations)
      tipChord = if null stations then 0 else ssChordM (last stations)
      strips = shapeStationsAt wing 16
  if wpArea > 0 && wpAR > 3 && wpAR < 12 && all (> 0) chords
     && shapeChord wing 0 == rootChord
     && shapeChord wing 1 == tipChord
     && shapeTwistRad wing 1 < shapeTwistRad wing 0
     && shapeSweepRad wing 0 > 0 && shapeSweepRad wing 1 > shapeSweepRad wing 0
     && length strips == 16 && all ((> 0) . spanChord) strips
    then pure ()
    else fail "default wing must be a proper tapered, washed-out, back-swept shape"
  let camber = 0.05
      plainMoment = sectionPitchMomentCoeff camber 0.0
      reflexedMoment = sectionPitchMomentCoeff camber 0.4
      cupped = relaxCamber 0.01 0.05 0.05
                 (membraneCamberTarget defaultBirdHandSection 1.0)
  if zeroLiftAngle camber < 0
     && abs (sectionPitchMomentCoeff 0 0.9) < 1.0e-12
     && plainMoment < reflexedMoment && reflexedMoment < 0
     && cupped > 0.05
    then pure ()
    else fail "section model must couple camber to lift, reflex to trim, membrane to load"
  let structDr = 0.05
      structForces = replicate 8 1.0
      structEIs = replicate 8 2.0
      (bendDefl, bendSlope) = integrateFlapBeam structForces structEIs structDr
      structMoms = replicate 8 (-0.1)
      structGJs = replicate 8 1.0
      twistDefl = integrateTwist structMoms structGJs structDr
  if last bendDefl > 0 && last bendSlope > 0
     && last twistDefl < 0 && and (zipWith (<=) bendDefl (tail bendDefl))
    then pure ()
    else fail "cantilever beam must bend tip-up under up-load and twist nose-down under nose-down moment"
  let input = VehicleInput (1 / 240) (Vec3 5 0 0) (Vec3 0 0 0) 0.7 0 0 0
      samples = take 1200 (tail (iterate (\(_, state) -> stepVehicle input state)
                                   (zeroVehicleOutput, defaultVehicle)))
      outputs = map fst samples
      finite value = not (isNaN value || isInfinite value)
      allFinite output = all finite
        [ x (totalForceN output), y (totalForceN output), z (totalForceN output)
        , x (totalMomentNm output), y (totalMomentNm output), z (totalMomentNm output)
        , totalMechanicalPowerW output, maxSeparation output
        ]
  if all allFinite outputs && all (\output -> maxSeparation output >= 0 && maxSeparation output <= 1) outputs
    then pure ()
    else fail "vehicle loads must remain finite and separation bounded"
  let (_, warmed) = last samples
      changedThrottle = input { throttleCommand = 0.1 }
      (_, changedState) = stepVehicle changedThrottle warmed
      phaseWarmed = oscPhase (vehicleOscillator warmed)
      phaseChanged = oscPhase (vehicleOscillator changedState)
      expectedPhase = phaseWarmed + stepSeconds input * 2 * pi * (1.2 + 3.8 * 0.1)
      phaseError = atan2 (sin (phaseChanged - expectedPhase))
                         (cos (phaseChanged - expectedPhase))
      invalidInputs =
        [ input { stepSeconds = 0 }, input { stepSeconds = 0.06 }
        , input { throttleCommand = 0 / 0 }, input { bodyRatesRadS = Vec3 (1 / 0) 0 0 }
        , input { bodyVelocityMS = Vec3 0 (0 / 0) 0 }
        ]
  if abs phaseError < 1e-12 && abs phaseChanged <= pi
    then pure () else fail "throttle change must integrate a bounded continuous phase"
  mapM_ (\bad -> let (rejected, unchanged) = stepVehicle bad warmed
                 in if outputFlags rejected /= 0 && unchanged == warmed
                    then pure () else fail "invalid input must roll back all state") invalidInputs
  let stationary = input { bodyVelocityMS = Vec3 0 0 0, pitchCommand = 0, yawCommand = 0 }
      (neutral, _) = stepVehicle stationary defaultVehicle
      (deflected, _) = stepVehicle (stationary { pitchCommand = 1, yawCommand = 1 }) defaultVehicle
  if totalForceN neutral == totalForceN deflected && totalMomentNm neutral == totalMomentNm deflected
    then pure () else fail "stationary tail must not generate control forces without flow"
  if all (\o -> abs (y (totalForceN o)) < 1e-10 && abs (x (totalMomentNm o)) < 1e-10
                && abs (z (totalMomentNm o)) < 1e-10) outputs
    then pure () else fail "symmetric wings must cancel lateral loads and roll/yaw moments"
  let rollInput = input { rollCommand = 0.8 }
      rollOutputs = map fst (take 240 (drop 1 (iterate (\(_, s) -> stepVehicle rollInput s)
                                        (zeroVehicleOutput, defaultVehicle))))
  if any ((> 1.0e-6) . abs . x . totalMomentNm) rollOutputs
    then pure () else fail "differential flapping must create a roll moment"
  -- Waveform fidelity (port of PteronautOS FlappingOscillator::shapeWave).
  let f = 4.0
      halfWaveSymmetry theta =
        abs (shapeWave theta f f (-1) 0 0 0 + shapeWave (theta + pi) f f (-1) 0 0 0)
  if all (< 1.0e-9) (map halfWaveSymmetry [0.0, 0.5, 1.2, 2.0, 2.9, pi - 0.01])
    then pure () else fail "equal ferocities must give an odd-symmetric wave"
  let lim = limiarFromFerocities 3 5
      bounded p = let v = shapeWave p 3 5 lim 20 10 (-10) in v >= -1.0000001 && v <= 1.0000001
  if all bounded [0.0, 0.7, 1.3, 2.1, 3.0, 4.5, 6.0]
    then pure () else fail "shaped wave must stay within [-1, +1]"
  let eps = 1.0e-5
      derivPoint = 1.3
      (_, dv) = shapeWaveWithDerivative derivPoint 3 5 lim 20 10 (-10)
      fd = (shapeWave (derivPoint + eps) 3 5 lim 20 10 (-10)
            - shapeWave (derivPoint - eps) 3 5 lim 20 10 (-10)) / (2 * eps)
  if abs (dv - fd) < 1.0e-3
    then pure () else fail "analytic wave derivative must match finite differences"
  let (_, osc1) = advanceOscillator 12 1 0 (1 / 240) defaultOscillator
  if oscPhase osc1 >= 0 && oscPhase osc1 < 2 * pi && oscDebtVel osc1 == 0
    then pure ()
    else fail "beat-locked oscillator must stay on-grid at nominal demand"
  -- Firmware mixer: glide at rest, flapping at full throttle with correct freq.
  let (glide, _) = computeServoMixer defaultRcChannels defaultFirmwareParams defaultFirmwareState (1 / 240)
  if not (mixIsFlapping glide) && mixFlapHz glide == 0
     && mixLeftFlapDevDeg glide == 4 && mixRightFlapDevDeg glide == 4
    then pure ()
    else fail "neutral glide must use the configured wing position"
  let mixer pilot = fst (computeServoMixer (pilotToRc pilot) defaultFirmwareParams defaultFirmwareState (1/240))
      glideYaw = mixer (PilotInput 0 0 0 0.5)
      glideRoll = mixer (PilotInput 0 0.5 0 0)
      glidePitch = mixer (PilotInput 0 0 0.5 0)
      cancelled = mixer (PilotInput 0 0.325 0 0.5)
      yawDiff = mixRightFlapDevDeg glideYaw - mixLeftFlapDevDeg glideYaw
      rollDiff = mixRightFlapDevDeg glideRoll - mixLeftFlapDevDeg glideRoll
  if yawDiff > 1 && rollDiff < -1 && mixLeftFlapDevDeg glidePitch < 4
     && abs (mixRightFlapDevDeg cancelled - mixLeftFlapDevDeg cancelled) < 1e-10
    then pure () else fail "glide RC channels must move the shared wings and allow opposing commands to cancel"
  let coherent output = abs (mixLeftFlapDevDeg output + (mixLeftWingDeg output-100)/2) < 1e-10
                     && abs (mixRightFlapDevDeg output - (mixRightWingDeg output-100)/2) < 1e-10
  if all coherent [glideYaw,glideRoll,glidePitch,mixer (PilotInput 0.72 0.4 (-0.3) 0.5)]
    then pure () else fail "physical flap angles must include the complete mixed servo command"
  if all (\a -> abs (crsfToNorm (normToRaw a) - a) < 1e-12) [-1,-0.5,0,0.5,1]
    then pure () else fail "RC normalization must preserve exact neutral and endpoint values"
  let flapRc = defaultRcChannels { rcThrottle = 1811 }
      (flap, flapState) = computeServoMixer flapRc defaultFirmwareParams defaultFirmwareState (1 / 240)
  if mixIsFlapping flap && mixFlapHz flap > 0 && mixThrottlePct flap == 1
     && mixLeftFlapDevDeg flap /= 0 && mixRightFlapDevDeg flap /= 0
    then pure ()
    else fail "full throttle must flap with positive frequency and deflection"
  -- Flapping hysteresis: dropping to just below the on-threshold stays flapping.
  let lowThrottle = defaultRcChannels { rcThrottle = 300 }
      (hystOut, _) = computeServoMixer lowThrottle defaultFirmwareParams flapState (1 / 240)
  if mixIsFlapping hystOut
    then pure ()
    else fail "flapping must persist within the hysteresis band"
  -- Servo: unloaded tracking converges toward the target.
  let servo = defaultServoSpec
      battery = defaultBatterySpec
      voltage = batteryNominalVoltage battery
      (a1, s1) = stepServo servo battery 120 0 voltage (1 / 240) defaultServoState
      (a2, s2) = stepServo servo battery 120 0 voltage (1 / 240) s1
  if a2 > a1 && a2 <= 120
    then pure ()
    else fail "unloaded servo must slew toward its target"
  -- Servo: load exceeding stall torque back-drives it (the wing wins).
  -- A strong NEGATIVE load pushes the servo away from a positive target.
  let stall = servoStallTorqueNm servo
      (back, _) = stepServo servo battery 120 (negate stall * 2) voltage (1 / 240) defaultServoState
  if back < 0
    then pure ()
    else fail "overloaded servo must be back-driven by the aerodynamic load"
  -- Closed loop: full throttle flaps the wings (servo tracks, lift appears).
  let flapRcClosed = defaultRcChannels { rcThrottle = 1811 }
      dtClosed = 1 / 240
      bodyVel = Vec3 5 0 0
      (out1, st1) = stepFirmwareVehicle flapRcClosed defaultFirmwareParams
                     defaultServoSpec defaultBatterySpec dtClosed bodyVel (Vec3 0 0 0)
                     defaultFirmwareVehicleState
      (out2, _) = stepFirmwareVehicle flapRcClosed defaultFirmwareParams
                     defaultServoSpec defaultBatterySpec dtClosed bodyVel (Vec3 0 0 0) st1
      finite value = not (isNaN value || isInfinite value)
  if finite (z (totalForceN out2)) && finite (x (totalMomentNm out2))
     && totalMechanicalPowerW out2 >= 0
    then pure ()
    else fail "firmware-vehicle closed loop must produce finite loads"
  -- Logical pilot input: maps faithfully onto the firmware's CRSF channel space.
  let rcFull = pilotToRc (PilotInput 1 0 0 0)
  if rcThrottle rcFull == 1811 && rcAileron rcFull == 992 && rcRudder rcFull == 992
     && rcArm rcFull == 1811
    then pure ()
    else fail "full-throttle pilot input must map to full CRSF throttle with neutral sticks"
  let rcGlide = pilotToRc defaultPilotInput
      (glidePilot, _) = computeServoMixer rcGlide defaultFirmwareParams defaultFirmwareState (1 / 240)
  if not (mixIsFlapping glidePilot)
    then pure ()
    else fail "zero-throttle pilot input must hold glide"
  let rcRoll = pilotToRc (PilotInput 1 1 0 0)
  if rcAileron rcRoll > 992 && rcRudder rcRoll == 992 && rcElevator rcRoll == 992
    then pure ()
    else fail "roll stick must deflect the aileron channel only"
  -- Closed loop via logical pilot input: full throttle flaps and tracks battery.
  let pilotFlap = PilotInput 1 0 0 0
      dtFw = 1 / 240
      bodyVelFw = Vec3 5 0 0
      stepFw s = stepFirmwareVehicle (pilotToRc pilotFlap) defaultFirmwareParams
                   defaultServoSpec defaultBatterySpec dtFw bodyVelFw (Vec3 0 0 0) s
      goFw 0 s peak = (s, peak)
      goFw n s peak =
        let (_, s') = stepFw s
            peak' = max peak (max (abs (servoAngleDeg (fvServoLeft s')))
                                  (abs (servoAngleDeg (fvServoRight s'))))
        in goFw (n - 1) s' peak'
      (warmFw, peakFlap) = goFw 120 defaultFirmwareVehicleState 0
  if fwWasFlapping (fvFirmware warmFw) && peakFlap > 1.0 && fvBatterySoc warmFw <= 1.0
    then pure ()
    else fail "pilot-driven firmware loop must flap both wings and track battery"
  -- Isolate aileron timing: no static wing offset, rudder surface or amplitude
  -- differential is available to generate this roll moment.
  let skewParams = defaultFirmwareParams
        { fwServoSpeedMs = 50, fwFlapBaseFreqDh = 32, fwRudderRollWeight = 0
        , fwProfile = (fwProfile defaultFirmwareParams)
            { profThrottleFrequencyMix = 100, profAileronSkewMix = 55
            , profAileronScale = 0, profRudderAmplitudeDiff = 0 } }
      skewServo = defaultServoSpec { servoNoLoadSpeedDegPerSec = 1200, servoStallTorqueNm = 8 }
      skewBattery = defaultBatterySpec
        { batteryNominalVoltage = 11.1, batteryCapacityAh = 1.3, batteryInternalResistanceOhm = 0.08 }
      skewMoment roll =
        let rc = pilotToRc (PilotInput 0.72 roll 0 0)
            tick (_,s) = stepFirmwareVehicle rc skewParams skewServo skewBattery
                          (1/240) (Vec3 8 0 (-1)) (Vec3 0 0 0) s
            samples = drop 481 $ take 1921 $ iterate tick (zeroVehicleOutput,defaultFirmwareVehicleState)
        in sum (map (x . totalMomentNm . fst) samples) / fromIntegral (length samples)
      leftSkewMoment = skewMoment (-0.5)
      rightSkewMoment = skewMoment 0.5
  if leftSkewMoment > 0.01 && rightSkewMoment < -0.01
     && abs (leftSkewMoment + rightSkewMoment) < 1e-10
    then pure () else fail "opposite stroke timing alone must generate mirrored aerodynamic roll"
  putStrLn "MathCore properties passed"

zeroVehicleOutput :: VehicleOutput
zeroVehicleOutput = VehicleOutput (Vec3 0 0 0) (Vec3 0 0 0) 0 0 (-1) 0
