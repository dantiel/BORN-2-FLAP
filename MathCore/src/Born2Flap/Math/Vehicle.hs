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
  , emptyStrip
  , initialStrips
  , stepWingWithStroke
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
import Born2Flap.Math.Planform
  ( WingShape(..), defaultBirdWing, shapeChord
  , shapeSweepRad, shapeTwistRad, shapeAspectRatio, shapeSection, shapeStructure )
import Born2Flap.Math.Section
  ( SectionProfile(..), zeroLiftAngle, sectionPitchMomentCoeff
  , membraneCamberTarget, relaxCamber )
import Born2Flap.Math.Structure
  ( StructureProfile(..), integrateFlapBeam, integrateTwist, relaxDeflection )
import Born2Flap.Math.Waveform
  ( OscillatorState(..), defaultOscillator, advanceOscillator
  , limiarFromFerocities, shapeWaveWithDerivative )

data VehicleInput = VehicleInput
  { stepSeconds :: !Double
  , bodyVelocityMS :: !Vec3
  , bodyRatesRadS :: !Vec3
  , throttleCommand :: !Double
  , rollCommand :: !Double
  , pitchCommand :: !Double
  , yawCommand :: !Double
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
  , resultSectionMoment :: !Double  -- ^ sectional pitching moment [N·m] (nose-up +)
  } deriving stock (Eq, Show)

stripCount :: Int
stripCount = 16

emptyStrip :: StripState
emptyStrip = StripState 0 0 0 0 0 0 0 0

-- | Strips initialised to the rest camber of the default wing (so the membrane
-- model starts from the built-in camber line, not a flat plate).
initialStrips :: [StripState]
initialStrips =
  [ StripState 0 0 0 0 (spRestCamber (shapeSection defaultBirdWing fraction)) 0 0 0
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
          bodyDrag = scaleVec (-0.5 * 1.225 * 0.032 * speed) (bodyVelocityMS input)
          tailArm = Vec3 (-0.48) 0 0
          tailVelocity = addVec (bodyVelocityMS input) (crossVec rates tailArm)
          tailQArea = 0.5 * 1.225 * 0.035 * magnitude tailVelocity * magnitude tailVelocity
          -- Fixed tailplane incidence: a small, always-on DOWN force (negative z
          -- lift) proportional to dynamic pressure, providing pitch trim; the
          -- stick adds control lift on top of it.
          tailTrimDownforce = -0.18 * tailQArea
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
  in stepWingWithStroke side stroke strokeRate input states

-- | Step one wing given the *actual* flap angle [rad] and flap rate [rad/s],
-- independent of how they were produced (firmware mixer + servo, or the
-- legacy throttle drive). This is the primitive the firmware-emulation loop
-- uses to close the aero→servo-load feedback.
stepWingWithStroke :: Double -> Double -> Double -> VehicleInput -> [StripState]
                   -> ([StripResult], [StripState])
stepWingWithStroke side stroke strokeRate input states =
  let raw = zipWith (stepStrip side stroke strokeRate input) [0 ..] states
      transported = transportSeparation side input raw
      deformed = applyStructure input transported
  in (deformed, map resultState deformed)

stepStrip :: Double -> Double -> Double -> VehicleInput -> Int -> StripState -> StripResult
stepStrip side stroke strokeRate input index old =
  let dt = stepSeconds input
      wp = defaultBirdWing
      fraction = (fromIntegral index + 0.5) / fromIntegral stripCount
      spanM = wsSpanM wp
      dr = spanM / fromIntegral stripCount
      radius = dr * (fromIntegral index + 0.5)
      chord = shapeChord wp fraction
      twist = shapeTwistRad wp fraction
      profile = shapeSection wp fraction
      camberPrev = stripCamber old
      aeroTwist = stripTwistAero old
      bend = stripBendM old
      velocity = bodyVelocityMS input
      rates = bodyRatesRadS input
      -- Use section velocity through air throughout (not a mixture of air
      -- velocity and body velocity). Drag must do negative work. The normal
      -- rotates with the flap; positive stroke raises BOTH wings.
      position = Vec3 0 (side * radius * cos stroke) (radius * sin stroke + bend)
      normal = Vec3 0 (-side * sin stroke) (cos stroke)
      spanAxis = Vec3 0 (side * cos stroke) (sin stroke)
      flapVelocity = scaleVec (radius * strokeRate) normal
      sectionVelocity = addVec velocity (addVec (crossVec rates position) flapVelocity)
      dot (Vec3 a b c) (Vec3 d e f) = a*d + b*e + c*f
      chordVelocity = vx sectionVelocity
      normalVelocity = dot sectionVelocity normal
      spanVelocity = dot sectionVelocity spanAxis
      planarSpeed = sqrt (chordVelocity * chordVelocity + normalVelocity * normalVelocity)
      safeSpeed = max 1.0e-6 planarSpeed
      -- Aeroelastic twist adds to the geometric incidence: a load-induced washout
      -- reduces the local angle of attack, closing the bending→aerodynamics loop.
      alpha = twist + aeroTwist + atan2 (-normalVelocity) chordVelocity
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
      attachedCl = clamp (-1.9) 1.9 (2 * pi * alphaEff)
      separatedCl = 1.05 * sin (2 * alphaEff)
      rotationalCl = clamp (-0.65) 0.65 (0.5 * chord * alphaRate / safeSpeed)
      cl = lerp attachedCl separatedCl relaxed + rotationalCl + signum alphaEff * 0.45 * lev
      camberNext = relaxCamber dt (spMembraneTau profile) camberPrev
                     (membraneCamberTarget profile attachedCl)
      aspectRatio = shapeAspectRatio wp
      inducedCd = cl * cl / (pi * 0.82 * aspectRatio)
      attachedCd = 0.025 + inducedCd
      separatedCd = 0.20 + 1.20 * sin alphaEff * sin alphaEff
      cd = lerp attachedCd separatedCd relaxed
      sideCd = 0.018 + 0.08 * relaxed
      area = chord * dr
      q = 0.5 * 1.225 * planarSpeed * planarSpeed
      lift = q * area * cl
      drag = q * area * cd
      flowX = chordVelocity / safeSpeed
      flowZ = normalVelocity / safeSpeed
      -- Lift is orthogonal to section velocity, drag opposes it, including
      -- reverse flow. In particular a passive falling wing cannot add energy.
      chordForce = Vec3 (-drag * flowX - lift * flowZ) 0 0
      normalForce = scaleVec (-drag * flowZ + lift * flowX) normal
      spanDrag = scaleVec (-0.5 * 1.225 * area * sideCd * abs spanVelocity * spanVelocity) spanAxis
      force = addVec chordForce (addVec normalForce spanDrag)
      -- Explicit differencing of BODY acceleration as an added-mass force
      -- creates a delayed feedback loop at contacts. Omit this term until an
      -- implicit fluid/body inertia solve is available; do not clamp it into
      -- a fictitious 25 N source at every strip.
      cm0 = sectionPitchMomentCoeff camberPrev (spReflex profile)
      sectionPitchMoment = cm0 * q * chord * chord * dr
      moment = addVec (crossVec position force) (Vec3 0 sectionPitchMoment 0)
      power = max 0 (negate (dot force flapVelocity))
      next = StripState relaxed alpha normalVelocity lev camberNext bend
               (stripBendSlope old) aeroTwist
  in StripResult next force moment power sectionPitchMoment

transportSeparation :: Double -> VehicleInput -> [StripResult] -> [StripResult]
transportSeparation _side input results = zipWith update [0 ..] results
  where
    dt = stepSeconds input
    dr = wsSpanM defaultBirdWing / fromIntegral stripCount
    count = length results
    separationAt i = stripSeparation . resultState $ results !! clampInt 0 (count - 1) i
    update i result =
      let fraction = (fromIntegral i + 0.5) / fromIntegral stripCount
          sweep = shapeSweepRad defaultBirdWing fraction
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
applyStructure :: VehicleInput -> [StripResult] -> [StripResult]
applyStructure input results =
  let dt = stepSeconds input
      wp = defaultBirdWing
      count = length results
      dr = wsSpanM wp / fromIntegral stripCount
      fracs = [ (fromIntegral i + 0.5) / fromIntegral stripCount | i <- [0 .. count - 1] ]
      structures = map (shapeStructure wp) fracs
      forces = map (vz . resultForce) results
      sectionMoments = map resultSectionMoment results
      (targetBend, targetSlope) = integrateFlapBeam forces (map stBendEI structures) dr
      targetTwist = integrateTwist sectionMoments (map stTwistGJ structures) dr
      maxBend = 0.35 * wsSpanM wp
      maxTwist = radians 15
      update result structure bend slope twist =
        let old = resultState result
            nextBend = clamp (-maxBend) maxBend
                          (relaxDeflection dt (stTauBend structure) (stripBendM old) bend)
            nextSlope = relaxDeflection dt (stTauBend structure) (stripBendSlope old) slope
            nextTwist = clamp (-maxTwist) maxTwist
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
