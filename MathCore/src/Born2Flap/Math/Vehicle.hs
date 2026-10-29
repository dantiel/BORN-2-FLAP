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

import Born2Flap.Math.Types
import Born2Flap.Math.Simulation
import Born2Flap.Math.Wing (crossflowBaseline, relaxSeparation)
import Born2Flap.Math.Model (WingModel(..), kestrelModel)
import Born2Flap.Math.Planform
  ( WingShape(..), shapeChord, shapeLeadingEdgeX
  , shapeSweepRad, shapeTwistRad, shapeDihedralRad, shapeAspectRatio, shapeSection, referenceWingMassKg )
import Born2Flap.Math.Section
  ( SectionProfile(..), zeroLiftAngle, sectionPitchMomentCoeff
  , membraneCamberTarget, relaxCamber )
import Born2Flap.Math.Structure
  ( SparInventory, integrateFlapBeam, integrateTwist, relaxDeflection, stepCoupledElasticModes
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

-- The support field's handover speed: the compressed-air cushion keeps
-- carrying the load up to this horizontal speed (gate = v0²/(v0²+v²)).
-- Circulation lift takes over only at high cruise speed, so the glide
-- never loses its weight support mid-transition (the old 1 m/s floor
-- handed over at 8 m/s and the bird sank).
supportHandoverMS :: Double
supportHandoverMS = 4.0

-- Static-field coupling of the support force to the wing's TRANSLATION: the
-- beating wing compresses the carrying air field (|rate| term — a hover rides
-- the compressed field), and the translated movement adds/subtracts lift
-- (signed rate term): a wing moving UP loses part of its lift counter-force,
-- a wing pushed DOWN gains it — the user's position+translation lift model.
-- The signed part averages to zero over a symmetric stroke; the compression
-- part is what makes a zero-airspeed hover net-positive despite the
-- (deliberately) stall-degraded reversed-flow circulation at ±90 deg alpha.
fieldCompressionPerRate :: Double
fieldCompressionPerRate = 0.06

-- | Reference velocity of the WORLD-translation lift modulation: the wing's
-- lift counter-force changes with its actual vertical motion through the
-- air (m/s). The modulation saturates at ±(clamp range) within a few tens
-- of cm/s, so a slow glued sweep stays near liftMod=1.
liftModVelRef :: Double
liftModVelRef = 0.3

-- | Slope of the support-field translation modulation: a wing moving up at
-- 1 m/s loses 6x the reference share of its field counter-force.
fieldTranslationSlope :: Double
fieldTranslationSlope = 6.0

-- Speed-agnostic pressure glue of the compressed-air lift field. The field
-- resists any motion of the strip through it — normal compression and
-- tangential shear — with a constant Coulomb-like force from the minimum
-- dynamic pressure (½ρ·Cp·v0²). The magnitude is independent of the speed
-- (the field is pre-compressed) and of the load, so the wing is glued to
-- the air at EVERY speed instead of slipping at slow rates. The quadratic
-- pressure law adds on top at cruise where the dynamic pressure grows.
glueFloorMS, glueShearCp :: Double
glueFloorMS = 4.5
glueShearCp = 0.5

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
  , stripGripLoad :: !Double  -- ^ low-passed field grip strength [N] — the compressed-air anchor reacts to the MEAN carried load, not the instantaneous lift spikes of the flap stroke
  } deriving stock (Eq, Show)

data VehicleState = VehicleState
  { vehicleOscillator :: !OscillatorState
  , leftStrips :: ![StripState]
  , rightStrips :: ![StripState]
  } deriving stock (Eq, Show)

data StripResult = StripResult
  { resultState :: !StripState
  , resultForce :: !Vec3
  , resultMoment :: !Vec3
  , resultPower :: !Double
  , resultSectionMoment :: !Double  -- ^ twist-driving moment about the LE elastic axis [N·m] (nose-up +)
  } deriving stock (Eq, Show)

stripCount :: Int
stripCount = 16

emptyStrip :: StripState
emptyStrip = StripState 0 0 0 0 0 0 0 0 0 0 0 0

-- | Strips initialised to the rest camber of the default wing (so the membrane
-- model starts from the built-in camber line, not a flat plate).
initialStrips :: [StripState]
initialStrips =
  [ StripState 0 0 0 0 (spRestCamber (shapeSection (wmWing kestrelModel) fraction)) 0 0 0 0 0 0 0
  | i <- [0 .. stripCount - 1]
  , let fraction = (fromIntegral i + 0.5) / fromIntegral stripCount
  ]

defaultVehicle :: VehicleState
defaultVehicle = VehicleState defaultOscillator initialStrips initialStrips

stepVehicle :: VehicleInput -> VehicleState -> (VehicleOutput, VehicleState)
stepVehicle input state =
  case runSimulation transaction state of
    Left flag -> (zeroOutput flag, state)
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
   let wp = wmWing model
       raw = zipWith3 (\i s sup -> stepStrip False strokeRate flapping side stroke input incidenceRad wp i s sup centre (vx (bodyRatesRadS input)) strokeRate 0) [0 ..] states supports
       transported = transportSeparation side input wp raw
       deformed = applyStructure side stroke (wmSpars model) wp input transported
   in (deformed, map resultState deformed)

-- Force-only evaluation for the implicit roll Jacobian. Structural integration
-- and separation transport only modify next state; commit those once after
-- solving, rather than doing a full beam solve at every trial velocity.
wingLoadsWithStroke :: Bool -> Double -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [StripState] -> [Double] -> Double -> [StripResult]
wingLoadsWithStroke flapping side stroke rate phaseRate input model incidenceRad states supports centre =
  prepareWingLoads flapping side stroke phaseRate input model incidenceRad states supports centre (vx (bodyRatesRadS input)) rate 0

-- Cache the fixed geometry and aerodynamic history outside Newton's candidate
-- velocities. Rebuilding every strip for every finite-difference column made
-- the 240 Hz physics consume most of the game thread on small aircraft.
prepareWingLoads :: Bool -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [StripState] -> [Double] -> Double
                 -> Double -> Double -> Double -> [StripResult]
prepareWingLoads flapping side stroke phaseRate input model incidenceRad states supports centre =
  let strips = zipWith3 (\i s sup -> stepStrip True phaseRate flapping side stroke input incidenceRad (wmWing model) i s sup centre) [0..] states supports
  in \rollRate rate common -> map (\strip -> strip rollRate rate common) strips

-- Commit aero memory/structure after the implicit solve using the same loads
-- as its residual. Hysteretic state transitions are not Newton unknowns.
stepCoupledWing :: Bool -> Double -> Double -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [StripState] -> [Double] -> Double
                -> ([StripResult], [StripState])
stepCoupledWing flapping side stroke rate phaseRate commonRate input model incidenceRad states supports centre =
  let wp=wmWing model
      raw=prepareWingLoads flapping side stroke phaseRate input model incidenceRad states supports centre (vx (bodyRatesRadS input)) rate commonRate
      results=applyStructure side stroke (wmSpars model) wp input (transportSeparation side input wp raw)
  in (results,map resultState results)

-- High wing / low centre of mass. The side-force lever contributes natural
-- lateral stability; it is not an attitude-dependent restoring torque.
wingRootHeight :: Double
wingRootHeight = 0.04

stepStrip :: Bool -> Double -> Bool -> Double -> Double -> VehicleInput -> Double -> WingShape -> Int -> StripState -> Double -> Double -> Double -> Double -> Double -> StripResult
{-# NOINLINE stepStrip #-}
stepStrip implicitLoads _phaseRate flapping side stroke input incidenceRad wp index old supportN centre =
  let !dt = stepSeconds input
      !fraction = (fromIntegral index + 0.5) / fromIntegral stripCount
      !spanM = wsSpanM wp
      !dr = spanM / fromIntegral stripCount
      -- Spanwise lever arm from the shoulder hinge: the wing root sits at the
      -- shoulder distance outboard of the body centreline, then the wing's own
      -- span runs root→tip.
      !radius = wsShoulderM wp + dr * (fromIntegral index + 0.5)
      !chord = shapeChord wp fraction
      !twist = shapeTwistRad wp fraction
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
      !dihedral = stroke * cos mountRad + shapeDihedralRad wp fraction
      !profile = shapeSection wp fraction
      !camberPrev = stripCamber old
      !aeroTwist = stripTwistAero old
      !bend = stripBendM old
      !bendSlope = stripBendSlope old
      -- Flex reorients the surface: the out-of-plane flap slope adds to the
      -- geometric dihedral, so a loaded tip (bent up) flies at a higher local
      -- dihedral than the stroke alone would set. The force axes (normal/chord/
      -- span) use this local dihedral; the geometric 'dihedral' still sets the
      -- physical strip position and lever arms.
      !dihedralLocal = dihedral + bendSlope
      -- Ground effect: proximity to the surface below, normalised by the total
      -- wingspan (2 × half-span). Each strip sees its OWN flap height, so the
      -- downstroke pushes the tip into the cushion and the upstroke lifts it
      -- back out — the effect is strongest at the bottom of the beat.
      !stripHeightM = wingRootHeight + radius * sin dihedral + bend*cos dihedral
      !ge = groundEffectFactor (groundClearanceM input + stripHeightM) (2 * wsSpanM wp)
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
      !leadingEdge = addVec (Vec3 (-shapeLeadingEdgeX wp fraction)
                         (side * radius * cos dihedral) (wingRootHeight + radius * sin dihedral))
                         (scaleVec bend (Vec3 0 (-side*sin dihedral) (cos dihedral)))
      !position = addVec leadingEdge (scaleVec (-0.25 * chord) chordAxis)
      !normal0 = Vec3 0 (-side * sin dihedralLocal) (cos dihedralLocal)
      -- Resolve flow in the swept section plane. Ignoring sweep here removed
      -- its differential lift in sideslip, despite it being in the planform.
      !sweep = shapeSweepRad wp fraction + (mountRad - armRad) + sin mountRad * stroke
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
      !aspectRatio = shapeAspectRatio wp
  in \rollRate strokeRate commonRate ->
  let rates = Vec3 rollRate (vy (bodyRatesRadS input)) (vz (bodyRatesRadS input))
      flapVelocity = crossVec (Vec3 (side * strokeRate) 0 0)
                       (addVec position (Vec3 0 0 (-wingRootHeight)))
      elasticVelocity = addVec
        (scaleVec (stripBendRateMS old) (Vec3 0 (-side*sin dihedral) (cos dihedral)))
        (scaleVec (-0.25*chord*stripTwistRateRadS old) normal)
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
      -- Membrane camber follows the AERODYNAMIC normal load only: the static
      -- weight-support force is a body-weight reaction, not a circulation
      -- load, and must not cup the membrane. The ±12 clamp only bounds the
      -- q→0 division; the physical loads stay well inside it.
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
      normalPressure = surfacePressure area normalVelocity
      pressureForce = scaleVec normalPressure normal
      -- The speed-agnostic glue is a real surface pressure: it joins the
      -- aerodynamic force the membrane camber follows, so a stalled wing
      -- still cups from the pressure the field actually applies.
      aeroForce = addVec gripForce (addVec pressureForce (addVec chordForce (addVec normalForce spanDrag)))
      -- Static weight support: the "compressed air" lift field — a VERTICAL
      -- cushion under the wing that carries the strip's weight share at any
      -- flap angle or planform dihedral. No normal projection and no
      -- amplification: the field fills whatever the geometry cannot reach,
      -- so the body rides it at any stroke position and the servo is never
      -- stalled by a geometric feedback loop. Circulation lift takes the
      -- load over as the body builds HORIZONTAL speed. The gate must not
      -- follow the local flap velocity (that would strangle a hover) nor
      -- the sink speed (that would let a slow descent snowball); vertical
      -- body motion rides on the field itself.
      supportGate = supportHandoverMS * supportHandoverMS
                  / (supportHandoverMS * supportHandoverMS
                     + vx velocity * vx velocity + vy velocity * vy velocity)
      -- Position-based lift modulation on the AERO lift (the counter-rotation
      -- coupling): a wing held UP loses part of its aero lift, a held-DOWN
      -- wing gains it — odd in the deviation from the common stroke centre.
      liftMod = clamp 0.3 3.0 (1 - (if flapping then 2.4 else 8.0) * (stroke - centre))
      -- WORLD-translation modulation on the SUPPORT FIELD only (the user's
      -- "translated movement adds to the lift force"): a wing moving UP
      -- through the air loses part of its field counter-force, a wing pushed
      -- DOWN gains it. Kept off the aero lift so the inverted-geometry
      -- reflection stays exact; the field carries the weight, so the law
      -- lives there. When the glue holds the wing (world velocity ~0) no
      -- phantom couple arises and the roll stops with the sweep.
      liftModField = clamp 0.3 3.0 (1 - fieldTranslationSlope * z sectionVelocity / liftModVelRef)
      -- The field's rate response is the SYMMETRIC compression only: a
      -- flapping wing squeezes the compressed-air cushion (∝ |strokeRate|)
      -- and that squeeze carries the hover. The compression is EVEN in
      -- rate, so an aileron slew (equal and opposite rates on the two
      -- wings) produces no roll couple. The former LINEAR translation term
      -- (−fieldTranslationPerRate·fieldRate) made the down-going wing's
      -- support surge during the servo slew: the body rolled hard while
      -- the wings had barely deflected — the "magic roll" with no
      -- connection to the wing position. It is removed.
      fieldRate = max (-8) (min 8 strokeRate)
      -- The modulated part is the VELOCITY-PERPENDICULAR lift only (the force
      -- that counters gravity): a wing held UP loses part of it, a wing pushed
      -- DOWN carries more. The drag-aligned part stays untouched, so the
      -- surface can never extract energy from the relative flow, and the
      -- low-speed pressure floor keeps its clean velocity opposition.
      liftPart = scaleVec liftMod
                   (addVec (scaleVec (lift * flowX) normal)
                           (scaleVec (-lift * flowZ) chordAxis))
      dragPart = addVec (scaleVec (-drag * flowX) chordAxis)
                        (scaleVec (-drag * flowZ) normal)
      supportForce = scaleVec (liftModField * supportGate * supportN
                                * (1 + fieldCompressionPerRate * sqrt (fieldRate * fieldRate + 0.25))) (Vec3 0 0 1)
      -- Speed-agnostic DIFFERENTIAL pressure glue: the compressed-air field
      -- resists the wings' SCISSORING motion (aileron/roll) with a CONSTANT
      -- Coulomb-like force from the minimum dynamic pressure ½ρ·Cp·v0² —
      -- speed-agnostic, load-independent pressure drag. The common flap
      -- stroke (both wings beating together) is EXEMPT: the symmetric beat
      -- compresses the field (|rate| term) while the differential shears
      -- it, so the aileron is glued to the air at every speed and cannot
      -- slip "as if it was nothing", yet the servo can still flap. The
      -- reaction flows to the body through the hinge: a slow aileron sweep
      -- leaves the wings horizontal in the world while the fuselage
      -- counter-rotates the commanded differential, and a spinning body is
      -- damped hard because its roll rate IS differential wing motion.
      commonFlapVel = crossVec (Vec3 (side * commonRate) 0 0)
                       (addVec position (Vec3 0 0 (-wingRootHeight)))
      glueVel = addVec sectionVelocity (scaleVec (-1) commonFlapVel)
      glueNormalVel = dot glueVel normal
      glueChordVel = dot glueVel chordAxis
      glueSpanVel = dot glueVel spanAxis
      tangentialSpeed = sqrt (glueChordVel * glueChordVel + glueSpanVel * glueSpanVel)
      gripLoad = 0.5 * airDensity * area * surfacePressureCp * glueFloorMS * glueFloorMS
      gripNormalMag = gripLoad * tanh (glueNormalVel / glueFloorMS)
      gripShearMag = 0.5 * airDensity * area * glueShearCp * glueFloorMS * glueFloorMS
                     * tanh (tangentialSpeed / glueFloorMS)
      gripForce = addVec (scaleVec (-gripNormalMag) normal)
                   (scaleVec (-gripShearMag / max 1e-9 tangentialSpeed)
                             (addVec (scaleVec glueChordVel chordAxis) (scaleVec glueSpanVel spanAxis)))
      force = addVec supportForce
                (addVec gripForce
                  (addVec dragPart (addVec pressureForce (addVec spanDrag liftPart))))
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
      -- Section Cm is positive nose-up. With body +x forward,+y right,+z up,
      -- the axial +Y moment rotates the nose DOWN, so convert the sign here.
      -- Keep the scalar section moment in its original convention for twist.
      moment = addVec (crossVec position force)
                      (scaleVec (-side * sectionPitchMoment) spanAxis)
      power = max 0 (negate (dot force flapVelocity))
      next = StripState relaxed alpha normalVelocity lev camberNext bend
               (stripBendSlope old) aeroTwist (stripSpringLoad old)
               (stripBendRateMS old) (stripTwistRateRadS old) gripLoad
  in StripResult next force moment power sectionTwistMoment

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

-- | Two reduced structural modes: cantilever bending and leading-edge torsion.
-- The static beam compliance defines each mode shape and stiffness; distributed
-- wing mass defines modal inertia. Pressure drives the modes, whose velocities
-- feed the local relative flow on the following step. This captures structural
-- ringing, rather than treating waveform dwell as physical resonance.
applyStructure :: Double -> Double -> SparInventory -> WingShape -> VehicleInput -> [StripResult] -> [StripResult]
applyStructure side stroke inv wp input results
  | null results = []
  | otherwise =
  let dt = stepSeconds input
      dr = wsSpanM wp / fromIntegral stripCount
      fracs = [ (fromIntegral i + 0.5) / fromIntegral stripCount | i <- [0 .. length results - 1] ]
      chords = map (shapeChord wp) fracs
      areas = map (*dr) chords
      weights = map (/max 1e-12 (sum areas)) areas
      bendingEIs = map (sparBendEISpanwise inv) fracs
      twistGJs = map (sparTwistGJSpanwise inv) fracs
      (unitBend,_) = integrateFlapBeam weights bendingEIs dr
      unitTwist = integrateTwist weights twistGJs dr
      bendCompliance = max 1e-12 (last unitBend)
      twistCompliance = max 1e-12 (last unitTwist)
      bendShape = map (/bendCompliance) unitBend
      twistShape = map (/twistCompliance) unitTwist
      dotList a b = sum (zipWith (*) a b)
      bendK = dotList weights bendShape / bendCompliance
      twistK = dotList weights twistShape / twistCompliance
      masses = map (*referenceWingMassKg) weights
      -- Flat-plate added air inertia, consistent with the hinge's added mass.
      -- Translation/pitch coupling is about the leading edge elastic axis.
      airMasses = map (\c -> airDensity*pi*c*c*dr/4) chords
      totalMasses = zipWith (+) masses airMasses
      bendMass = dotList totalMasses (map (\x -> x*x) bendShape)
      crossMass = sum (zipWith4 (\m c b t -> -m*c*b*t/2) totalMasses chords bendShape twistShape)
      pitchInertias = zipWith3 (\m a c -> m*c*c/3+a*c*c/4+airDensity*pi*c**4*dr/128) masses airMasses chords
      twistMass = dotList pitchInertias (map (\x -> x*x) twistShape)
      normalLoad f r =
        let a = stroke*cos (radians (wsMountAngleDeg wp)) + shapeDihedralRad wp f
            force = resultForce r
        in (-side*sin a)*vy force + cos a*vz force
      bendLoad = dotList bendShape (zipWith normalLoad fracs results)
      twistLoad = dotList twistShape (map resultSectionMoment results)
      tip = resultState (last results)
      -- Provisional material damping ratio; aerodynamic damping additionally
      -- emerges from pressure opposing elastic surface velocity.
      damping mass stiffness = 2*0.25*sqrt (mass*stiffness)
      ((bend,twist),(bendRate,twistRate)) = stepCoupledElasticModes dt
          (bendMass,crossMass,twistMass) (bendK,twistK)
          (damping bendMass bendK,damping twistMass twistK) (bendLoad,twistLoad)
          (stripBendM tip,stripTwistAero tip) (stripBendRateMS tip,stripTwistRateRadS tip)
      bends = map (*bend) bendShape
      slopes = zipWith (\q previous -> (q-previous)/dr) bends (0:bends)
      update r phi psi slope =
        let old=resultState r
            q=phi*bend; angle=psi*twist
            diagnostic=clamp 0 1 (0.5*abs q/(0.10*wsSpanM wp)+0.5*abs angle/radians 15)
        in r {resultState=old {stripBendM=q, stripBendSlope=slope,
                    stripTwistAero=angle, stripBendRateMS=phi*bendRate,
                    stripTwistRateRadS=psi*twistRate,
                    stripSpringLoad=relaxDeflection dt springLoadTau (stripSpringLoad old) diagnostic}}
  in zipWith4 update results bendShape twistShape slopes

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