{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

module Born2Flap.Math.Vehicle
  ( VehicleInput(..)
  , VehicleOutput(..)
  , VehicleState
  , vehicleOscillator
  , defaultVehicle
  , stepVehicle
  , StripState(..)
  , StripResult(..)
  , stripCount
  , wingRootHeight
  , airDensity
  , surfaceSkinCd
  , surfacePressure
  , finiteWingLiftSlope
  , inducedDragCoefficient
  , emptyStrip
  , initialStrips
  , stepWingWithStroke
  , wingLoadsWithStroke
  , prepareWingLoads
  , WingSystem(..), prepareWingSystem, coupleWingPressure
  , stepCoupledWing
  , addVec
  , crossVec
  , scaleVec
  , magnitude
  , zeroVec
  , vx, vy, vz
  , radians
  ) where

import Data.List (zipWith4, zip4)

import Born2Flap.Math.RollDynamics (solveImplicitSystem)
import Born2Flap.Math.LiftField
import Born2Flap.Math.Types
import Born2Flap.Math.Simulation
import Born2Flap.Math.Wing (crossflowBaseline, relaxSeparation)
import Born2Flap.Math.Model (WingModel(..), kestrelModel)
import Born2Flap.Math.Planform
  ( WingShape(..), SpanStation(..), shapeStationsAt, shapeChord
  , shapeSweepRad, shapeTwistRad, shapeDihedralRad, shapeAspectRatio, shapeSection, referenceWingMassKg )
import Born2Flap.Math.Section
  ( SectionProfile(..), zeroLiftAngle, sectionPitchMomentCoeff
  , membraneCamberTarget, relaxCamber )
import Born2Flap.Math.Structure
  ( SparInventory, integrateFlapBeam, integrateTwist, relaxDeflection
  , sparBendEISpanwise, sparTwistGJSpanwise )
import Born2Flap.Math.Waveform
  ( OscillatorState(..), defaultOscillator, advanceOscillator
  , limiarFromFerocities, shapeWaveWithDerivative )

-- | Ambient density and provisional low-Reynolds-number polar calibration.
-- No separate propulsion gain: stroke motion enters through relative velocity.
airDensity, circulationLiftEfficiency :: Double
airDensity = 1.05
circulationLiftEfficiency = 0.85

-- Shared membrane/plate coefficients for wings and ruddervators. Effective
-- low-Re calibration, not measured material data. Pressure is always passive.
surfaceSkinCd :: Double
surfaceSkinCd = 0.009

-- Flat-plate normal-pressure coefficient for wings and ruddervators.
surfacePressureCp :: Double
surfacePressureCp = 1.8

-- Low-speed normal-pressure floor [m/s]. A purely quadratic v·|v| pressure
-- vanishes as the relative speed tends to zero, so a slowly-drifting surface
-- (slow aileron, held glide dihedral, a body just starting to roll) slips
-- through the air with no reaction. v·√(v²+v0²) is linear below v0 and
-- quadratic above it: it anchors the wing at a crawl without distorting
-- flapping or forward glide. Raised 0.5 → 1.0 so the roll-rate coupling of a
-- held differential stroke has a real grip below 1 m/s of normal motion.
lowSpeedNormalFloor :: Double
lowSpeedNormalFloor = 1.0

-- Shared wing/tail normal pressure; circulation supplies forward-flow lift.
surfacePressure :: Double -> Double -> Double
surfacePressure area normalSpeed =
  -0.5 * airDensity * area * surfacePressureCp
    * normalSpeed * sqrt (normalSpeed*normalSpeed + lowSpeedNormalFloor*lowSpeedNormalFloor)

finiteWingLiftSlope :: Double -> Double
finiteWingLiftSlope aspect = 2*pi*aspect/(aspect+2)*circulationLiftEfficiency

inducedDragCoefficient :: Double -> Double -> Double
inducedDragCoefficient aspect cl = cl*cl/(pi*0.82*max 0.01 aspect)

-- Smoothing of the legacy display diagnostic only; not a physical time constant.
springLoadTau :: Double
springLoadTau = 0.05

groundEffectLiftGain, groundEffectDragRelief :: Double
groundEffectLiftGain = 0.30
groundEffectDragRelief = 0.55

-- | Strength 0..1 of ground effect at a strip height @h@ above the surface,
-- normalised by the total wingspan @b@. 1 on the deck, ~0.08 by one span, so
-- the effect lives on the scale the wing actually spans.
groundEffectFactor :: Double -> Double -> Double
groundEffectFactor h b
  | b <= 0.01 = 0
  | h <= 0    = 1
  | otherwise = exp (-2.5 * h / b)

data VehicleInput = VehicleInput
  { stepSeconds :: !Double
  , bodyVelocityMS :: !Vec3
  , bodyRatesRadS :: !Vec3
  , throttleCommand :: !Double
  , rollCommand :: !Double
  , pitchCommand :: !Double
  , yawCommand :: !Double
  , groundClearanceM :: !Double  -- ^ body-origin height above the surface below [m] (ground effect)
  , bodyUpB :: !Vec3             -- ^ world-up unit vector expressed in the BODY frame (ABI v13)
  } deriving stock (Eq, Show)

data VehicleOutput = VehicleOutput
  { totalForceN :: !Vec3
  , totalMomentNm :: !Vec3
  , totalMechanicalPowerW :: !Double
  , maxSeparation :: !Double
  , firstStalledElement :: !Int
  , outputFlags :: !Int
  } deriving stock (Eq, Show)

data StripState = StripState
  { stripSeparation :: !Double
  , stripPreviousAlpha :: !Double
  , stripPreviousNormalVelocity :: !Double
  , stripLevStrength :: !Double
  , stripCamber :: !Double   -- ^ current camber @f/c@ (rest camber + membrane billow)
  , stripBendM :: !Double    -- ^ out-of-plane flap deflection [m] (tip up +)
  , stripBendSlope :: !Double  -- ^ flap deflection slope [rad]
  , stripTwistAero :: !Double  -- ^ aeroelastic twist [rad] (nose-up +)
  , stripSpringLoad :: !Double  -- ^ normalized deflection diagnostic 0..1; not energy and never a force multiplier
  , stripBendRateMS :: !Double
  , stripTwistRateRadS :: !Double
  , stripBendSlopeRate :: !Double -- ^ d(deflection slope)/dt, for AC point velocity
  , stripPressureField :: !PressureField
  , stripPosition :: !Vec3    -- ^ deformed force application point in body coordinates
  , stripNormal :: !Vec3      -- ^ deformed surface normal
  , stripLift :: !Vec3        -- ^ circulation force, perpendicular to local flow
  , stripPressure :: !Vec3    -- ^ instantaneous pressure plus relaxing air patch
  , stripAirVelocity :: !Vec3 -- ^ complete velocity through the sampled local wind
  , stripWindDelta :: !Vec3   -- ^ local wind minus body-origin wind, body frame
  } deriving stock (Eq, Show)
emptyStrip :: StripState
emptyStrip = StripState 0 0 0 0 0 0 0 0 0 0 0 0 emptyPressureField zeroVec (Vec3 0 0 1) zeroVec zeroVec zeroVec zeroVec

data VehicleState = VehicleState
  { vehicleOscillator :: !OscillatorState
  , leftStrips :: ![StripState]
  , rightStrips :: ![StripState]
  } deriving stock (Eq, Show)

data StripResult = StripResult
  { resultState :: ~StripState
  , resultForce :: !Vec3
  , resultMoment :: !Vec3
  , resultPower :: !Double
  , resultSectionMoment :: !Double  -- ^ twist-driving moment about the LE elastic axis [N·m] (nose-up +)
  , resultBendMoment :: !Double     -- ^ moment conjugate to the local bend angle
  , resultFrame :: !(Vec3,Vec3,Vec3,Vec3,Vec3,Vec3) -- position, chord, span, normal, flow, flap velocity
  } deriving stock (Eq, Show)

stripCount :: Int
stripCount = 16


-- | Strips initialised to the rest camber of the default wing (so the membrane
-- model starts from the built-in camber line, not a flat plate).
initialStrips :: [StripState]
initialStrips =
  [ emptyStrip {stripCamber = spRestCamber (shapeSection (wmWing kestrelModel) fraction)}
  | i <- [0 .. stripCount - 1]
  , let fraction = (fromIntegral i + 0.5) / fromIntegral stripCount
  ]

defaultVehicle :: VehicleState
defaultVehicle = VehicleState defaultOscillator initialStrips initialStrips

stepVehicle :: VehicleInput -> VehicleState -> (VehicleOutput, VehicleState)
stepVehicle input state =
  case runSimulation transaction state of
    Left flag | flag==2 && stepSeconds input>1/7680 && stepSeconds input<=0.05
                && all finite (vecValues(bodyVelocityMS input)++vecValues(bodyRatesRadS input)
                     ++[throttleCommand input,rollCommand input,pitchCommand input,yawCommand input]) ->
      let half=input {stepSeconds=stepSeconds input/2}
          (a,middle)=stepVehicle half state
          (b,final)=stepVehicle half middle
          average u v=scaleVec 0.5 (addVec u v)
      in if outputFlags a/=0 || outputFlags b/=0 then (zeroOutput flag,state)
         else (b {totalForceN=average(totalForceN a)(totalForceN b)
                 ,totalMomentNm=average(totalMomentNm a)(totalMomentNm b)
                 ,totalMechanicalPowerW=0.5*(totalMechanicalPowerW a+totalMechanicalPowerW b)
                 ,maxSeparation=max(maxSeparation a)(maxSeparation b)},final)
              | otherwise -> (zeroOutput flag, state)
    Right result -> result
  where
    transaction = do
      old <- getState
      let (output, next) = stepVehicleUnchecked input old
      if outputFlags output /= 0
        then abort (outputFlags output)
        else putState next >> pure output

stepVehicleUnchecked :: VehicleInput -> VehicleState -> (VehicleOutput, VehicleState)
stepVehicleUnchecked input state
  | not (finite dt) || dt <= 0 || dt > 0.05 = (zeroOutput 1, state)
  | not (all finite (vecValues (bodyVelocityMS input) ++ vecValues (bodyRatesRadS input)
      ++ [throttleCommand input, rollCommand input, pitchCommand input, yawCommand input])) =
      (zeroOutput 2, state)
  | otherwise =
      let throttle = clamp 0 1 (throttleCommand input)
          rates = bodyRatesRadS input
          -- Rate damping is deliberately inside the deterministic math core.
          rollMix = clamp (-1) 1 (rollCommand input - 0.22 * vx rates)
          pitchMix = clamp (-1) 1 (pitchCommand input - 0.18 * vy rates)
          yawMix = clamp (-1) 1 (yawCommand input - 0.16 * vz rates)
          -- Waveform drive: throttle sets the beat (cadence) and base ferocity;
          -- pitch biases stroke vs return ferocity (thrust timing), matching the
          -- firmware's balance→ferocity-asymmetry coupling.
          cadenceTarget = 2 * pi * (1.2 + 3.8 * throttle)
          baseFerocity = 1 + 7 * throttle
          strokeFerocity = clamp 0 8 (baseFerocity + 0.6 * pitchMix)
          returnFerocity = clamp 0 8 (baseFerocity - 0.6 * pitchMix)
          limiarShared = limiarFromFerocities strokeFerocity returnFerocity
          (phase, nextOscillator) = advanceOscillator cadenceTarget 1 0 dt (vehicleOscillator state)
          (pulse, pulseDeriv) = shapeWaveWithDerivative phase strokeFerocity returnFerocity limiarShared 0 0 0
          pulseRate = pulseDeriv * oscCadence nextOscillator
          (leftResults, leftNext) = stepWing (-1) pulse pulseRate throttle rollMix input (leftStrips state)
          (rightResults, rightNext) = stepWing 1 pulse pulseRate throttle rollMix input (rightStrips state)
          wingResults = leftResults ++ rightResults
          wingForce = foldr (addVec . resultForce) zeroVec wingResults
          wingMoment = foldr (addVec . resultMoment) zeroVec wingResults
          speed = magnitude (bodyVelocityMS input)
          bodyDrag = scaleVec (-0.5 * airDensity * 0.0004 * speed) (bodyVelocityMS input)
          tailArm = Vec3 (-0.48) 0 0
          tailVelocity = addVec (bodyVelocityMS input) (crossVec rates tailArm)
          tailQArea = 0.5 * airDensity * 0.0015 * magnitude tailVelocity * magnitude tailVelocity
          -- Fixed tailplane incidence: a small, always-on DOWN force (negative z
          -- lift) proportional to dynamic pressure, providing pitch trim; the
          -- stick adds control lift on top of it.
          tailTrimDownforce = -0.08 * tailQArea
          tailForce = addVec (scaleVec tailQArea (Vec3 0 (-yawMix) (-pitchMix)))
                             (Vec3 0 0 tailTrimDownforce)
          tailMoment = crossVec tailArm tailForce
          force = addVec wingForce (addVec bodyDrag tailForce)
          moment = addVec wingMoment tailMoment
          separations = map (stripSeparation . resultState) wingResults
          maximumSeparation = maximum (0 : separations)
          stalled = firstIndex (> 0.60) separations
          power = sum (map resultPower wingResults)
          flags = if all finite (vecValues force ++ vecValues moment ++ [power]) then 0 else 2
          output = if flags == 0
                     then VehicleOutput force moment power maximumSeparation stalled flags
                     else zeroOutput flags
      in (output, VehicleState nextOscillator leftNext rightNext)
  where
    dt = stepSeconds input

stepWing :: Double -> Double -> Double -> Double -> Double -> VehicleInput -> [StripState]
         -> ([StripResult], [StripState])
stepWing side pulse pulseRate throttle rollMix input states =
   let amplitude = radians (12 + 38 * throttle) * clamp 0.55 1.35 (1 - side * 0.22 * rollMix)
       stroke = amplitude * pulse
       strokeRate = amplitude * pulseRate
   in stepWingWithStroke True side stroke strokeRate input kestrelModel 0 (replicate stripCount 0) 0 states

-- | Step one wing given the *actual* flap angle [rad] and flap rate [rad/s],
-- independent of how they were produced (firmware mixer + servo, or the
-- legacy throttle drive). This is the primitive the firmware-emulation loop
-- uses to close the aero→servo-load feedback. @supports@ carries the per-strip
-- static weight-support share [N] (zero on the legacy path).
stepWingWithStroke :: Bool -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [Double] -> Double -> [StripState]
                   -> ([StripResult], [StripState])
stepWingWithStroke flapping side stroke strokeRate input model incidenceRad supports centre states =
   stepCoupledWing flapping side stroke strokeRate strokeRate 0 input model incidenceRad states supports centre 0

-- Force-only evaluation for the implicit roll Jacobian. Structural integration
-- and separation transport only modify next state; commit those once after
-- solving, rather than doing a full beam solve at every trial velocity.
wingLoadsWithStroke :: Bool -> Double -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [StripState] -> [Double] -> Double -> Double -> [StripResult]
wingLoadsWithStroke flapping side stroke rate phaseRate input model incidenceRad states supports centre supportPosDev =
  zipWith3 (\i s sup -> stepStrip True phaseRate flapping side stroke input incidenceRad (wmWing model) i s sup centre supportPosDev (vx (bodyRatesRadS input)) rate 0) [0..] states supports

-- Geometry and material history are shared by all trial velocities. The host
-- solves these four elastic coordinates together with body roll and hinges.
data WingSystem = WingSystem
  { wingInitialRates :: (Double,Double)
  , wingTrialLoads :: Double -> Double -> (Double,Double) -> [StripResult]
  , wingElasticResidual :: (Double,Double) -> [StripResult] -> (Double,Double)
  , wingCommit :: [StripResult] -> ([StripResult],[StripState])
  }

prepareWingSystem :: Bool -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [StripState] -> [Double] -> Double -> Double -> WingSystem
prepareWingSystem flapping side stroke phaseRate input model incidenceRad states supports centre supportPosDev =
  let wp = wmWing model
      stations = shapeStationsAt wp stripCount
      aspect = shapeAspectRatio wp
      (initial,deform,residual,finish) = prepareStructure side stroke (wmSpars model) wp input states
      raw rollRate rate elastic = zipWith3
        (\station s sup -> stepStripPrepared station aspect True phaseRate flapping side stroke input incidenceRad wp 0 s sup centre supportPosDev rollRate rate 0)
        stations (deform elastic) supports
      commit rs = let results=transportSeparation side input wp (map finish rs)
                  in (results,map resultState results)
  in WingSystem initial raw residual commit

prepareWingLoads :: Bool -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [StripState] -> [Double] -> Double -> Double
                 -> Double -> Double -> Double -> [StripResult]
prepareWingLoads flapping side stroke phaseRate input model incidenceRad states supports centre supportPosDev =
  let system=prepareWingSystem flapping side stroke phaseRate input model incidenceRad states supports centre supportPosDev
      (vb,vt)=wingInitialRates system
  in \rollRate rate _common ->
    let loads b t=wingTrialLoads system rollRate rate (b,t)
        residual [b,t]=let (rb,rt)=wingElasticResidual system (b,t) (loads b t) in [rb,rt]
        residual _=error "elastic coordinate count"
        solved=solveImplicitSystem residual [vb,vt]
    in case solved of Just [b,t] -> loads b t; _ -> loads (0/0) (0/0)

-- Commit aero memory/structure after the implicit solve using the same loads
-- as its residual. Hysteretic state transitions are not Newton unknowns.
stepCoupledWing :: Bool -> Double -> Double -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [StripState] -> [Double] -> Double -> Double
                -> ([StripResult], [StripState])
stepCoupledWing flapping side stroke rate phaseRate commonRate input model incidenceRad states supports centre supportPosDev =
  let wp=wmWing model
      raw=prepareWingLoads flapping side stroke phaseRate input model incidenceRad states supports centre supportPosDev (vx (bodyRatesRadS input)) rate commonRate
      results=transportSeparation side input wp raw
  in (results,map resultState results)

-- High wing / low centre of mass. The side-force lever contributes natural
-- lateral stability; it is not an attitude-dependent restoring torque.
wingRootHeight :: Double
wingRootHeight = 0.04

stepStrip :: Bool -> Double -> Bool -> Double -> Double -> VehicleInput -> Double -> WingShape -> Int -> StripState -> Double -> Double -> Double -> Double -> Double -> Double -> StripResult
stepStrip implicitLoads phaseRate flapping side stroke input incidenceRad wp index =
  stepStripPrepared (shapeStationsAt wp stripCount !! index) (shapeAspectRatio wp)
    implicitLoads phaseRate flapping side stroke input incidenceRad wp index

-- Planform interpolation is invariant during the nonlinear solve. Share it
-- across all trial elastic velocities instead of sorting/resampling the
-- outline again for each Jacobian column.
stepStripPrepared :: SpanStation -> Double -> Bool -> Double -> Bool -> Double -> Double -> VehicleInput -> Double -> WingShape -> Int -> StripState -> Double -> Double -> Double -> Double -> Double -> Double -> StripResult
{-# NOINLINE stepStripPrepared #-}
stepStripPrepared station aspectRatio implicitLoads _phaseRate _flapping side stroke input incidenceRad wp _index old _supportN _centre _supportPosDev =
  let !dt = stepSeconds input
      !fraction = spanFraction station
      !spanM = wsSpanM wp
      !dr = spanM / fromIntegral stripCount
      -- Spanwise lever arm from the shoulder hinge: the wing root sits at the
      -- shoulder distance outboard of the body centreline, then the wing's own
      -- span runs root→tip.
      !radius = wsShoulderM wp + spanM * fraction
      !chord = spanChord station
      !twist = spanTwist station
      -- Horizontal servo mount: the flap hinge is yawed by mountRad in the
      -- horizontal plane. A stroke about that yawed hinge decomposes into a
      -- vertical (dihedral) component ∝ cos mountRad and a horizontal (sweep)
      -- component ∝ sin mountRad. The stroke-proportional sweep term is what
      -- makes the section's angle of attack vary with its up/down position —
      -- a position-dependent "drag rudder". The arm angle rotates the LE spar
      -- at the arm to cancel the mount's neutral-sweep shift (armRad = mountRad
      -- ⇒ neutral sweep unchanged). mountRad/armRad are zero for the kestrel,
      -- so it keeps pure parallel flapping.
      !mountRad = radians (wsMountAngleDeg wp)
      !armRad = radians (wsArmAngleDeg wp)
      !dihedral = stroke * cos mountRad + spanDihed station
      !profile = spanSection station
      !camberPrev = stripCamber old
      !aeroTwist = stripTwistAero old
      !bend = stripBendM old
      !bendSlope = stripBendSlope old
      -- Flex reorients the surface: the out-of-plane flap slope adds to the
      -- geometric dihedral, so a loaded tip (bent up) flies at a higher local
      -- dihedral than the stroke alone would set. The force axes (normal/chord/
      -- span) use this local dihedral; the geometric 'dihedral' still sets the
      -- physical strip position and lever arms.
      !dihedralLocal = dihedral + atan bendSlope
      -- Ground effect: proximity to the surface below, normalised by the total
      -- wingspan (2 × half-span). Each strip sees its OWN flap height, so the
      -- downstroke pushes the tip into the cushion and the upstroke lifts it
      -- back out — the effect is strongest at the bottom of the beat.
      !stripHeightM = wingRootHeight + radius * sin dihedral + bend*cos dihedral
      !ge = groundEffectFactor (groundClearanceM input + stripHeightM) (2 * wsSpanM wp)
      !velocity = addVec (bodyVelocityMS input) (scaleVec (-1) (stripWindDelta old))
      -- Use section velocity through air throughout (not a mixture of air
      -- velocity and body velocity). Drag must do negative work. The normal
      -- rotates with the flap; positive stroke raises BOTH wings.
      -- Aerodynamic centre sits at the quarter-chord, AFT of the shoulder
      -- hinge (the root LE is at x=0, chord runs aft, so AC x is negative).
      -- Placing the lift force at this lever arm gives the wing the nose-down
      -- static moment a real bird has; with the lift pinned at x=0 (the CG),
      -- the wing contributes no pitch moment and the tail downforce alone
      -- trims the bird nose-up regardless of CG slider position.
      !leadingEdge = addVec (Vec3 (-spanLeadX station)
                         (side * radius * cos dihedral) (wingRootHeight + radius * sin dihedral))
                         (scaleVec bend (Vec3 0 (-side*sin dihedral) (cos dihedral)))
      !position = addVec leadingEdge (scaleVec (-0.25 * chord) chordAxis)
      !normal0 = Vec3 0 (-side * sin dihedralLocal) (cos dihedralLocal)
      -- Resolve flow in the swept section plane. Ignoring sweep here removed
      -- its differential lift in sideslip, despite it being in the planform.
      !sweep = spanSweep station + (mountRad - armRad) + sin mountRad * stroke
      !chordAxis0 = Vec3 (cos sweep) (side * sin sweep * cos dihedralLocal) (sin sweep * sin dihedralLocal)
      !spanAxis = Vec3 (-sin sweep) (side * cos sweep * cos dihedralLocal) (cos sweep * sin dihedralLocal)
      -- Mount incidence, geometric twist and elastic twist rotate the local
      -- surface frame exactly once. The hinge stays on body X; point velocity
      -- below is omega cross the actual deformed application-point offset.
      !ci = cos (incidenceRad + twist + aeroTwist)
      !si = sin (incidenceRad + twist + aeroTwist)
      !chordAxis = addVec (scaleVec ci chordAxis0) (scaleVec si normal0)
      !normal    = addVec (scaleVec ci normal0) (scaleVec (-si) chordAxis0)
      -- Flap velocity is the wing surface's motion through the air about the
      -- shoulder hinge. It exists in BOTH flapping and glide: in glide a
      -- differential-dihedral (roll) command rotates the wing, so the surface
      -- genuinely sweeps through the air and RESISTS. That resistance, acting
      -- at the spanwise lever arm (position), is what rolls the fuselage — the
      -- fuselage/tail sit on the centreline and carry no roll lever arm.
  in \rollRate strokeRate _commonRate ->
  let rates = Vec3 rollRate (vy (bodyRatesRadS input)) (vz (bodyRatesRadS input))
      flapVelocity = crossVec (Vec3 (side * strokeRate) 0 0)
                       (addVec position (Vec3 0 0 (-wingRootHeight)))
      bendAngleRate = stripBendSlopeRate old / (1+bendSlope*bendSlope)
      bendRotationVelocity = scaleVec bendAngleRate
        (crossVec (Vec3 side 0 0) (scaleVec (-0.25*chord) chordAxis))
      elasticVelocity = addVec bendRotationVelocity (addVec
        (scaleVec (stripBendRateMS old) (Vec3 0 (-side*sin dihedral) (cos dihedral)))
        (scaleVec (-0.25*chord*stripTwistRateRadS old) normal))
      sectionVelocity = addVec elasticVelocity (addVec velocity (addVec (crossVec rates position) flapVelocity))
      dot (Vec3 a b c) (Vec3 d e f) = a*d + b*e + c*f
      chordVelocity = dot sectionVelocity chordAxis
      normalVelocity = dot sectionVelocity normal
      spanVelocity = dot sectionVelocity spanAxis
      planarSpeed = sqrt (chordVelocity * chordVelocity + normalVelocity * normalVelocity)
      safeSpeed = max 1.0e-6 planarSpeed
      -- Incidence, rest twist and load-driven elastic twist are already in
      -- the section frame. No phase-selected feathering or recovery assistance:
      -- the same relative flow and polar act in either stroke direction.
      alpha = atan2 (-normalVelocity) chordVelocity
      alphaEff = alpha - zeroLiftAngle camberPrev
      alphaDelta = alpha - stripPreviousAlpha old
      alphaRate = atan2 (sin alphaDelta) (cos alphaDelta) / dt
      reynolds = planarSpeed * chord / 1.48e-5
      stallAngle = radians (clamp 10 18 (16 - 1.4 * logBase 10 (max 1 (150000 / reynolds))))
      dynamicDelay = clamp (-0.16) 0.16 (0.018 * alphaRate * signum alphaEff)
      target = smoothStep (stallAngle - radians 3) (stallAngle + radians 4)
                (abs alphaEff - dynamicDelay)
      -- Stall hysteresis: separation sets in fast (0.045 s), but reattachment
      -- is deliberately slower so a passing gust doesn't chatter the flow.
      -- 0.090 s, however, made a brief stall LINGER through several flap cycles:
      -- after a hard pull-up bled off speed, the wing stayed separated (no lift
      -- → no pitch authority, elevated separated drag → no net thrust) long
      -- after alpha had dropped back below stall. 0.060 s reattaches within ~1
      -- flap cycle while still smoothing genuine gust noise.
      separating = target > stripSeparation old
      tau = if separating then 0.045 else 0.060
      UnitInterval relaxed = relaxSeparation (Seconds dt) (Seconds tau)
                               (clamp01 (stripSeparation old)) (clamp01 target)
      levTarget = if abs alphaEff > stallAngle && alphaRate * alphaEff > 0 then 1 else 0
      levRelax = 1 - exp (-dt / if levTarget > stripLevStrength old then 0.025 else 0.080)
      lev = clamp 0 1 (stripLevStrength old + levRelax * (levTarget - stripLevStrength old))
      -- Finite-aspect-ratio lift slope (Helmbold): a 3D wing of aspect ratio
      -- AR reaches 2π·AR/(AR+2), not the 2D thin-airfoil 2π. The missing part
      -- is the induced downwash — the same downwash charged again below as
      -- induced drag. Using 2π here double-counts it: lift reads too high AND
      -- the drag-due-to-lift ramps up on that inflated Cl. The 3D slope keeps
      -- the lift/drag relationship consistent.
      liftSlope = finiteWingLiftSlope aspectRatio
      attachedCl = clamp (-1.9) 1.9 (liftSlope * alphaEff)
      -- Rotational circulation follows section pitch, not the difference of
      -- flow angle (which includes roll acceleration and gust acceleration).
      -- The latter duplicated added-air inertia and introduced timestep feedback.
      sectionPitchRate = stripTwistRateRadS old - side * dot
                           (addVec rates (Vec3 (side*strokeRate) 0 0)) spanAxis
      rotationalCl = clamp (-0.65) 0.65 (0.20 * chord * sectionPitchRate / safeSpeed)
      -- Keep separation/LEV memory fixed during the implicit mechanical solve.
      -- Updating its discrete vortex gate at each trial velocity can leave no
      -- root at all. Advance that memory once in `next` after convergence.
      forceSeparation = if implicitLoads then stripSeparation old else relaxed
      vortexLift = if implicitLoads
        then signum (stripPreviousAlpha old - zeroLiftAngle camberPrev) * 0.05 * stripLevStrength old
        else signum alphaEff * 0.05 * lev
      circulatoryCl = (1 - forceSeparation) * attachedCl
      cl = circulatoryCl + rotationalCl + vortexLift
      -- Membrane loading follows the same surface pressure applied to the
      -- mechanics, including the relaxing per-strip air patch.
      camberNext = relaxCamber dt (spMembraneTau profile) camberPrev
                     (membraneCamberTarget profile
                       (clamp (-12) 12 (dot aeroForce normal / max 1e-9 (q * area))))
      -- Induced drag is the cost of the CIRCULATION (bound-vortex) lift only.
      -- Rotational lift (Kramer effect) and LEV lift are transient/separated
      -- mechanisms that do NOT shed the trailing downwash; charging them
      -- Cl²/(πeAR) inflated the drag at high alpha and coupled it to the lagged
      -- LEV/separation state — the sway feedback loop. Square the attached
      -- (circulation) lift instead. Ground effect is the classic victim: the
      -- image vortex cancels downwash, so the wake tilts less and the bird
      -- glides faster.
      inducedRelief = max 0 (1 - groundEffectDragRelief * ge)
      inducedCd = inducedDragCoefficient aspectRatio circulatoryCl * inducedRelief
      -- Skin friction and circulation drag are distinct from broadside
      -- pressure. Pressure reacts immediately to normal motion; stall memory
      -- must not make a newly rotating wing transparent to air.
      cd = surfaceSkinCd + inducedCd
      -- Effective edge/membrane crossflow coefficient; shared by glide and flap.
      sideCd = 0.04 + 0.12 * forceSeparation
      area = chord * dr
      q = 0.5 * airDensity * planarSpeed * planarSpeed
      lift = q * area * cl * (1 + groundEffectLiftGain * ge)
      drag = q * area * cd
      flowX = chordVelocity / safeSpeed
      flowZ = normalVelocity / safeSpeed
      -- Lift is orthogonal to section velocity, drag opposes it, including
      -- reverse flow. In particular a passive falling wing cannot add energy.
      chordForce = scaleVec (-drag * flowX - lift * flowZ) chordAxis
      normalForce = scaleVec (-drag * flowZ + lift * flowX) normal
      spanDrag = scaleVec (-0.5 * airDensity * area * sideCd * abs spanVelocity * spanVelocity) spanAxis
      -- Full local-flow drag is shared by glide and flap. The coherent wake
      -- pressure difference is added by coupleWingPressure after both wings
      -- have been evaluated on their deformed surfaces.
      oldPatch = stripPressureField old
      quadraticPressure = scaleVec (surfacePressure area normalVelocity) normal
      liftPart = addVec (scaleVec (lift * flowX) normal)
                       (scaleVec (-lift * flowZ) chordAxis)
      force = addVec quadraticPressure (addVec chordForce (addVec normalForce spanDrag))
      aeroForce = force
      nextLoad = fieldLoad oldPatch + (1-exp(-dt/0.15))
                   * (magnitude liftPart + magnitude quadraticPressure - fieldLoad oldPatch)
      nextPatch = oldPatch {fieldLoad=nextLoad}
      -- Explicit differencing of BODY acceleration as an added-mass force
      -- creates a delayed feedback loop at contacts. Omit this term until an
      -- implicit fluid/body inertia solve is available; do not clamp it into
      -- a fictitious 25 N source at every strip.
      cm0 = sectionPitchMomentCoeff camberPrev (spReflex profile)
      sectionPitchMoment = cm0 * q * chord * chord * dr
      -- Twist is driven by the SAME force at its actual quarter-chord lever,
      -- not a separate 2-4% chord coefficient. Otherwise the structure barely
      -- feathers even when the aerodynamic moment is large.
      leMoment = crossVec (addVec position (scaleVec (-1) leadingEdge)) force
      sectionTwistMoment = -side * dot leMoment spanAxis + sectionPitchMoment
      sectionBendMoment = side * vx leMoment - sectionPitchMoment * vx spanAxis
      -- Section Cm is positive nose-up. With body +x forward,+y right,+z up,
      -- the axial +Y moment rotates the nose DOWN, so convert the sign here.
      -- Keep the scalar section moment in its original convention for twist.
      moment = addVec (crossVec position force)
                      (scaleVec (-side * sectionPitchMoment) spanAxis)
      power = max 0 (negate (dot force flapVelocity))
      next = old { stripSeparation = relaxed, stripPreviousAlpha = alpha
                 , stripPreviousNormalVelocity = normalVelocity, stripLevStrength = lev
                 , stripCamber = camberNext, stripPressureField = nextPatch
                 , stripPosition = position, stripNormal = normal
                 , stripLift = liftPart, stripPressure = quadraticPressure
                 , stripAirVelocity = sectionVelocity }
  in StripResult next force moment power sectionTwistMoment sectionBendMoment (position,chordAxis,spanAxis,normal,sectionVelocity,flapVelocity)

-- A coherent wake carries the common motion of each spanwise pair. Its
-- differential pressure stores roll strain, not aircraft altitude: paired
-- reactions sum to zero force and do work only on relative strip motion.
-- Instantaneous lift and full surface drag still see COMPLETE local flow on
-- each wing in every mode. A crosswind gradient also enters the relative flow.
coupleWingPressure :: VehicleInput -> WingModel -> [StripState] -> [StripState]
                   -> [StripResult] -> [StripResult] -> ([StripResult],[StripResult])
coupleWingPressure input model oldL oldR left right = unzip
  [ pair i a b l r | (i,(a,b,l,r)) <- zip [0::Int ..] (zip4 oldL oldR left right) ]
 where
  dt=stepSeconds input
  pair i a b l r =
    let (pl,cl,sl,nl,vl,fl)=resultFrame l
        (pr,cr,sr,nr,vr,fr)=resultFrame r
        chord=shapeChord (wmWing model) ((fromIntegral i+0.5)/fromIntegral stripCount)
        pa=stripPressureField a; pb=stripPressureField b
        history=addVec (fieldDirection pa) (fieldDirection pb)
        target=unitVector (addVec nl nr)
        rawNormal=if magnitude history>0.5
          then addVec (unitVector history) (scaleVec (1-exp(-dt/12)) (addVec target (scaleVec (-1) (unitVector history))))
          else target
        -- This is remembered surface pressure, not the instantaneous lift
        -- vector. Re-projecting it perpendicular to body airspeed every step
        -- parallel-transported the support plane through pitch/yaw turns and
        -- accumulated an uncommanded bank. Convect its direction unchanged;
        -- only the explicit relaxation above may follow the wing. Ordinary
        -- circulatory lift remains perpendicular to each strip's local flow.
        n=unitVector rawNormal
        load=0.5*(fieldLoad pa+fieldLoad pb)
        stiffness=100*load/chord
        q0=scaleVec 0.5 (addVec (fieldDisplacement pa) (scaleVec (-1) (fieldDisplacement pb)))
        relative=scaleVec 0.5 (addVec vl (scaleVec (-1) vr))
        -- A coherent wake persists in a through-flow. Once that flow stops,
        -- shed its strain promptly instead of pinning a landed wing to an old
        -- airborne patch. Use the pair's common local flow, so pure roll cannot
        -- manufacture the flow that would keep its own pressure memory alive.
        commonFlow=scaleVec 0.5 (addVec vl vr)
        commonSpeed2=dotVector commonFlow commonFlow
        wakeTau=0.05+11.95*commonSpeed2/(commonSpeed2+1)
        (springPressure,patch)=advancePressureField dt wakeTau stiffness n relative (PressureField q0 n load)
        dr=wsSpanM (wmWing model)/fromIntegral stripCount
        entrainedMass=airDensity*pi*chord*chord*dr/4
        damping=2*sqrt(stiffness*entrainedMass)
        pressure=addVec springPressure (scaleVec (-damping*dotVector relative n) n)
        update old side position chordAxis spanAxis normal velocity flapVelocity force result =
          let arm=scaleVec (-0.25*chord) chordAxis
              leMoment=crossVec arm force
              next=resultState result
              nextLoad=0.5*(fieldLoad(stripPressureField(resultState l))+fieldLoad(stripPressureField(resultState r)))
              energyScale=if nextLoad>1e-12 then min 1e6 (sqrt(load/nextLoad)) else 0
              nextPatch=(stripPressureField next)
                {fieldDisplacement=transportVector dt (bodyRatesRadS input)
                     (scaleVec (-side*energyScale) (fieldDisplacement patch))
                ,fieldDirection=transportVector dt (bodyRatesRadS input) n}
              total=addVec (resultForce result) force
              profile=shapeSection (wmWing model) ((fromIntegral i+0.5)/fromIntegral stripCount)
              planarSpeed2=dotVector velocity velocity-dotVector velocity spanAxis**2
              qArea=0.5*airDensity*max 1e-9 planarSpeed2*chord*dr
              camber=relaxCamber dt (spMembraneTau profile) (stripCamber old)
                (membraneCamberTarget profile (clamp (-12) 12 (dotVector total normal/qArea)))
          in result { resultForce=total
             ,resultMoment=addVec (resultMoment result) (crossVec position force)
             ,resultSectionMoment=resultSectionMoment result-side*dotVector leMoment spanAxis
             ,resultBendMoment=resultBendMoment result+side*vx leMoment
             ,resultPower=max 0 (negate(dotVector total flapVelocity))
             ,resultState=next {stripCamber=camber,stripPressure=addVec (stripPressure next) force,stripPressureField=nextPatch}}
    in (update a (-1) pl cl sl nl vl fl pressure l,update b 1 pr cr sr nr vr fr (scaleVec (-1) pressure) r)

transportSeparation :: Double -> VehicleInput -> WingShape -> [StripResult] -> [StripResult]
transportSeparation _side input wp results = zipWith update [0 ..] results
  where
    dt = stepSeconds input
    dr = wsSpanM wp / fromIntegral stripCount
    count = length results
    separationAt i = stripSeparation . resultState $ results !! clampInt 0 (count - 1) i
    update i result =
      let fraction = (fromIntegral i + 0.5) / fromIntegral stripCount
          sweep = shapeSweepRad wp fraction
          speed = max 0.05 (magnitude (bodyVelocityMS input))
          -- Strip indices always run root-to-tip, on both wings. Signed sweep therefore
          -- determines transport direction without an additional world-side sign.
          crossSpeed = unMetresPerSecond (crossflowBaseline 0.32 (Radians sweep)
                           (MetresPerSecond speed))
          upstream = if crossSpeed >= 0 then i - 1 else i + 1
          current = separationAt i
          courant = clamp 0 0.45 (abs crossSpeed * dt / dr)
          advected = 0.75 * courant * (separationAt upstream - current)
          diffusion = min 0.25 (0.0051046875 * dt / (dr*dr)) * (separationAt (i - 1) - 2 * current + separationAt (i + 1))
          newSeparation = clamp 0 1 (current + advected + diffusion)
          oldState = resultState result
      in result { resultState = oldState { stripSeparation = newSeparation } }

-- | Solve bending and torsion against the pressure evaluated on the same
-- end-of-step surface. The former explicit, post-hinge update delayed elastic
-- velocity by one step and could add energy even with passive pressure.
-- Geometry/mass/stiffness are cached outside the hinge Newton iterations.
prepareStructure :: Double -> Double -> SparInventory -> WingShape -> VehicleInput -> [StripState]
                 -> ((Double,Double), (Double,Double) -> [StripState],
                     (Double,Double) -> [StripResult] -> (Double,Double), StripResult -> StripResult)
prepareStructure side stroke inv wp input old =
  let dt = stepSeconds input
      dr = wsSpanM wp / fromIntegral stripCount
      fracs = [(fromIntegral i+0.5)/fromIntegral stripCount | i<-[0..length old-1]]
      chords = map (shapeChord wp) fracs
      areas = map (*dr) chords
      weights = map (/max 1e-12 (sum areas)) areas
      (unitBend,unitSlope) = integrateFlapBeam weights (map (sparBendEISpanwise inv) fracs) dr
      unitTwist = integrateTwist weights (map (sparTwistGJSpanwise inv) fracs) dr
      bendCompliance = max 1e-12 (last unitBend)
      twistCompliance = max 1e-12 (last unitTwist)
      bendShape = map (/bendCompliance) unitBend
      slopeShape = map (/bendCompliance) unitSlope
      twistShape = map (/twistCompliance) unitTwist
      dotList a b = sum (zipWith (*) a b)
      bendK = dotList weights bendShape / bendCompliance
      twistK = dotList weights twistShape / twistCompliance
      masses = map (*referenceWingMassKg) weights
      airMasses = map (\c -> airDensity*pi*c*c*dr/4) chords
      totalMasses = zipWith (+) masses airMasses
      bendMass = dotList totalMasses (map (\x -> x*x) bendShape)
      crossMass = sum (zipWith4 (\m c b t -> -m*c*b*t/2) totalMasses chords bendShape twistShape)
      pitchInertias = zipWith3 (\m a c -> m*c*c/3+a*c*c/4+airDensity*pi*c**4*dr/128) masses airMasses chords
      twistMass = dotList pitchInertias (map (\x -> x*x) twistShape)
      tip = last old
      qb0 = stripBendM tip; qt0 = stripTwistAero tip
      vb0 = stripBendRateMS tip; vt0 = stripTwistRateRadS tip
      cb = 2*0.25*sqrt(bendMass*bendK)
      ct = 2*0.25*sqrt(twistMass*twistK)
      deform (vb,vt) = zipWith4 (\s b t slope -> s
        {stripBendM=b*(qb0+dt*vb), stripBendSlope=slope*(qb0+dt*vb)
        ,stripTwistAero=t*(qt0+dt*vt),stripBendRateMS=b*vb,stripTwistRateRadS=t*vt
        ,stripBendSlopeRate=slope*vb}) old bendShape twistShape slopeShape
      normalLoad f r =
        let a = stroke*cos(radians(wsMountAngleDeg wp))+shapeDihedralRad wp f
        in (-side*sin a)*vy(resultForce r)+cos a*vz(resultForce r)
      residual (vb,vt) rs =
        let bendRotationLoad slope r = slope*resultBendMoment r
              / (1+(slope*(qb0+dt*vb))**2)
            fb=dotList bendShape (zipWith normalLoad fracs rs)
                 + sum(zipWith bendRotationLoad slopeShape rs)
            ft=dotList twistShape (map resultSectionMoment rs)
            rb=bendMass*(vb-vb0)+crossMass*(vt-vt0)+dt*(cb*vb+bendK*(qb0+dt*vb)-fb)
            rt=crossMass*(vb-vb0)+twistMass*(vt-vt0)+dt*(ct*vt+twistK*(qt0+dt*vt)-ft)
        in (rb/max 1e-12 bendMass,rt/max 1e-12 twistMass)
      finish r =
        let state=resultState r
            diagnostic=clamp 0 1 (0.5*abs(stripBendM state)/(0.10*wsSpanM wp)+0.5*abs(stripTwistAero state)/radians 15)
        in r {resultState=state {stripSpringLoad=relaxDeflection dt springLoadTau (stripSpringLoad state) diagnostic}}
  in ((vb0,vt0),deform,residual,finish)

zeroOutput :: Int -> VehicleOutput
zeroOutput flags = VehicleOutput zeroVec zeroVec 0 0 (-1) flags

zeroVec :: Vec3
zeroVec = Vec3 0 0 0

addVec :: Vec3 -> Vec3 -> Vec3
addVec (Vec3 ax ay az) (Vec3 bx by bz) = Vec3 (ax + bx) (ay + by) (az + bz)

scaleVec :: Double -> Vec3 -> Vec3
scaleVec scale (Vec3 ax ay az) = Vec3 (scale * ax) (scale * ay) (scale * az)

crossVec :: Vec3 -> Vec3 -> Vec3
crossVec (Vec3 ax ay az) (Vec3 bx by bz) =
  Vec3 (ay * bz - az * by) (az * bx - ax * bz) (ax * by - ay * bx)

magnitude :: Vec3 -> Double
magnitude (Vec3 ax ay az) = sqrt (ax * ax + ay * ay + az * az)

vecValues :: Vec3 -> [Double]
vecValues (Vec3 ax ay az) = [ax, ay, az]

vx, vy, vz :: Vec3 -> Double
vx (Vec3 value _ _) = value
vy (Vec3 _ value _) = value
vz (Vec3 _ _ value) = value

finite :: Double -> Bool
finite value = not (isNaN value || isInfinite value)

clamp :: Ord a => a -> a -> a -> a
clamp low high = max low . min high

clampInt :: Int -> Int -> Int -> Int
clampInt = clamp

lerp :: Double -> Double -> Double -> Double
lerp from to amount = from + (to - from) * amount

smoothStep :: Double -> Double -> Double -> Double
smoothStep edge0 edge1 value =
  let amount = clamp 0 1 ((value - edge0) / max 1.0e-9 (edge1 - edge0))
  in amount * amount * (3 - 2 * amount)

radians :: Double -> Double
radians degrees = degrees * pi / 180

firstIndex :: (a -> Bool) -> [a] -> Int
firstIndex predicate = go 0
  where
    go _ [] = -1
    go index (value : rest)
      | predicate value = index
      | otherwise = go (index + 1) rest
