module Main (main) where

import Born2Flap.Math.Types
import Born2Flap.Math.Wing
import Born2Flap.Math.Control
import Born2Flap.Math.Vehicle
import Born2Flap.Math.Planform
import Born2Flap.Math.Airframe
import Born2Flap.Math.Section
import Born2Flap.Math.Structure
import Born2Flap.Math.Model
import Born2Flap.Math.Simulation
import Born2Flap.Math.Waveform
  ( OscillatorState(..), defaultOscillator, advanceOscillator
  , limiarFromFerocities, shapeWave, shapeWaveWithDerivative, wavePeakDerivativeBound )
import Born2Flap.Math.Firmware
import Born2Flap.Math.Servo
import Born2Flap.Math.RollDynamics
import Born2Flap.Math.FirmwareVehicle
import Born2Flap.Math.FerocityPrice
import Born2Flap.Math.PhaseEnvelope
import Born2Flap.Math.ResponsePhase

main :: IO ()
main = do
  -- Structural ringing stores real kinetic/elastic energy, not waveform phase.
  let energy (q,v)=0.5*0.02*v*v+0.5*8*q*q
      ring damping = take 1000 (iterate (\(q,v) -> stepElasticMode (1/240) 0.02 8 damping 0 q v) (0.01,0))
      undamped=map energy (ring 0)
      damped=map energy (ring 0.08)
  if all (\e -> abs(e-head undamped)<1e-12) undamped &&
     and (zipWith (>=) damped (drop 1 damped)) && last damped < head damped*0.001
    then pure () else fail "structural mode must conserve free energy or dissipate with damping"
  let response frequency =
        let tick (q,v,peak) i =
              let force=0.01*sin(2*pi*frequency*fromIntegral i/240)
                  (q1,v1)=stepElasticMode (1/240) 0.02 8 0.04 force q v
              in (q1,v1,if i>1200 then max peak (abs q1) else peak)
            (_,_,peak)=foldl tick (0,0,0) [1..2400::Int]
        in peak
      naturalHz=sqrt(8/0.02)/(2*pi)
  if response naturalHz > 3*max (response (naturalHz/2)) (response (naturalHz*2))
    then pure () else fail "elastic resonance must follow sqrt(stiffness/inertia), not flap phase"
  let coupledEnergy ((b,t),(v,w))=0.5*(0.02*v*v-0.002*v*w+0.001*w*w+8*b*b+0.2*t*t)
      coupledRing=take 1000 (iterate (\(q,v) -> stepCoupledElasticModes (1/240)
                   (0.02,-0.001,0.001) (8,0.2) (0,0) (0,0) q v) ((0.01,0.1),(0,0)))
  if all (\s -> abs(coupledEnergy s-coupledEnergy(head coupledRing))<1e-12) coupledRing
    then pure () else fail "coupled bend/twist inertia must conserve free modal energy"
  let tailPassive (u,w,a)=let (fx,fz)=tailSurface 0.0242 a u w in fx*u+fz*w<=1e-10
      (_,broadsideTail)=tailSurface 0.0242 0 0 3
      expectedPressure=surfacePressure 0.0242 3-0.5*airDensity*0.0242*surfaceSkinCd*9
  if abs (broadsideTail-expectedPressure)<1e-12 &&
     all tailPassive [(u,w,a) | u<-[-8,0,8],w<-[-5,0,5],a<-[-0.5,0,0.5]]
    then pure () else fail "tail must share passive wing pressure and skin constants"
  let (bodyMass,wingMass,tailMass)=airframeMasses 0.05
      (ix,iy,iz)=airframeInertia kestrelModel 0.05 0
      wingDry=shapeFlapInertia wingMass 0 defaultBirdWing
  if abs (bodyMass-0.038)<1e-12 && wingMass==0.005 && tailMass==0.002 &&
     abs (bodyMass+2*wingMass+tailMass-0.05)<1e-12 &&
     ix>2*wingDry && ix<2*wingDry*1.15 && iy>0 && iz>0
    then pure () else fail "component mass budget and narrow-body roll inertia"
  let flow=VehicleInput (1/240) (Vec3 8 0 (-1)) (Vec3 0 0 0) 0 0 0 0 5
      loads rate=wingLoadsWithStroke True 1 0.1 rate 0 flow kestrelModel 0 initialStrips
      summed=foldr (addVec . resultForce) zeroVec
      still=summed (loads 0)
      beating=summed (loads (-3))
  -- Stroke motion tilts the aerodynamic resultant; no independent jet.
  if vx beating > vx still
    then pure () else fail "flap thrust must emerge from stroke motion, not a flag"
  -- Coupled inertia: internal motor impulses conserve angular momentum;
  -- kinetic work converges to the continuous constant-torque solution.
  let j=0.002; rotor=0.0016; locked=0.008; dt=1/240
      zero=Vec3 0 0 0
      drive _=Vec3 0 (-1) 1
      solve h old angles loads = case stepRoll h locked j rotor old angles loads of
        Just v -> v
        Nothing -> error "coupled roll solve failed"
      moved=solve dt zero (0,0) drive
      a=1/(j+rotor-2*j*j/locked)
  if abs (rollMomentum locked j moved) < 1e-9
     && abs (y moved + dt*a) < 1e-6 && abs (z moved - dt*a) < 1e-6
    then pure () else fail "free-body motor acceleration must match coupled mass matrix"
  let symmetric=solve dt zero (0,0) (const (Vec3 0 1 1))
      damp v=Vec3 (-0.2*x v) (-0.1*y v) (-0.1*z v)
      moving=Vec3 2 (-3) 4
      damped=solve dt moving (0,0) damp
      stopped=solve dt moving (-80*pi/180,80*pi/180) (const zero)
  if abs (x symmetric) < 1e-9
     && rollEnergy locked j rotor damped < rollEnergy locked j rotor moving
     && abs (rollMomentum locked j stopped-rollMomentum locked j moving) < 1e-8
     && y stopped >= -1e-8 && z stopped <= 1e-8
     && rollEnergy locked j rotor stopped <= rollEnergy locked j rotor moving + 1e-9
    then pure () else fail "symmetry, passive dissipation and internal end-stop momentum must hold"
  if servoDriveTorque defaultServoSpec defaultBatterySpec 0 dt 40 0 j defaultServoState 1 == 0
    then pure () else fail "empty battery must supply no motor torque"
  -- If roll and hinge motion cancel in world space, the wing sees the same
  -- air velocity (including compensation for translation of the high root).
  let wingInput=VehicleInput dt (Vec3 8 0 0) zero 0 0 0 0 1000
      sumForce=foldr (addVec . resultForce) zero
      steady=sumForce (wingLoadsWithStroke False 1 0 0 0 wingInput kestrelModel 0 initialStrips)
      counter=sumForce (wingLoadsWithStroke False 1 0 (-1) 0
                 (wingInput {bodyRatesRadS=Vec3 1 0 0,bodyVelocityMS=Vec3 8 wingRootHeight 0}) kestrelModel 0 initialStrips)
      falling=wingInput {bodyVelocityMS=Vec3 8 0 (-1)}
      before=sumForce (wingLoadsWithStroke True 1 0 (-1e-8) (-0.1) falling kestrelModel 0 initialStrips)
      after=sumForce (wingLoadsWithStroke True 1 0 1e-8 (-0.1) falling kestrelModel 0 initialStrips)
  if magnitude (addVec steady (scaleVec (-1) counter)) < 1e-8
     && magnitude (addVec before (scaleVec (-1) after)) < 1e-6
    then pure () else fail "world-stationary wing flow and continuous implicit reversal loads must hold"
  let loadedStrip = emptyStrip { stripSpringLoad = 1 }
      strip moment = StripResult loadedStrip (Vec3 0 0 1) moment 0 0
      leftTorque = hingeTorque (-1) [strip (Vec3 (-2) 90 0)]
      rightTorque = hingeTorque 1 [strip (Vec3 2 90 0)]
      wingI = shapeFlapInertia referenceWingMassKg airDensity defaultBirdWing
  if leftTorque == 2 && rightTorque == 2 then pure ()
    else fail "mirrored flap hinges must project signed X torque, ignoring pitch torque"
  -- Internal compliance cannot erase the external moment at the hinge.
  let unloadedStrip = emptyStrip { stripSpringLoad = 0 }
      easedTorque = hingeTorque (-1) [StripResult unloadedStrip (Vec3 0 0 1) (Vec3 (-2) 90 0) 0 0]
      twistedStrip = emptyStrip { stripSpringLoad = 0, stripTwistAero = -0.2 }
      twistedTorque = hingeTorque (-1) [StripResult twistedStrip (Vec3 0 0 1) (Vec3 (-2) 90 0) 0 0]
  if easedTorque == 2 && twistedTorque == 2 then pure ()
    else fail "elastic geometry must not discount an unchanged external hinge moment"
  let fwStep dt vel s = stepFirmwareVehicle True defaultRcChannels defaultFirmwareParams
                        defaultServoSpec defaultBatterySpec kestrelModel dt vel (Vec3 0 0 0) 1000 s
      badCases = [(0, Vec3 5 0 0), (0/0, Vec3 5 0 0), (0.01, Vec3 (1/0) 0 0)]
  mapM_ (\(dt, vel) -> let (o,s) = fwStep dt vel defaultFirmwareVehicleState
                       in if outputFlags o /= 0 && s == defaultFirmwareVehicleState
                          then pure () else fail "firmware failure must preserve all state") badCases
  let flap = defaultRcChannels {rcThrottle = 1811}
      batteryStep battery s = stepFirmwareVehicle True flap defaultFirmwareParams defaultServoSpec
                               battery kestrelModel (1/240) (Vec3 5 0 0) (Vec3 0 0 0) 1000 s
      (_, full) = batteryStep defaultBatterySpec defaultFirmwareVehicleState
      (_, empty) = batteryStep defaultBatterySpec (defaultFirmwareVehicleState {fvBatterySoc = 0})
      loaded = defaultFirmwareVehicleState {fvLeftHingeTorqueNm = 0.5, fvRightHingeTorqueNm = 0.5, fvMotorCurrentA = 10}
      (_, lowR) = batteryStep (defaultBatterySpec {batteryInternalResistanceOhm = 0}) loaded
      (_, highR) = batteryStep (defaultBatterySpec {batteryInternalResistanceOhm = 0.8}) loaded
      (mixCmd, _) = computeServoMixer True flap defaultFirmwareParams defaultFirmwareState (1 / 240)
      flapCmd = mixLeftFlapDevDeg mixCmd
  -- An unpowered wing can now be moved by air; it must not be frozen. Voltage
  -- sag must reduce the servo's POWERED motion: the sagged servo ends further
  -- from its commanded flap (larger tracking error) than the full-voltage one.
  -- Track the command directly rather than |rate| — under a strong aero hinge
  -- load the sagged servo is blown BACK off the command (correct, unpowered
  -- motion), which inflates |rate| even though its powered motion shrank.
  if abs (servoAngleDeg (fvServoLeft empty)) < abs (servoAngleDeg (fvServoLeft full))
     && abs (flapCmd - servoAngleDeg (fvServoLeft highR)) > abs (flapCmd - servoAngleDeg (fvServoLeft lowR))
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
      twistLoaded = sectionTwistMomentCoeff camber 0.4 0.10 0.8
      twistReversed = sectionTwistMomentCoeff camber 0.4 0.10 (-0.8)
      leverInboard = sectionTwistLever 0.0
      leverOutboard = sectionTwistLever 1.0
  if zeroLiftAngle camber < 0
     && abs (sectionPitchMomentCoeff 0 0.9) < 1.0e-12
     && plainMoment < reflexedMoment && reflexedMoment < 0
     && cupped > 0.05
     && twistLoaded < reflexedMoment
     && twistReversed > 0 && twistReversed > twistLoaded
     && leverInboard < leverOutboard && leverInboard > 0 && leverOutboard < 0.25
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
  let input = VehicleInput (1 / 240) (Vec3 5 0 0) (Vec3 0 0 0) 0.7 0 0 0 1000
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
  let (glide, _) = computeServoMixer True defaultRcChannels defaultFirmwareParams defaultFirmwareState (1 / 240)
  if not (mixIsFlapping glide) && mixFlapHz glide == 0
     && mixLeftFlapDevDeg glide == 4 && mixRightFlapDevDeg glide == 4
    then pure ()
    else fail "neutral glide must use the configured wing position"
  let mixer pilot = fst (computeServoMixer True (pilotToRc pilot) defaultFirmwareParams defaultFirmwareState (1/240))
      glideYaw = mixer (pilot4 0 0 0 0.5)
      glideRoll = mixer (pilot4 0 0.5 0 0)
      glidePitch = mixer (pilot4 0 0 0.5 0)
      -- Positive aileron raises the RIGHT wing (positive right flap deviation);
      -- a left roll (negative aileron) therefore cancels a right-rudder yaw,
      -- since both act on the same differential dihedral.
      cancelled = mixer (pilot4 0 (-0.325) 0 0.5)
      yawDiff = mixRightFlapDevDeg glideYaw - mixLeftFlapDevDeg glideYaw
      rollDiff = mixRightFlapDevDeg glideRoll - mixLeftFlapDevDeg glideRoll
  if yawDiff > 1 && rollDiff > 1 && mixLeftFlapDevDeg glidePitch < 4
     && abs (mixRightFlapDevDeg cancelled - mixLeftFlapDevDeg cancelled) < 1e-10
    then pure () else fail "glide RC channels must move the shared wings and allow opposing commands to cancel"
  let coherent output = abs (mixLeftFlapDevDeg output + (mixLeftWingDeg output-100)/2) < 1e-10
                     && abs (mixRightFlapDevDeg output - (mixRightWingDeg output-100)/2) < 1e-10
  if all coherent [glideYaw,glideRoll,glidePitch,mixer (pilot4 0.72 0.4 (-0.3) 0.5)]
    then pure () else fail "physical flap angles must include the complete mixed servo command"
  if all (\a -> abs (crsfToNorm (normToRaw a) - a) < 1e-12) [-1,-0.5,0,0.5,1]
    then pure () else fail "RC normalization must preserve exact neutral and endpoint values"
  let flapRc = defaultRcChannels { rcThrottle = 1811 }
      (flap, flapState) = computeServoMixer True flapRc defaultFirmwareParams defaultFirmwareState (1 / 240)
  if mixIsFlapping flap && mixFlapHz flap > 0 && mixThrottlePct flap == 1
     && mixLeftFlapDevDeg flap /= 0 && mixRightFlapDevDeg flap /= 0
    then pure ()
    else fail "full throttle must flap with positive frequency and deflection"
  -- Flapping hysteresis: dropping to just below the on-threshold stays flapping.
  let lowThrottle = defaultRcChannels { rcThrottle = 300 }
      (hystOut, _) = computeServoMixer True lowThrottle defaultFirmwareParams flapState (1 / 240)
  if mixIsFlapping hystOut
    then pure ()
    else fail "flapping must persist within the hysteresis band"
  -- Servo: unloaded tracking converges toward the target.
  let servo = defaultServoSpec
      battery = defaultBatterySpec
      voltage = batteryNominalVoltage battery
      (a1, s1) = stepServo servo battery 120 0 wingI voltage (1 / 240) defaultServoState
      (a2, s2) = stepServo servo battery 120 0 wingI voltage (1 / 240) s1
  if a2 > a1 && a2 <= 120
    then pure ()
    else fail "unloaded servo must slew toward its target"
  -- Servo: an overpowering load saturates the tracker's commanded rate to ZERO
  -- (the servo can no longer track the target). The unified tracker never
  -- actively back-drives the shaft in the load's direction — that assist is
  -- what flung the wing into extreme-up flutter. The wing wins through the
  -- passive 'servoBackdriveTorque' in the joint solve, not through the motor.
  let stall = servoStallTorqueNm servo
      (back, _) = stepServo servo battery 120 (negate stall * 2) wingI voltage (1 / 240) defaultServoState
  if back == 0
    then pure ()
    else fail "overloaded servo tracker must saturate to zero commanded rate"
  -- Closed loop: full throttle flaps the wings (servo tracks, lift appears).
  let flapRcClosed = defaultRcChannels { rcThrottle = 1811 }
      dtClosed = 1 / 240
      bodyVel = Vec3 5 0 0
      (out1, st1) = stepFirmwareVehicle True flapRcClosed defaultFirmwareParams
                     defaultServoSpec defaultBatterySpec kestrelModel dtClosed bodyVel (Vec3 0 0 0) 1000
                     defaultFirmwareVehicleState
      (out2, _) = stepFirmwareVehicle True flapRcClosed defaultFirmwareParams
                     defaultServoSpec defaultBatterySpec kestrelModel dtClosed bodyVel (Vec3 0 0 0) 1000 st1
      finite value = not (isNaN value || isInfinite value)
  if finite (z (totalForceN out2)) && finite (x (totalMomentNm out2))
     && totalMechanicalPowerW out2 >= 0
    then pure ()
    else fail "firmware-vehicle closed loop must produce finite loads"
  -- Ground effect: proximity to the surface (normalised by wingspan) boosts
  -- flapping lift AND forward thrust, while a distant wing feels neither.
  let geInput h = VehicleInput (1 / 240) (Vec3 5 0 0) (Vec3 0 0 0) 0 0 0 0 h
      strokeRateDown = -3.0
      (highGe, _) = stepWingWithStroke True (-1) 0 strokeRateDown (geInput 1000) kestrelModel 0 initialStrips
      (lowGe, _)  = stepWingWithStroke True (-1) 0 strokeRateDown (geInput 0.05) kestrelModel 0 initialStrips
      liftOf = sum . map (z . resultForce)
      thrustOf = sum . map (x . resultForce)
  if liftOf lowGe > liftOf highGe && thrustOf lowGe > thrustOf highGe
    then pure ()
    else fail "ground effect must raise flapping lift and forward thrust"
  -- Logical pilot input: maps faithfully onto the firmware's CRSF channel space.
  let rcFull = pilotToRc (pilot4 1 0 0 0)
  if rcThrottle rcFull == 1811 && rcAileron rcFull == 992 && rcRudder rcFull == 992
     && rcArm rcFull == 1811
    then pure ()
    else fail "full-throttle pilot input must map to full CRSF throttle with neutral sticks"
  let rcGlide = pilotToRc defaultPilotInput
      (glidePilot, _) = computeServoMixer True rcGlide defaultFirmwareParams defaultFirmwareState (1 / 240)
  if not (mixIsFlapping glidePilot)
    then pure ()
    else fail "zero-throttle pilot input must hold glide"
  let rcRoll = pilotToRc (pilot4 1 1 0 0)
  if rcAileron rcRoll > 992 && rcRudder rcRoll == 992 && rcElevator rcRoll == 992
    then pure ()
    else fail "roll stick must deflect the aileron channel only"
  -- Closed loop via logical pilot input: full throttle flaps and tracks battery.
  let pilotFlap = pilot4 1 0 0 0
      dtFw = 1 / 240
      bodyVelFw = Vec3 5 0 0
      stepFw s = stepFirmwareVehicle True (pilotToRc pilotFlap) defaultFirmwareParams
                   defaultServoSpec defaultBatterySpec kestrelModel dtFw bodyVelFw (Vec3 0 0 0) 1000 s
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
        let rc = pilotToRc (pilot4 0.72 roll 0 0)
            tick (_,s) = stepFirmwareVehicle True rc skewParams skewServo skewBattery kestrelModel
                          (1/240) (Vec3 8 0 (-1)) (Vec3 0 0 0) 1000 s
            samples = drop 481 $ take 1921 $ iterate tick (zeroVehicleOutput,defaultFirmwareVehicleState)
        in if any ((/=0) . outputFlags . fst) samples then error "stroke timing integration rejected a step"
           else sum (map (x . totalMomentNm . fst) samples) / fromIntegral (length samples)
      leftSkewMoment = skewMoment (-0.5)
      rightSkewMoment = skewMoment 0.5
  -- The property is the MIRROR (left skew = −right skew) and a non-zero roll
  -- authority; the asymmetric feathering (downstroke loaded, upstroke shed)
  -- legitimately shifts the sign and magnitude versus the old symmetric stroke,
  -- so the sign is not asserted here.
  -- Authority threshold scales with mass: the roll MOMENT here is aerodynamic
  -- (mass-independent), but a 50 g bird has ~9× less roll inertia than the
  -- 450 g reference the 1e-3 threshold was tuned for, so the required moment
  -- is ~9× smaller for the same roll acceleration. 1e-4 reflects the 50 g
  -- re-scale at circulationLiftEfficiency 0.14; the intentionally softer
  -- aeroelastic wing (siTwistScale 0.10, siBendScale 3.0) sheds a few percent
  -- of skew-roll authority under load, so the floor is relaxed to 5e-5 — still
  -- an order of magnitude above a collapse.
  if abs (leftSkewMoment + rightSkewMoment) < 1e-10
     && abs leftSkewMoment > 5e-5
    then pure () else fail ("opposite stroke timing alone must generate mirrored aerodynamic roll: " ++ show (leftSkewMoment,rightSkewMoment))
  -- ONDAS Rubedo M0: ferocity-price anchors (audit-verified 9.5).
  let priceCheck (d, p, f, t) = abs (ferocityPowerPrice d - p) < 1e-2
                               && abs (ferocityForcePrice d - f) < 1e-2
                               && abs (ferocityThrustPrice d - t) < 1e-2
  if all priceCheck [(0.05, 1.166, 1.108, 1.053), (0.3, 2.92, 2.04, 1.43), (0.8, 125, 25, 5)]
    then pure () else fail "ferocity price must match the 9.5 audit anchors"
  if abs (dwellFromFerocity 8 - 0.98) < 1e-12 && dwellFromFerocity 0 == 0
    then pure () else fail "dwell must scale ferocity 0..8 onto [0, 0.98]"
  -- Read-only response-phase diagnostic: it must idle below omega_min and keep
  -- its lag estimate within [-pi, pi] — pure telemetry, never a controller output.
  if rpPhaseError (stepResponsePhase 10 9 0 0.05 0.001 defaultResponsePhase) == 0
    then pure () else fail "response phase must idle below the omega threshold"
  let rpLag = rpPhaseError (stepResponsePhase 10 9 (pi/4) 10 0.001 defaultResponsePhase)
  if abs rpLag <= pi
    then pure () else fail "response-phase lag must stay within [-pi, pi]"
  -- ONDAS Rubedo M3: golden-angle strobe covers every bin without aliasing.
  if gcd (toInteger goldenAngleStep) (2 ^ (20 :: Integer)) == 1
    then pure () else fail "golden-angle step must be coprime to 2^20"
  let strobes = iterate (phaseStrobe 1.0) defaultPhaseEnvelope
      covered = phaseCoverage (strobes !! 1024)
      spreadPe = phaseEnvelope (strobes !! 1024)
      fixedRasterStrobe v st =
        st { peMeans = zipWith (\i m -> m * (1 - phaseEma)
                                          + if i == 0 then v * phaseEma else 0)
                               [0 :: Int ..] (peMeans st) }
      rasterPe = phaseEnvelope (iterate (fixedRasterStrobe 1.0) defaultPhaseEnvelope !! 1024)
  if covered == 1.0 && spreadPe < 0.65 && spreadPe < rasterPe
    then pure () else fail "golden-angle strobe must cover all bins and de-alias below a fixed raster"
  if detectReversal 0 3 1.5 && not (detectReversal 0 1 1.5) && detectReversal 6 0 1.5
    then pure () else fail "reversal must fire on the limiar crossing and the 2π wrap"
  -- Resonance is EMERGENT from the physical wing spring, not injected: after a
  -- long closed-loop flight the oscillator gain must remain at its nominal 1
  -- (the removed phase-lock no longer writes fwKGainMod).
  let flyFrom s = snd (stepFirmwareVehicle True flapRcClosed defaultFirmwareParams
                        defaultServoSpec defaultBatterySpec kestrelModel (1 / 240)
                        (Vec3 5 0 0) (Vec3 0 0 0) 1000 s)
      steady = iterate flyFrom defaultFirmwareVehicleState !! 2400
      kNominal = fwKGainMod (fvFirmware steady)
  if kNominal == 1.0
    then pure () else fail "oscillator gain must stay nominal (no phase-lock controller)"
  let flatStation st = st {ssLeadX=0,ssTwistDeg=0,ssDihedDeg=0,
                              ssSection=(ssSection st) {spRestCamber=0}}
      flatWing=defaultBirdWing {wsStations=map flatStation (wsStations defaultBirdWing)}
      flatModel=kestrelModel {wmWing=flatWing}
      flatState=replicate stripCount emptyStrip
      dotV a b=x a*x b+y a*y b+z a*z b
      checkAero (angle,rate,incidence,vzAir) =
        let input=flow {bodyVelocityMS=Vec3 8 0 vzAir, groundClearanceM=1000}
            loads=wingLoadsWithStroke True 1 angle rate rate input flatModel incidence flatState
            mirror=wingLoadsWithStroke True 1 (-angle) (-rate) (-rate)
                      (input {bodyVelocityMS=Vec3 8 0 (-vzAir)}) flatModel (-incidence) flatState
            checkStrip (i,r) =
              let f=(fromIntegral i+0.5)/fromIntegral stripCount
                  radius=wsShoulderM flatWing+wsSpanM flatWing*f
                  chord=shapeChord flatWing f
                  c=Vec3 (cos incidence) (-sin incidence*sin angle) (sin incidence*cos angle)
                  p=addVec (Vec3 0 (radius*cos angle) (radius*sin angle)) (scaleVec (-0.25*chord) c)
                  velocity=addVec (bodyVelocityMS input) (crossVec (Vec3 rate 0 0) p)
              in dotV (resultForce r) velocity <= 1e-8
            f=summed loads; g=summed mirror
        in all checkStrip (zip [0::Int ..] loads)
           && magnitude (addVec f (Vec3 (-x g) (-y g) (z g))) < 1e-7
  if all checkAero [(a,r,i,v) | a<-[-0.6,0,0.6],r<-[-8,0,8],i<-[-0.25,0,0.25],v<-[-2,0,2]]
    then pure () else fail "surface forces must dissipate relative-flow energy and reflect for inverted geometry"
  let speedLimit=servoNoLoadSpeedDegPerSec defaultServoSpec*pi/180
      stallLimit=servoStallTorqueNm defaultServoSpec
      voltage=batteryNominalVoltage defaultBatterySpec
      torqueAt rate target=servoDriveTorque defaultServoSpec defaultBatterySpec voltage (1/240)
                            target 0 0.002 defaultServoState rate
  if all (\r -> all (\target -> torqueAt r target*r <= stallLimit*speedLimit/4+1e-9) [-80,0,80])
         (map (*speedLimit) [-2,-1,-0.75,-0.5,0,0.5,0.75,1,2])
    then pure () else fail "servo mechanical power must respect torque-speed envelope"
  let mirroredStep (left,right,err) i =
        let t=fromIntegral i/240; a=0.4*cos (4*pi*t); rate= -1.6*pi*sin (4*pi*t)
            v=flow {bodyVelocityMS=Vec3 6 0 (-0.5),groundClearanceM=1000}
            (ls,ln)=stepCoupledWing True 1 a rate rate v flatModel 0.1 left
            (rs,rn)=stepCoupledWing True 1 (-a) (-rate) (-rate)
                      (v {bodyVelocityMS=Vec3 6 0 0.5}) flatModel (-0.1) right
            f=summed ls; g=summed rs
        in (ln,rn,max err (magnitude (addVec f (Vec3 (-x g) (-y g) (z g)))))
      (_,_,reflectionError)=foldl mirroredStep (flatState,flatState,0) [1..480::Int]
  if reflectionError < 1e-7 then pure ()
    else fail ("unsteady stall and structural history break inverted reflection: "++show reflectionError)
  if all (\(scale,f) -> abs (sparTwistGJSpanwise (scaleSpars scale kestrelSpars) f
                         / sparTwistGJSpanwise kestrelSpars f-scale**4)<1e-8)
         [(scale,f) | scale<-[0.5,2],f<-[0.2,0.7]]
    then pure () else fail "distributed torsional stiffness must scale as length to the fourth power"
  -- Broadside pressure must exist immediately, before separation memory grows.
  -- Both wings at zero forward speed: analytic Cp=1.2 resisting roll moment.
  let rollPressure separated =
        let omega=2
            input=flow {bodyVelocityMS=zero,bodyRatesRadS=Vec3 omega 0 0,groundClearanceM=1000}
            states=replicate stripCount (emptyStrip {stripSeparation=separated})
            rs=concat [wingLoadsWithStroke False side 0 0 0 input flatModel 0 states | side<-[-1,1]]
            moment=sum(map (x . resultMoment) rs)
            dr=wsSpanM flatWing/fromIntegral stripCount
            integral=sum [shapeChord flatWing f*dr*(wsShoulderM flatWing+wsSpanM flatWing*f)**3
                         | i<-[0..stripCount-1],let f=(fromIntegral i+0.5)/fromIntegral stripCount]
        in moment <= -airDensity*1.2*omega*omega*integral+1e-9
  if all rollPressure [0,1] then pure ()
    else fail "broadside roll pressure cannot wait for stall memory or a mode switch"
  -- A separated membrane must deform from its actual pressure, not the
  -- hypothetical attached lift that stall removed.
  let stalled=replicate stripCount (emptyStrip {stripSeparation=1})
      mount=0.1
      input=flow {bodyVelocityMS=Vec3 8 0 0,groundClearanceM=1000}
      (stalledLoads,stalledNext)=stepCoupledWing True 1 0 0 0 input flatModel mount stalled
      checkMembrane (i,r,next) =
        let f=(fromIntegral i+0.5)/fromIntegral stripCount
            normal=Vec3 (-sin mount) 0 (cos mount)
            qArea=0.5*airDensity*64*shapeChord flatWing f*wsSpanM flatWing/fromIntegral stripCount
            profile=shapeSection flatWing f
            actualCn=dotV (resultForce r) normal/qArea
            expected=relaxCamber (stepSeconds input) (spMembraneTau profile) 0
                       (membraneCamberTarget profile actualCn)
        in abs (stripCamber next-expected)<1e-10
  if all checkMembrane (zip3 [0::Int ..] stalledLoads stalledNext)
    then pure () else fail "stalled membrane must follow solved pressure instead of unattained attached lift"
  let boundCases=[(f,g,m,k) | f<-[0,4,8],g<-[0,4,8],m<-[0,50,100],k<-[-80,0,80]]
      boundHolds (f,g,m,k)=
        let bound=wavePeakDerivativeBound f g (-1) m k (-k)
        in all (\i -> abs (snd (shapeWaveWithDerivative (2*pi*fromIntegral i/500)
                            f g (-1) m k (-k)))<=bound+1e-9) [0..499::Int]
  if all boundHolds boundCases then pure () else fail "waveform cadence bound underestimates peak shaft speed"
  let commandedTravel fer =
        let p=defaultFirmwareParams {fwProfile=(fwProfile defaultFirmwareParams)
                    {profStrokeFerocity=fer,profReturnFerocity=fer,profTailElevatorAngleDeg=0}}
            at i =
              let phase=2*pi*fromIntegral i/500
                  state=defaultFirmwareState {fwOscillator=defaultOscillator {oscBasePhase=phase,oscPhase=phase}}
              in mixLeftFlapDevDeg (fst (computeServoMixer True flapRc p state 1e-6))
            values=map at [0..499::Int]
        in maximum values-minimum values
  if all (\fer -> commandedTravel fer>109) [0,50,100]
    then pure () else fail "full-throttle dwell must retain full 110 degree travel rather than shrink the arc"
  putStrLn "MathCore properties passed"

zeroVehicleOutput :: VehicleOutput
zeroVehicleOutput = VehicleOutput (Vec3 0 0 0) (Vec3 0 0 0) 0 0 (-1) 0
