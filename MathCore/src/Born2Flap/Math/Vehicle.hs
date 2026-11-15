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
  , surfacePressureCp
  , surfacePressureWith
  , stallOnset
  , finiteWingLiftSlope
  , rollLiftSlope
  , rollLiftSlopeRatio
  , inducedDragCoefficient
  , emptyStrip
  , initialStrips
  , stepWingWithStroke
  , wingLoadsWithStroke
  , prepareWingLoads
  , WingSystem(..), prepareWingSystem
  , stepCoupledWing
  , addVec
  , crossVec
  , scaleVec
  , magnitude
  , zeroVec
  , vx, vy, vz
  , radians
  ) where

import Data.List (zipWith4)

import Born2Flap.Math.RollDynamics (solveImplicitSystem)
import Born2Flap.Math.LiftField
import Born2Flap.Math.Types
import Born2Flap.Math.Simulation
import Born2Flap.Math.Wing (crossflowBaseline, relaxSeparation)
import Born2Flap.Math.Model (WingModel(..), kestrelModel)
import Born2Flap.Math.Planform
  ( WingShape(..), SpanStation(..), shapeStationsAt, shapeChord
  , shapeSweepRad, shapeDihedralRad, shapeAspectRatio, shapeSection, referenceWingMassKg )
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
surfaceSkinCd = 0.012

-- Flat-plate normal-pressure coefficient for wings and ruddervators.
surfacePressureCp :: Double
surfacePressureCp = 2.0

-- Kinematic viscosity of air at 15 °C [m²/s] and the Stokes creeping-flow
-- plate constant (≈4π for a thin strip normal to the flow). Together they
-- form the PHYSICAL low-Reynolds-number normal-force law: a viscous linear
-- term 2·Cstokes·ν/c·v that dominates the quadratic pressure only below
-- ~0.005 m/s at our chord scale — six orders below the old arbitrary 1.5 m/s
-- floor. Slow differential motion is resisted by real added mass (elastic
-- solve) and spanwise sweep coupling, never by a faked minimum speed.
airKinematicViscosity, surfaceStokesPlateCd :: Double
airKinematicViscosity = 1.48e-5
surfaceStokesPlateCd = 4 * pi

-- Shared wing/tail normal pressure: quadratic v·|v| plus the low-Re viscous
-- linear term. No artificial minimum-speed floor: a surface at rest in still
-- air produces no force, and vanishingly slow motion meets only true viscous
-- drag. @sqrt area@ is the reference chord scale (exact per strip, panel-scale
-- for the fixed fan). The same provisional broadside coefficient applies in
-- glide and flap; it already represents separated pressure resistance.
surfacePressureWith :: Double -> Double -> Double -> Double
surfacePressureWith pressureCp area normalSpeed =
  -0.5 * airDensity * area * normalSpeed
    * (pressureCp * abs normalSpeed
       + 2 * surfaceStokesPlateCd * airKinematicViscosity / max 1e-9 (sqrt area))

surfacePressure :: Double -> Double -> Double
surfacePressure = surfacePressureWith surfacePressureCp

-- | Instantaneous stall onset (0 = attached, 1 = fully separated) for one wing
-- section: the smooth blend of |alphaEff| across the Reynolds-dependent stall
-- angle, with a dynamic delay on alpha rate. This is the single source of
-- truth for the separation relaxation. @prevAlpha@/@camber@ are the strip's previous
-- alpha and camber state.
stallOnset :: Double -> Double -> Double -> Double -> Double -> Double -> Double -> Double
stallOnset dt chordVelocity normalVelocity planarSpeed prevAlpha camber chord =
  let alpha = atan2 (-normalVelocity) chordVelocity
      alphaEff = alpha - zeroLiftAngle camber
      alphaRate = atan2 (sin (alpha - prevAlpha)) (cos (alpha - prevAlpha)) / dt
      reynolds = planarSpeed * chord / airKinematicViscosity
      stallAngle = radians (clamp 10 18 (16 - 1.4 * logBase 10 (max 1 (150000 / reynolds))))
      dynamicDelay = clamp (-0.16) 0.16 (0.018 * alphaRate * signum alphaEff)
  in smoothStep (stallAngle - radians 3) (stallAngle + radians 4) (abs alphaEff - dynamicDelay)

finiteWingLiftSlope :: Double -> Double
finiteWingLiftSlope aspect = 2*pi*aspect/(aspect+2)*circulationLiftEfficiency

-- Anti-symmetric (roll/dihedral) lift slope. The finite-wing reduction above
-- is the SYMMETRIC downwash of the whole bound-vortex sheet (both wings shed
-- same-sign trailing vortices). In the roll/dihedral channel the two wings
-- carry OPPOSITE circulation, so their mutual downwash cancels and the section
-- responds at the 2D section slope, not the reduced 3D slope. This is the
-- downwash-free end of the anti-symmetric lifting-line mode.
rollLiftSlope :: Double
rollLiftSlope = 2 * pi * circulationLiftEfficiency

-- | Factor by which the strip solver's (3D-slope) roll/dihedral lift must be
-- rescaled to use the anti-symmetric slope. Equal to (AR+2)/AR.
rollLiftSlopeRatio :: Double -> Double
rollLiftSlopeRatio aspect = rollLiftSlope / finiteWingLiftSlope aspect

-- Provisional span-efficiency setting: 0.88 reduces induced drag by 6.8%
-- relative to 0.82. This is not a measured calibration of the flapping wake.
-- Ideal planar elliptical loading has e=1 and MINIMUM induced drag.
inducedDragCoefficient :: Double -> Double -> Double
inducedDragCoefficient aspect cl = cl*cl/(pi*0.88*max 0.01 aspect)

-- Smoothing of the legacy display diagnostic only; not a physical time constant.
springLoadTau :: Double
springLoadTau = 0.05

-- | Provisional fraction of critical damping for the assembled elastic modes.
-- 0.08 is underdamped, not lossless: a free linear mode retains about 36% of
-- its energy after one damped period. Carbon alone does not determine losses
-- in the membrane, attachments and joints. Aerodynamic damping is separate.
structuralDampingRatio :: Double
structuralDampingRatio = 0.08

-- | Rotational (Kramer) lift coefficient: the extra circulation a section gains
-- when it pitches about its span axis. This is a provisional coefficient in
-- Cl (q=0.5*rho*V^2) convention, on top of quarter-chord apparent-flow lift.
-- Published Crot coefficients use their own force normalization and reference
-- translation point; a leading-edge value is not directly interchangeable.
-- The contribution follows signed pitch rate, not stroke phase, and can
-- decrease downstroke lift during washout. It is not a pitch-up control gain.
rotationalLiftCoefficient :: Double
rotationalLiftCoefficient = 1.6

-- | Strength 0..1 of ground effect at a strip height @h@ above the surface,
-- normalised by the total wingspan @b@. 1 on the deck, ~0.08 by one span, so
-- the effect lives on the scale the wing actually spans. EMPIRICAL span-scaled
-- decay, not a universal law: the image-wing model below uses it to raise the
-- effective aspect ratio, so lift-slope gain and induced-drag relief stay
-- consistent through the SAME downwash cancellation.
groundEffectFactor :: Double -> Double -> Double
groundEffectFactor h b
  | b <= 0.01 = 0
  | h <= 0    = 1
  | otherwise = exp (-2.5 * h / b)

-- Image-wing aspect-ratio gain: the reflected wing extends the effective
-- aspect ratio. Lift slope rises toward the 2D value AND the induced-drag
-- factor drops through that same effective AR — no free lift multiplier.
-- Strength 0.5 is a documented empirical calibration (classic image estimates
-- bracket 0.3..0.8 for h/b ≲ 0.5); thick separated wakes weaken the image.
groundEffectAspectGain :: Double
groundEffectAspectGain = 0.5

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
  , stripDrag :: !Vec3       -- ^ skin, induced and spanwise drag
  , stripMoment :: !Vec3     -- ^ aerodynamic r cross F plus section couple
  , stripAirVelocity :: !Vec3 -- ^ complete velocity through the sampled local wind
  , stripWindDelta :: !Vec3   -- ^ local wind minus body-origin wind, body frame
  } deriving stock (Eq, Show)
emptyStrip :: StripState
emptyStrip = StripState 0 0 0 0 0 0 0 0 0 0 0 0 emptyPressureField zeroVec (Vec3 0 0 1) zeroVec zeroVec zeroVec zeroVec zeroVec zeroVec

data VehicleState = VehicleState
  { vehicleOscillator :: !OscillatorState
  , leftStrips :: ![StripState]
  , rightStrips :: ![StripState]
  } deriving stock (Eq, Show)

data StripResult = StripResult
  { resultState :: ~StripState
  , resultForce :: !Vec3
  , resultMoment :: !Vec3
  , resultLift :: !Vec3              -- ^ circulation lift force (Kutta-Joukowski), pre-drag/pre-pressure
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
   in stepWingWithStroke True side stroke strokeRate input kestrelModel 0 states

-- | Step one wing given the *actual* flap angle [rad] and flap rate [rad/s],
-- independent of how they were produced (firmware mixer + servo, or the
-- legacy throttle drive). This is the primitive the firmware-emulation loop
-- uses to close the aero→servo-load feedback. Lift comes only from the wing's
-- motion through the air and the bound/LEV circulation it sustains; weight is
-- carried by the vehicle mass budget, never handed to the strips.
stepWingWithStroke :: Bool -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [StripState]
                   -> ([StripResult], [StripState])
stepWingWithStroke flapping side stroke strokeRate input model incidenceRad states =
   stepCoupledWing flapping side stroke strokeRate strokeRate strokeRate input model incidenceRad states

-- Force-only evaluation for the implicit roll Jacobian. Structural integration
-- and separation transport only modify next state; commit those once after
-- solving, rather than doing a full beam solve at every trial velocity.
wingLoadsWithStroke :: Bool -> Double -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [StripState] -> [StripResult]
wingLoadsWithStroke flapping side stroke rate phaseRate input model incidenceRad states =
  zipWith (\i s -> stepStrip True phaseRate flapping side stroke input incidenceRad (wmWing model) i s (vx (bodyRatesRadS input)) rate rate) [0..] states

-- Geometry and material history are shared by all trial velocities. The host
-- solves these four elastic coordinates together with body roll and hinges.
data WingSystem = WingSystem
  { wingInitialRates :: (Double,Double)
  , wingTrialLoads :: Double -> Double -> Double -> (Double,Double) -> [StripResult]
  , wingTrialMotion :: Vec3 -> Vec3 -> Double -> Double -> (Double,Double) -> [StripResult]
  , wingElasticResidual :: (Double,Double) -> [StripResult] -> (Double,Double)
  , wingCommit :: [StripResult] -> ([StripResult],[StripState])
  }

prepareWingSystem :: Bool -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [StripState] -> WingSystem
prepareWingSystem flapping side stroke phaseRate input model incidenceRad states =
  let wp = wmWing model
      stations = shapeStationsAt wp stripCount
      aspect = shapeAspectRatio wp
      (initial,deform,residual,finish) = prepareStructure side stroke (wmSpars model) wp input states
      rawMotion rates deltaVelocity rate commonRate elastic = zipWith
        (\station s -> stepStripPreparedMotion station aspect True phaseRate flapping side stroke input incidenceRad wp 0 s rates deltaVelocity rate commonRate)
        stations (deform elastic)
      raw rollRate rate commonRate = rawMotion (Vec3 rollRate (vy(bodyRatesRadS input)) (vz(bodyRatesRadS input))) zeroVec rate commonRate
      commit rs = let results=transportSeparation side input wp (map finish rs)
                  in (results,map resultState results)
  in WingSystem initial raw rawMotion residual commit

prepareWingLoads :: Bool -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [StripState]
                 -> Double -> Double -> Double -> [StripResult]
prepareWingLoads flapping side stroke phaseRate input model incidenceRad states =
  let system=prepareWingSystem flapping side stroke phaseRate input model incidenceRad states
      (vb,vt)=wingInitialRates system
  in \rollRate rate common ->
    let loads b t=wingTrialLoads system rollRate rate common (b,t)
        residual [b,t]=let (rb,rt)=wingElasticResidual system (b,t) (loads b t) in [rb,rt]
        residual _=error "elastic coordinate count"
        solved=solveImplicitSystem residual [vb,vt]
    in case solved of Just [b,t] -> loads b t; _ -> loads (0/0) (0/0)

-- Commit aero memory/structure after the implicit solve using the same loads
-- as its residual. Hysteretic state transitions are not Newton unknowns.
stepCoupledWing :: Bool -> Double -> Double -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [StripState]
                -> ([StripResult], [StripState])
stepCoupledWing flapping side stroke rate phaseRate commonRate input model incidenceRad states =
  let wp=wmWing model
      raw=prepareWingLoads flapping side stroke phaseRate input model incidenceRad states (vx (bodyRatesRadS input)) rate commonRate
      results=transportSeparation side input wp raw
  in (results,map resultState results)

-- Reference Kestrel height for legacy diagnostic callers. Production geometry
-- reads the selected wing's mount height, shared with mass and hinge dynamics.
wingRootHeight :: Double
wingRootHeight = wsRootHeightM (wmWing kestrelModel)

stepStrip :: Bool -> Double -> Bool -> Double -> Double -> VehicleInput -> Double -> WingShape -> Int -> StripState -> Double -> Double -> Double -> StripResult
stepStrip implicitLoads phaseRate flapping side stroke input incidenceRad wp index old rollRate rate common =
  stepStripPrepared (shapeStationsAt wp stripCount !! index) (shapeAspectRatio wp)
    implicitLoads phaseRate flapping side stroke input incidenceRad wp index old rollRate rate common

-- Planform interpolation is invariant during the nonlinear solve. Share it
-- across all trial elastic velocities instead of sorting/resampling the
-- outline again for each Jacobian column.
stepStripPrepared :: SpanStation -> Double -> Bool -> Double -> Bool -> Double -> Double -> VehicleInput -> Double -> WingShape -> Int -> StripState -> Double -> Double -> Double -> StripResult
stepStripPrepared station aspectRatio implicitLoads phaseRate flapping side stroke input incidenceRad wp index old rollRate rate common =
  stepStripPreparedMotion station aspectRatio implicitLoads phaseRate flapping side stroke input incidenceRad wp index old
    (Vec3 rollRate (vy(bodyRatesRadS input)) (vz(bodyRatesRadS input))) zeroVec rate common

stepStripPreparedMotion :: SpanStation -> Double -> Bool -> Double -> Bool -> Double -> Double -> VehicleInput -> Double -> WingShape -> Int -> StripState -> Vec3 -> Vec3 -> Double -> Double -> StripResult
{-# NOINLINE stepStripPreparedMotion #-}
stepStripPreparedMotion station aspectRatio implicitLoads _phaseRate _flapping side stroke input incidenceRad wp _index old =
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
      -- Exact 3D hinge kinematics. The flap hinge axis is the body X axis
      -- yawed by mountRad in the horizontal plane, and the WHOLE wing follows
      -- one rigid rotation about it, with u = side·stroke so a positive stroke
      -- raises BOTH wings. Position, velocity and the section frame come from
      -- the same transform — no mixed-axis decomposition, so a yawed hinge
      -- (forward-swept ornithopters) gets its true sweep/incidence coupling.
      -- The servo arm yaws the neutral sweep by (mountRad − armRad); planform
      -- dihedral and the aeroelastic bend slope rotate the section about the
      -- neutral chord axis. mountRad/armRad are zero for the kestrel, so it
      -- keeps pure parallel flapping.
      !mountRad = radians (wsMountAngleDeg wp)
      !armRad = radians (wsArmAngleDeg wp)
      !hingeAxis = Vec3 (cos mountRad) (side * sin mountRad) 0
      !u = side * stroke
      !sweep0 = mountRad - armRad
      !dihedralBase = spanDihed station + atan bendSlope
      -- Legacy-compatible dihedral of the POSITION/bend-offset vectors
      -- (stroke ∝ cos mountRad about the body X axis): for a parallel hinge
      -- this is exactly the old geometry; for a yawed hinge the position
      -- itself follows the exact rotation while this scalar only orients the
      -- elastic bend offset and elastic velocity direction.
      !dihedralApprox = stroke * cos mountRad + spanDihed station
      !profile = spanSection station
      !camberPrev = stripCamber old
      !aeroTwist = stripTwistAero old
      !bend = stripBendM old
      !bendSlope = stripBendSlope old
      -- Rest (pre-stroke) section frame: static arm yaw, then planform
      -- dihedral + bend slope about the neutral chord axis.
      !chordS = Vec3 (cos sweep0) (side * sin sweep0) 0
      !spanS = rotateX (side * dihedralBase) (Vec3 (-sin sweep0) (side * cos sweep0) 0)
      !normalS = rotateX (side * dihedralBase) (Vec3 0 0 1)
      !chordBase = rotateAxis u hingeAxis chordS
      !spanBase = rotateAxis u hingeAxis spanS
      !normalBase = rotateAxis u hingeAxis normalS
      -- Mount incidence, geometric twist and elastic twist rotate the local
      -- surface frame exactly once, about the stroke-rotated span axis.
      !ci = cos (incidenceRad + twist + aeroTwist)
      !si = sin (incidenceRad + twist + aeroTwist)
      !chordAxis = addVec (scaleVec ci chordBase) (scaleVec si normalBase)
      !normal    = addVec (scaleVec ci normalBase) (scaleVec (-si) chordBase)
      !spanAxis  = spanBase
      -- Galilean invariance: ONE apparent wind for the whole bird (the body
      -- velocity already carries the origin wind subtraction). Per-strip wind
      -- deltas are dead: different parts of the bird must never fly through
      -- different air. The stripWindDelta field stays in the ABI but is inert.
      !velocity = bodyVelocityMS input
      -- Use section velocity through air throughout (not a mixture of air
      -- velocity and body velocity). Drag must do negative work. The normal
      -- rotates with the flap; positive stroke raises BOTH wings.
      -- Aerodynamic centre sits at the quarter-chord, AFT of the shoulder
      -- hinge (the root LE is at x=0, chord runs aft, so AC x is negative).
      -- Placing the lift force at this lever arm gives the wing the nose-down
      -- static moment a real bird has; with the lift pinned at x=0 (the CG),
      -- the wing contributes no pitch moment and the tail downforce alone
      -- trims the bird nose-up regardless of CG slider position.
      -- Leading edge follows the SAME rigid rotation: streamwise planform
      -- offset (spanLeadX encodes the outline sweep) plus the spanwise radius,
      -- rotated about the yawed hinge axis, then the elastic bend offset along
      -- the stroke-rotated normal.
      !spanRest = rotateX (side * spanDihed station) (Vec3 (-sin sweep0) (side * cos sweep0) 0)
      !rootHeight = wsRootHeightM wp
      !leadingEdge = addVec (Vec3 0 0 rootHeight)
                     (addVec (rotateAxis u hingeAxis
                               (addVec (scaleVec (-spanLeadX station) (Vec3 1 0 0))
                                       (scaleVec radius spanRest)))
                             (scaleVec bend (rotateX (side * dihedralApprox) (Vec3 0 0 1))))
      !position = addVec leadingEdge (scaleVec (-0.25 * chord) chordAxis)
      -- Height is along WORLD up, expressed in this body basis. A banked or
      -- inverted wing must not receive the upright strip-height correction.
      !stripHeightM = dotVector position (bodyUpB input)
      !ge = groundEffectFactor (groundClearanceM input + stripHeightM) (2 * wsSpanM wp)
      -- Flap velocity is the wing surface's motion through the air about the
      -- shoulder hinge — the rigid stroke rotation rate about the TRUE yawed
      -- hinge axis: d/dt[R(u,â)·r] = u̇·(â × r). It exists in BOTH flapping
      -- and glide: in glide a differential-dihedral (roll) command rotates the
      -- wing, so the surface genuinely sweeps through the air and RESISTS.
      -- That resistance, acting at the spanwise lever arm (position), is what
      -- rolls the fuselage — the fuselage/tail sit on the centreline and carry
      -- no roll lever arm. Power then reads F·v = u̇·â·(r×F): the genuine
      -- generalized torque about the hinge, with no free lift or grip.
  in \rates deltaVelocity strokeRate _commonRate ->
  let flapVelocity = scaleVec (side * strokeRate)
                       (crossVec hingeAxis (addVec position (Vec3 0 0 (-rootHeight))))
      bendAngleRate = stripBendSlopeRate old / (1+bendSlope*bendSlope)
      bendAxis = rotateAxis u hingeAxis (Vec3 side 0 0)
      bendRotationVelocity = scaleVec bendAngleRate
        (crossVec bendAxis (scaleVec (-0.25*chord) chordAxis))
      elasticVelocity = addVec
        (scaleVec (stripBendRateMS old) (rotateX (side * dihedralApprox) (Vec3 0 0 1)))
        (scaleVec (-0.25*chord*stripTwistRateRadS old) normal)
      sectionVelocity = addVec deltaVelocity (addVec bendRotationVelocity (addVec elasticVelocity (addVec velocity (addVec (crossVec rates position) flapVelocity))))
      flapOmega = scaleVec (side*strokeRate) hingeAxis
      sectionOmega = addVec rates (addVec flapOmega
        (addVec (scaleVec bendAngleRate bendAxis)
                (scaleVec (-side*stripTwistRateRadS old) spanAxis)))
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
      target = stallOnset dt chordVelocity normalVelocity planarSpeed (stripPreviousAlpha old) camberPrev chord
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
      -- the lift/drag relationship consistent. Ground proximity raises the
      -- EFFECTIVE aspect ratio (image wing): lift slope up and induced-drag
      -- factor down through the SAME AR — no free lift multiplier.
      effectiveAspect = aspectRatio * (1 + groundEffectAspectGain * ge)
      liftSlope = finiteWingLiftSlope effectiveAspect
      attachedCl = clamp (-1.9) 1.9 (liftSlope * alphaEff)
      -- Rotational circulation follows section pitch, not the difference of
      -- flow angle (which includes roll acceleration and gust acceleration).
      -- The latter duplicated added-air inertia and introduced timestep feedback.
      sectionPitchRate = -side * dot sectionOmega spanAxis
      rotationalCl = clamp (-0.65) 0.65 (rotationalLiftCoefficient * chord * sectionPitchRate / safeSpeed)
      -- Keep separation/LEV memory fixed during the implicit mechanical solve.
      -- Updating its discrete vortex gate at each trial velocity can leave no
      -- root at all. Advance that memory once in `next` after convergence.
      forceSeparation = if implicitLoads then stripSeparation old else relaxed
      vortexLift = if implicitLoads
        then signum (stripPreviousAlpha old - zeroLiftAngle camberPrev) * 0.05 * stripLevStrength old
        else signum alphaEff * 0.05 * lev
      circulatoryCl = (1 - forceSeparation) * attachedCl
      cl = circulatoryCl + rotationalCl + vortexLift
      -- Membrane loading follows the same local aerodynamic force applied
      -- to the mechanics.
      camberNext = relaxCamber dt (spMembraneTau profile) camberPrev
                    (membraneCamberTarget profile
                      (clamp (-12) 12 (dot aeroForce normal / max 1e-9 (q * area))))
      -- Induced drag is charged on the WHOLE effective circulation: attached,
      -- rotational (Kramer) and LEV lift all bend the same bound vortex sheet
      -- and shed the same trailing downwash, so none of them is free energy —
      -- the vortex lift pays Cl²/(πeAR) here, and its separated-flow profile
      -- drag enters sideCd through the separation state. Ground effect then
      -- reduces this drag through the SAME effective aspect ratio that raised
      -- the slope (image vortex cancels downwash).
      inducedCd = inducedDragCoefficient effectiveAspect cl
      -- Skin friction and circulation drag are distinct from broadside
      -- pressure. Pressure reacts immediately to normal motion; stall memory
      -- must not make a newly rotating wing transparent to air.
      cd = surfaceSkinCd + inducedCd
      -- Effective edge/membrane crossflow coefficient; shared by glide and flap.
      sideCd = 0.04 + 0.12 * forceSeparation
      area = chord * dr
      q = 0.5 * airDensity * planarSpeed * planarSpeed
      lift = q * area * cl
      drag = q * area * cd
      flowX = chordVelocity / safeSpeed
      flowZ = normalVelocity / safeSpeed
      -- Lift is orthogonal to section velocity, drag opposes it, including
      -- reverse flow. In particular a passive falling wing cannot add energy.
      -- Vector Kutta-Joukowski direction: local motion crossed with the
      -- consistently oriented bound-vortex axis. This includes sideways force
      -- from banking/bending and changes immediately with the apparent wind.
      -- No fixed body/world lift direction and no prescribed outward force.
      liftPart = scaleVec (lift / safeSpeed)
                   (crossVec sectionVelocity (scaleVec side spanAxis))
      profileDrag = scaleVec (-drag / safeSpeed)
                      (addVec (scaleVec chordVelocity chordAxis) (scaleVec normalVelocity normal))
      spanDrag = scaleVec (-0.5 * airDensity * area * sideCd * abs spanVelocity * spanVelocity) spanAxis
      -- Every strip sees complete local flow in glide and flap. No cross-wing
      -- positional spring: air does not retain a fixed bank/support plane.
      -- Non-circulatory added-air inertia is in the coupled mass solve.
      oldPatch = stripPressureField old
      -- Broadside pressure is a distributed surface traction, unlike bound
      -- circulation at the quarter chord. Integrate over the chord, sampling
      -- each point's actual velocity. Uniform pressure acts at midchord;
      -- rotation also creates a real resisting couple even when the centroid
      -- is stationary. Split at any zero of normal velocity: on each interval
      -- pressure is quadratic and its moment cubic, so two Gauss points give
      -- the exact integral without a minimum force or damping constant.
      pressureAt (s,w) =
        let offset=scaleVec ((0.25-s)*chord) chordAxis
            localVelocity=addVec sectionVelocity (crossVec sectionOmega offset)
            -- Quadrature weights integrate the traction; they must not change
            -- the physical strip length used by its low-Re pressure law.
            localForce=scaleVec (w*surfacePressure area (dot localVelocity normal)) normal
        in (localForce,crossVec offset localForce)
      pressureGradient=chord*dot (crossVec sectionOmega chordAxis) normal
      pressureZero=0.25+normalVelocity/pressureGradient
      pressureIntervals=if abs pressureGradient>1e-12 && pressureZero>0 && pressureZero<1
        then [(0,pressureZero),(pressureZero,1)] else [(0,1)]
      pressurePanels=map pressureAt
        [((a+b)/2+sign*(b-a)/(2*sqrt 3),(b-a)/2)
         | (a,b)<-pressureIntervals,sign<-[-1,1]]
      quadraticPressure=foldr (addVec . fst) zeroVec pressurePanels
      pressureCouple=foldr (addVec . snd) zeroVec pressurePanels
      -- Pure aerodynamic lift, no position-based modulation: a held
      -- differential must roll the bird through the wings' real tilted lift
      -- vectors, never through an invented counter-rotation gain.
      force = addVec quadraticPressure (addVec liftPart (addVec profileDrag spanDrag))
      aeroForce = force
      nextLoad = fieldLoad oldPatch + (1-exp(-dt/0.15))
                   * (magnitude liftPart + magnitude quadraticPressure - fieldLoad oldPatch)
      nextPatch = PressureField zeroVec (transportVector dt rates normal) nextLoad
      -- Explicit differencing of BODY acceleration as an added-mass force
      -- creates a delayed feedback loop at contacts. Omit this term until an
      -- implicit fluid/body inertia solve is available; do not clamp it into
      -- a fictitious 25 N source at every strip.
      cm0 = sectionPitchMomentCoeff camberPrev (spReflex profile)
      sectionPitchMoment = cm0 * q * chord * chord * dr
      -- Twist is driven by the SAME force at its actual quarter-chord lever,
      -- not a separate 2-4% chord coefficient. Otherwise the structure barely
      -- feathers even when the aerodynamic moment is large. This is the
      -- physical virtual-work load, not a compliance tuning parameter:
      -- multiplying only this torque breaks the force/moment energy balance.
      -- Positive strokeRate raises BOTH wings; it needs no extra side sign.
      leMoment = addVec pressureCouple
        (crossVec (addVec position (scaleVec (-1) leadingEdge)) force)
      sectionTwistMoment = -side * dot leMoment spanAxis + sectionPitchMoment
      sectionCouple = addVec pressureCouple (scaleVec (-side*sectionPitchMoment) spanAxis)
      sectionBendMoment = dot bendAxis (addVec
        (crossVec (addVec position (scaleVec (-1) leadingEdge)) force) sectionCouple)
      -- Section Cm is positive nose-up. With body +x forward,+y right,+z up,
      -- the axial +Y moment rotates the nose DOWN, so convert the sign here.
      -- Keep the scalar section moment in its original convention for twist.
      -- Every aerodynamic moment is the real force at the real lever arm
      -- τ = r × F about the body origin (the host shifts to the CG), plus the
      -- section couple. No pendulum term, no invented restoring torque: roll
      -- stability must come from dihedral effect, the side-force lever and
      -- the CG position — the geometry itself.
      moment = addVec (crossVec position force) sectionCouple
      -- SIGNED instantaneous mechanical power: −F·v_flap — the rate at which
      -- the WING does work on the air. Positive = the wing drives the air
      -- (actuator input / elastic energy release); negative = the flow
      -- drives the wing (energy recovery — gusts, the upstroke sail).
      -- Clamping at zero hid the recovery half of the exchange; the elastic
      -- solver and the host must see the full signed power to audit the
      -- energy balance of the whole system.
      power = negate (dot force flapVelocity + dot sectionCouple flapOmega)
      dragDiag = addVec (scaleVec (-drag*flowX) chordAxis)
                   (addVec (scaleVec (-drag*flowZ) normal) spanDrag)
      -- The diagnostic decomposition must reconstruct the TOTAL force exactly
      -- (lift + drag + pressure): pressure carries the residuals.
      pressureDiag = addVec force (scaleVec (-1) (addVec liftPart dragDiag))
      next = old { stripSeparation = relaxed, stripPreviousAlpha = alpha
                 , stripPreviousNormalVelocity = normalVelocity, stripLevStrength = lev
                 , stripCamber = camberNext, stripPressureField = nextPatch
                 , stripPosition = position, stripNormal = normal
                 , stripLift = liftPart, stripPressure = pressureDiag
                 , stripDrag = dragDiag
                 , stripMoment = moment
                 , stripAirVelocity = sectionVelocity }
 in StripResult next force moment liftPart power sectionTwistMoment sectionBendMoment (position,chordAxis,spanAxis,normal,sectionVelocity,flapVelocity)

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
      cb = 2*structuralDampingRatio*sqrt(bendMass*bendK)
      ct = 2*structuralDampingRatio*sqrt(twistMass*twistK)
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

-- | Dot product of two body-frame vectors.
dotV :: Vec3 -> Vec3 -> Double
dotV (Vec3 ax ay az) (Vec3 bx by bz) = ax * bx + ay * by + az * bz

-- | Rodrigues rotation of @v@ about the unit axis @axis@ by angle @a@.
-- This is the single transform every wing vector (leading edge, chord, span,
-- normal) and every derived velocity follow — one rigid 3D motion.
rotateAxis :: Double -> Vec3 -> Vec3 -> Vec3
rotateAxis a axis v =
  let axial = scaleVec (dotV v axis) axis
      radial = addVec v (scaleVec (-1) axial)
      tangential = crossVec axis v
  in addVec (scaleVec (cos a) radial) (addVec axial (scaleVec (sin a) tangential))

-- | Rotation about the body X axis (parallel flapping / planform dihedral).
rotateX :: Double -> Vec3 -> Vec3
rotateX a = rotateAxis a (Vec3 1 0 0)

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