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
  , flapJetCoeff
  , flapDampingCd
  , flapUpstrokeDampingFraction
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

import Data.List (zipWith5)

import Born2Flap.Math.Types
import Born2Flap.Math.Simulation
import Born2Flap.Math.Wing (crossflowBaseline, relaxSeparation)
import Born2Flap.Math.Model (WingModel(..), kestrelModel)
import Born2Flap.Math.Planform
  ( WingShape(..), shapeChord
  , shapeSweepRad, shapeTwistRad, shapeDihedralRad, shapeAspectRatio, shapeSection, shapeStructure )
import Born2Flap.Math.Section
  ( SectionProfile(..), zeroLiftAngle, sectionPitchMomentCoeff
  , sectionTwistLever, sectionTwistMomentCoeff, membraneCamberTarget, relaxCamber )
import Born2Flap.Math.Structure
  ( StructureProfile(..), SparInventory, integrateFlapBeam, integrateTwist, relaxDeflection, softSaturate
  , sparBendEISpanwise, sparTwistGJSpanwise )
import Born2Flap.Math.Waveform
  ( OscillatorState(..), defaultOscillator, advanceOscillator
  , limiarFromFerocities, shapeWaveWithDerivative )

-- | Air density [kg/m³] across the whole flight envelope. Tuned a touch thin
-- (≈ 850 m ASL) so the air reads light and the bird fast; flap thrust is
-- boosted separately via 'flapJetCoeff' so wing beats still bite hard.
airDensity :: Double
airDensity = 1.05

-- | Momentum-jet thrust factor for the flapping downstroke (dimensionless).
-- The downstroke sheds a backward jet whose reaction propels the bird. This is
-- the ONLY free gain in the flap force budget and must be calibrated against
-- measured total force, not a textbook C_T. Lift-tilt alone yields thrust at
-- rest but goes NEGATIVE once the bird has forward speed (the flap's lift
-- vector tilts aft), so the momentum jet is what actually sustains cruise —
-- it must overcome the lift-tilt drag at the bird's maximum airspeed (~7 m/s).
-- C_T is therefore sized so net thrust stays positive to 7 m/s, not to a
-- textbook value. At the kestrel's 3.2 Hz / 20° stroke this is ~0.6.
flapJetCoeff :: Double
flapJetCoeff = 0.6

-- | Lumped pressure-drag coefficient for the wing's OWN flap motion — the broad
-- surface sweeping normal to itself through air. A thin airfoil moving edge-on
-- has Cd ≈ 0.02, but a wing moving normal to its own surface is a flat plate
-- (Cd ≈ 1.2), and a rotating cambered wing additionally sweeps added-mass /
-- radial momentum flux, so its effective lumped value sits a touch above the
-- bare translating-plate figure. This is the "air resistance" a wing feels when
-- it moves up/down (flap stroke or differential-dihedral roll).
--
-- CRITICAL: the flap hinge torque this force produces is ½·ρ·Cd·ω²·∫chord·r³dr.
-- At the servo's 1200°/s peak rate (ω ≈ 20.9 rad/s) on the 1.0 m membrane wing
-- the integral ≈ 0.024 m⁴, so hinge torque ≈ 5.4·Cd [N·m]. Raising Cd to chase
-- ROLL AUTHORITY therefore back-fires hard: Cd=50 → ~270 N·m, which back-drives
-- even a 20 N·m servo into its ±80° end-stop and latches it there, and the same
-- huge normal force bends the wing to its saturation clamp (the "deformed and
-- stuck" failure). Roll authority must NOT come from flat-plate drag — it belongs
-- to the differential-dihedral LIFT tilt (aileronCmd shifting the stroke centre)
-- and to the V-tail, both of which use lift, not drag. Keep Cd at the physical
-- flat-plate figure so the wing BITES the air via lift while its flap motion
-- merely sees normal flat-plate resistance.
flapDampingCd :: Double
flapDampingCd = 0.9

-- | Fraction of 'flapDampingCd' the wing keeps on the UPSTROKE during flapping.
-- A real bird feathers the recovery stroke so the downstroke (power stroke) bites
-- full-pressure air while the upstroke sheds nearly all of it; that asymmetry is
-- what turns the symmetric pressure drag into NET lift + roll authority instead of
-- a loss term that merely loads the servo. In glide (differential-dihedral roll)
-- there is no flapping, so BOTH wings keep the full Cd and roll authority stays
-- symmetric. Only the flapping upstroke sheds damping.
flapUpstrokeDampingFraction :: Double
flapUpstrokeDampingFraction = 0.35

-- | Aeroelastic thrust boost (dimensionless). A cambered (membrane-cupped)
-- section sheds a stronger backward jet than a flat plate: the deeper the cup
-- the membrane holds under load (see 'membraneCamberTarget'), the more flap
-- flow it redirects aft, which is the LE/TE stiffness asymmetry a real bird
-- uses to make thrust. A rigid flat wing (camber ≈ 0) recovers none of this —
-- it just disperses the stroke force outward instead of propelling. This is the
-- one term that makes thrust GROW with the aeroelastic state, so the wing gets
-- stronger (not weaker) as it loads up into cruise.
aeroCamberThrustGain :: Double
aeroCamberThrustGain = 6.0

-- | Forward-advance thrust gain (dimensionless): the cross-term between the
-- oncoming chordwise flow and the flap velocity. A flapping wing at forward
-- speed pushes the oncoming air backward, so its thrust GROWS with airspeed —
-- this is what lets a bird keep biting the air into a headwind, where the
-- static momentum jet (∝ flap-speed²) has already collapsed. Vanishes at rest
-- (chordVelocity ≈ 0), so hover thrust is unchanged.
flapAdvanceThrustGain :: Double
flapAdvanceThrustGain = 1.0

-- | Ground-effect constants (dimensionless). Ground effect is the image-vortex
-- cushion a wing feels close to a surface. For a GLIDING wing it only boosts
-- lift (higher effective aspect ratio → less downwash → more Cl at the same
-- incidence). For a FLAPPING wing it does something a fixed wing cannot: the
-- stroke's pressure unloads against the ground, so the down/up-stroke shed a
-- stronger jet (more thrust) while the stroke's own flat-plate pressure drag
-- falls away — which is why a bird both lifts AND accelerates in ground effect.
groundEffectLiftGain :: Double
groundEffectLiftGain = 0.30

-- | Fraction of induced + stroke pressure drag removed at the deck (flapping).
groundEffectDragRelief :: Double
groundEffectDragRelief = 0.55

-- | Extra downstroke thrust at the deck (flapping).
groundEffectThrustGain :: Double
groundEffectThrustGain = 0.20

-- | Upstroke thrust recovery in ground effect, as a fraction of the downstroke
-- jet. The normally-feathered upstroke only pushes off the ground cushion.
groundEffectUpstrokeRecover :: Double
groundEffectUpstrokeRecover = 0.25

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
emptyStrip = StripState 0 0 0 0 0 0 0 0

-- | Strips initialised to the rest camber of the default wing (so the membrane
-- model starts from the built-in camber line, not a flat plate).
initialStrips :: [StripState]
initialStrips =
  [ StripState 0 0 0 0 (spRestCamber (shapeSection (wmWing kestrelModel) fraction)) 0 0 0
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
  in stepWingWithStroke True side stroke strokeRate input kestrelModel 0 states

-- | Step one wing given the *actual* flap angle [rad] and flap rate [rad/s],
-- independent of how they were produced (firmware mixer + servo, or the
-- legacy throttle drive). This is the primitive the firmware-emulation loop
-- uses to close the aero→servo-load feedback.
stepWingWithStroke :: Bool -> Double -> Double -> Double -> VehicleInput -> WingModel -> Double -> [StripState]
                   -> ([StripResult], [StripState])
stepWingWithStroke flapping side stroke strokeRate input model incidenceRad states =
  let wp = wmWing model
      raw = zipWith (\i s -> stepStrip False strokeRate flapping side stroke input incidenceRad wp i s (vx (bodyRatesRadS input)) strokeRate) [0 ..] states
      transported = transportSeparation side input wp raw
      deformed = applyStructure (wmSpars model) wp input transported
  in (deformed, map resultState deformed)

-- Force-only evaluation for the implicit roll Jacobian. Structural relaxation
-- and separation transport only modify next state; commit those once after
-- solving, rather than doing a full beam solve at every trial velocity.
wingLoadsWithStroke :: Bool -> Double -> Double -> Double -> Double -> VehicleInput -> WingModel -> [StripState] -> [StripResult]
wingLoadsWithStroke flapping side stroke rate phaseRate input model states =
  prepareWingLoads flapping side stroke phaseRate input model states (vx (bodyRatesRadS input)) rate

-- Cache the fixed geometry and aerodynamic history outside Newton's candidate
-- velocities. Rebuilding every strip for every finite-difference column made
-- the 240 Hz physics consume most of the game thread on small aircraft.
prepareWingLoads :: Bool -> Double -> Double -> Double -> VehicleInput -> WingModel -> [StripState]
                 -> Double -> Double -> [StripResult]
prepareWingLoads flapping side stroke phaseRate input model states =
  let strips = zipWith (stepStrip True phaseRate flapping side stroke input 0 (wmWing model)) [0..] states
  in \rollRate rate -> map (\strip -> strip rollRate rate) strips

-- Commit aero memory/structure after the implicit solve using the same loads
-- as its residual. Hysteretic state transitions are not Newton unknowns.
stepCoupledWing :: Bool -> Double -> Double -> Double -> Double -> VehicleInput -> WingModel -> [StripState]
                -> ([StripResult], [StripState])
stepCoupledWing flapping side stroke rate phaseRate input model states =
  let wp=wmWing model
      raw=wingLoadsWithStroke flapping side stroke rate phaseRate input model states
      results=applyStructure (wmSpars model) wp input (transportSeparation side input wp raw)
  in (results,map resultState results)

-- High wing / low centre of mass. The side-force lever contributes natural
-- lateral stability; it is not an attitude-dependent restoring torque.
wingRootHeight :: Double
wingRootHeight = 0.04

stepStrip :: Bool -> Double -> Bool -> Double -> Double -> VehicleInput -> Double -> WingShape -> Int -> StripState -> Double -> Double -> StripResult
{-# NOINLINE stepStrip #-}
stepStrip implicitLoads _phaseRate _flapping side stroke input incidenceRad wp index old =
  let !dt = stepSeconds input
      !fraction = (fromIntegral index + 0.5) / fromIntegral stripCount
      !spanM = wsSpanM wp
      !dr = spanM / fromIntegral stripCount
      -- Spanwise lever arm from the shoulder hinge: the wing root sits at the
      -- shoulder distance outboard of the body centreline, then the wing's own
      -- span runs root→tip.
      !radius = wsShoulderM wp + dr * (fromIntegral index + 0.5)
      !chord = shapeChord wp fraction
      !twist = shapeTwistRad wp fraction + incidenceRad
      !dihedral = stroke + shapeDihedralRad wp fraction
      !profile = shapeSection wp fraction
      !camberPrev = stripCamber old
      !aeroTwist = stripTwistAero old
      !bend = stripBendM old
      -- Ground effect: proximity to the surface below, normalised by the total
      -- wingspan (2 × half-span). Each strip sees its OWN flap height, so the
      -- downstroke pushes the tip into the cushion and the upstroke lifts it
      -- back out — the effect is strongest at the bottom of the beat.
      !stripHeightM = wingRootHeight + radius * sin dihedral + bend
      !ge = groundEffectFactor (groundClearanceM input + stripHeightM) (2 * wsSpanM wp)
      !velocity = bodyVelocityMS input
      -- Use section velocity through air throughout (not a mixture of air
      -- velocity and body velocity). Drag must do negative work. The normal
      -- rotates with the flap; positive stroke raises BOTH wings.
      !position = Vec3 0 (side * radius * cos dihedral) (wingRootHeight + radius * sin dihedral + bend)
      !normal = Vec3 0 (-side * sin dihedral) (cos dihedral)
      -- Resolve flow in the swept section plane. Ignoring sweep here removed
      -- its differential lift in sideslip, despite it being in the planform.
      !sweep = shapeSweepRad wp fraction
      !chordAxis = Vec3 (cos sweep) (side * sin sweep * cos dihedral) (sin sweep * sin dihedral)
      !spanAxis = Vec3 (-sin sweep) (side * cos sweep * cos dihedral) (cos sweep * sin dihedral)
      -- Flap velocity is the wing surface's motion through the air about the
      -- shoulder hinge. It exists in BOTH flapping and glide: in glide a
      -- differential-dihedral (roll) command rotates the wing, so the surface
      -- genuinely sweeps through the air and RESISTS. That resistance, acting
      -- at the spanwise lever arm (position), is what rolls the fuselage — the
      -- fuselage/tail sit on the centreline and carry no roll lever arm.
      -- Feathering (passive pitch) stays a flapping-stroke phenomenon (below).
      !aspectRatio = shapeAspectRatio wp
  in \rollRate strokeRate ->
  let rates = Vec3 rollRate (vy (bodyRatesRadS input)) (vz (bodyRatesRadS input))
      flapVelocity = scaleVec (radius * strokeRate) normal
      sectionVelocity = addVec velocity (addVec (crossVec rates position) flapVelocity)
      dot (Vec3 a b c) (Vec3 d e f) = a*d + b*e + c*f
      chordVelocity = dot sectionVelocity chordAxis
      normalVelocity = dot sectionVelocity normal
      spanVelocity = dot sectionVelocity spanAxis
      planarSpeed = sqrt (chordVelocity * chordVelocity + normalVelocity * normalVelocity)
      safeSpeed = max 1.0e-6 planarSpeed
      -- Aeroelastic twist adds to the geometric incidence: a load-induced washout
      -- reduces the local angle of attack, closing the bending→aerodynamics loop.
      -- Asymmetric passive feathering of the flexible wing during a stroke.
      -- The upstroke feathers fully (sheds nearly all aerodynamic load, like a
      -- real bird recovering the wing) while the downstroke feathers only
      -- enough to hold the section at its stall onset — maximum lift without
      -- the rigid-plate 90° stall that used to kill both thrust and lift.
      -- No flap motion means no feathering; body sink still changes incidence.
      worldStrokeRate = strokeRate + side * rollRate
      sweepNormal = radius * worldStrokeRate
      feather =
        let inflow = atan2 sweepNormal (max 1.5 (abs chordVelocity))
            compliance = if sweepNormal > 0 then 1.0 else 0.85
        in compliance * inflow
      -- A 2-servo ornithopter has one actuator per wing: the flap hinge. Pitch
      -- and roll reach the wing through the flap angle (stroke-centre shift and
      -- differential), never through a separate incidence twist. Angle of attack
      -- is the passive sum of geometric washout, aeroelastic twist, flap-induced
      -- feathering and the local flow direction.
      alpha = twist + aeroTwist + feather + atan2 (-normalVelocity) chordVelocity
      alphaEff = alpha - zeroLiftAngle camberPrev
      alphaRate = (alpha - stripPreviousAlpha old) / dt
      reynolds = planarSpeed * chord / 1.48e-5
      stallAngle = radians (clamp 10 18 (16 - 1.4 * logBase 10 (max 1 (150000 / reynolds))))
      dynamicDelay = clamp (-0.16) 0.16 (0.018 * alphaRate)
      target = smoothStep (stallAngle - radians 3) (stallAngle + radians 4)
                (abs alphaEff - dynamicDelay)
      separating = target > stripSeparation old
      tau = if separating then 0.045 else 0.090
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
      liftSlope = 2 * pi * aspectRatio / (aspectRatio + 2)
      attachedCl = clamp (-1.9) 1.9 (liftSlope * alphaEff)
      separatedCl = 1.05 * sin (2 * alphaEff)
      rotationalCl = clamp (-0.65) 0.65 (0.5 * chord * alphaRate / safeSpeed)
      -- Keep separation/LEV memory fixed during the implicit mechanical solve.
      -- Updating its discrete vortex gate at each trial velocity can leave no
      -- root at all. Advance that memory once in `next` after convergence.
      forceSeparation = if implicitLoads then stripSeparation old else relaxed
      vortexLift = if implicitLoads
        then signum (stripPreviousAlpha old - zeroLiftAngle camberPrev) * 0.45 * stripLevStrength old
        else signum alphaEff * 0.45 * lev
      cl = lerp attachedCl separatedCl forceSeparation + rotationalCl + vortexLift
      camberNext = relaxCamber dt (spMembraneTau profile) camberPrev
                     (membraneCamberTarget profile attachedCl)
      -- Induced drag is the cost of the CIRCULATION (bound-vortex) lift only.
      -- Rotational lift (Kramer effect) and LEV lift are transient/separated
      -- mechanisms that do NOT shed the trailing downwash; charging them
      -- Cl²/(πeAR) inflated the drag at high alpha and coupled it to the lagged
      -- LEV/separation state — the sway feedback loop. Square the attached
      -- (circulation) lift instead. Ground effect is the classic victim: the
      -- image vortex cancels downwash, so the wake tilts less and the bird
      -- glides faster.
      inducedRelief = max 0 (1 - groundEffectDragRelief * ge)
      inducedCd = attachedCl * attachedCl / (pi * 0.82 * aspectRatio) * inducedRelief
      attachedCd = 0.016 + inducedCd
      separatedCd = 0.20 + 1.20 * sin alphaEff * sin alphaEff
      cd = lerp attachedCd separatedCd forceSeparation
      sideCd = 0.018 + 0.08 * forceSeparation
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
      -- Flapping thrust (momentum theory): the downstroke sheds a backward jet,
      -- so the wing receives a forward reaction proportional to the flap-induced
      -- dynamic pressure and the strip area. Only on the downstroke (strokeRate
      -- < 0 = wing descending); the upstroke is feathered and contributes none.
      -- This is what turns the otherwise drag-dominated flap into net propulsion.
      -- The jet coefficient (2.5) is the momentum-jet thrust factor for a
      -- flapping wing at its own planform area: the downstroke accelerates a
      -- column of air comparable to the wing chord, so C_T ≈ 2 is typical —
      -- 0.70 left a 49 g kestrel with NEGATIVE cruise thrust (measured -0.23 N
      -- at 6 m/s) and it could never accelerate to flying speed.
      -- The downstroke jet is amplified in ground effect (the stroke pressure
      -- pushes off the surface) and — uniquely — the normally-feathered
      -- upstroke recovers a fraction of that jet too, so the bird accelerates
      -- in the cushion where a gliding wing only gains lift.
      camberBoost = 1.0 + aeroCamberThrustGain * max 0 camberPrev
      -- Two momentum-jet components: the static jet (∝ flap speed²) that
      -- exists at hover, and the advance jet (∝ forward flow × flap speed)
      -- that only appears at speed and is what sustains cruise into a
      -- headwind. Both scale with the aeroelastic camber (the deeper the
      -- membrane cups under load, the more flow it redirects aft).
      flapSpeed = abs (radius * worldStrokeRate)
      staticJet = 0.5 * airDensity * area * flapSpeed * flapSpeed * flapJetCoeff * camberBoost
      advanceJet = 0.5 * airDensity * area * max 0 chordVelocity * flapSpeed * flapJetCoeff * camberBoost * flapAdvanceThrustGain
      flapThrust = let jet = staticJet + advanceJet
                           in if worldStrokeRate < 0
                                then jet * (1 + groundEffectThrustGain * ge)
                                else jet * groundEffectUpstrokeRecover * ge
      thrustForce = Vec3 flapThrust 0 0
      -- Flat-plate pressure drag on the wing surface moving normal to itself.
      -- This is the "air resistance" a broad wing feels when it sweeps up/down
      -- (flap stroke or differential-dihedral roll): independent of the
      -- angle-of-attack lift/drag machinery, which uses a thin-edge section
      -- drag and would otherwise let the wing slice through air almost free.
      -- Opposes the wing surface's FULL normal velocity -- flap stroke plus the
      -- body's own translation/rotation -- so a rolling body also drags the wing
      -- through the air (and the reaction at the shoulder rolls the fuselage).
      -- One pressure law in glide and flap. A feathered membrane presents
      -- less normal area/flow because its physical normal rotates, rather
      -- than because a throttle-mode flag changes its drag coefficient.
      pressureNormal = addVec (scaleVec (cos feather) normal) (scaleVec (-sin feather) chordAxis)
      pressureVelocity = dot sectionVelocity pressureNormal
      flapDampingMag = -0.5 * airDensity * area * flapDampingCd
                       * pressureVelocity * abs pressureVelocity
      flapDampingForce = scaleVec flapDampingMag pressureNormal
      force = addVec (addVec chordForce (addVec normalForce spanDrag))
                     (addVec thrustForce flapDampingForce)
      -- Explicit differencing of BODY acceleration as an added-mass force
      -- creates a delayed feedback loop at contacts. Omit this term until an
      -- implicit fluid/body inertia solve is available; do not clamp it into
      -- a fictitious 25 N source at every strip.
      cm0 = sectionPitchMomentCoeff camberPrev (spReflex profile)
      sectionPitchMoment = cm0 * q * chord * chord * dr
      -- Twist-driving moment about the LE elastic axis: the camber/reflex AC
      -- moment plus the lift acting at the 0.25c AC offset. Load-proportional —
      -- positive cl deepens washout (gust/stall relief, delays tip stall), and
      -- the upstroke's reversed lift twists the outer wing nose-up (wash-in),
      -- the reversal a real hand-wing shows. This feeds the spanwise torsion
      -- only; the body pitching moment above still uses the AC moment.
      twistLever = sectionTwistLever fraction
      sectionTwistMoment = sectionTwistMomentCoeff camberPrev (spReflex profile) twistLever cl
                            * q * chord * chord * dr
      -- Section Cm is positive nose-up. With body +x forward,+y right,+z up,
      -- the axial +Y moment rotates the nose DOWN, so convert the sign here.
      -- Keep the scalar section moment in its original convention for twist.
      moment = addVec (crossVec position force) (Vec3 0 (-sectionPitchMoment) 0)
      power = max 0 (negate (dot force flapVelocity))
      next = StripState relaxed alpha normalVelocity lev camberNext bend
               (stripBendSlope old) aeroTwist
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
          diffusion = 0.018 * (separationAt (i - 1) - 2 * current + separationAt (i + 1))
          newSeparation = clamp 0 1 (current + advected + diffusion)
          oldState = resultState result
      in result { resultState = oldState { stripSeparation = newSeparation } }

-- | Structural pass: compute the quasi-static cantilever deflection/twist from
-- the just-computed aerodynamic loads, relax the strip deformation state toward
-- it, and (implicitly, via the state) feed the deformed shape into the next
-- aero step. Root is clamped; the tip is free.
applyStructure :: SparInventory -> WingShape -> VehicleInput -> [StripResult] -> [StripResult]
applyStructure inv wp input results =
  let dt = stepSeconds input
      count = length results
      spanM = wsSpanM wp
      dr = spanM / fromIntegral stripCount
      fracs = [ (fromIntegral i + 0.5) / fromIntegral stripCount | i <- [0 .. count - 1] ]
      structures = map (shapeStructure wp) fracs
      -- Spanwise spar stiffness (inner → outer main spar; braces inboard) replaces
      -- the interpolated station structure for EI/GJ. The station structure still
      -- supplies the relaxation time constants below. Stiffness comes straight
      -- from the model's spar inventory — no hidden scale factor.
      bendingEIs = map (sparBendEISpanwise inv) fracs
      twistGJs   = map (sparTwistGJSpanwise inv) fracs
      forces = map (vz . resultForce) results
      sectionMoments = map resultSectionMoment results
      (targetBend, targetSlope) = integrateFlapBeam forces bendingEIs dr
      targetTwist = integrateTwist sectionMoments twistGJs dr
      maxBend = 0.15 * wsSpanM wp
      maxTwist = radians 15
      update result structure bend slope twist =
        let old = resultState result
            nextBend = softSaturate maxBend
                          (relaxDeflection dt (stTauBend structure) (stripBendM old) bend)
            nextSlope = relaxDeflection dt (stTauBend structure) (stripBendSlope old) slope
            nextTwist = softSaturate maxTwist
                          (relaxDeflection dt (stTauTwist structure) (stripTwistAero old) twist)
        in result { resultState = old { stripBendM = nextBend
                                      , stripBendSlope = nextSlope
                                      , stripTwistAero = nextTwist } }
  in zipWith5 update results structures targetBend targetSlope targetTwist

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