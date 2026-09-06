{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

module Born2Flap.Math.Vehicle
  ( VehicleInput(..)
  , VehicleOutput(..)
  , VehicleState
  , vehiclePhase
  , defaultVehicle
  , stepVehicle
  ) where

import Born2Flap.Math.Types
import Born2Flap.Math.Simulation
import Born2Flap.Math.Wing (crossflowBaseline, relaxSeparation)

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
  } deriving stock (Eq, Show)

data VehicleState = VehicleState
  { vehiclePhase :: !Double
  , leftStrips :: ![StripState]
  , rightStrips :: ![StripState]
  } deriving stock (Eq, Show)

data StripResult = StripResult
  { resultState :: !StripState
  , resultForce :: !Vec3
  , resultMoment :: !Vec3
  , resultPower :: !Double
  } deriving stock (Eq, Show)

stripCount :: Int
stripCount = 16

emptyStrip :: StripState
emptyStrip = StripState 0 0 0 0

defaultVehicle :: VehicleState
defaultVehicle = VehicleState 0 (replicate stripCount emptyStrip) (replicate stripCount emptyStrip)

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
          phase = wrapAngle (vehiclePhase state + dt * 2 * pi * (1.2 + 3.8 * throttle))
          rates = bodyRatesRadS input
          -- Rate damping is deliberately inside the deterministic math core.
          rollMix = clamp (-1) 1 (rollCommand input - 0.22 * vx rates)
          pitchMix = clamp (-1) 1 (pitchCommand input - 0.18 * vy rates)
          yawMix = clamp (-1) 1 (yawCommand input - 0.16 * vz rates)
          (leftResults, leftNext) = stepWing (-1) phase throttle rollMix input (leftStrips state)
          (rightResults, rightNext) = stepWing 1 phase throttle rollMix input (rightStrips state)
          wingResults = leftResults ++ rightResults
          wingForce = foldr (addVec . resultForce) zeroVec wingResults
          wingMoment = foldr (addVec . resultMoment) zeroVec wingResults
          speed = magnitude (bodyVelocityMS input)
          bodyDrag = scaleVec (-0.5 * 1.225 * 0.032 * speed) (bodyVelocityMS input)
          tailArm = Vec3 (-0.48) 0 0
          tailVelocity = addVec (bodyVelocityMS input) (crossVec rates tailArm)
          tailQArea = 0.5 * 1.225 * 0.035 * magnitude tailVelocity * magnitude tailVelocity
          tailForce = scaleVec tailQArea (Vec3 0 (-yawMix) (-pitchMix))
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
      in (output, VehicleState phase leftNext rightNext)
  where
    dt = stepSeconds input

stepWing :: Double -> Double -> Double -> Double -> VehicleInput -> [StripState]
         -> ([StripResult], [StripState])
stepWing side phase throttle rollMix input states =
  let frequency = 1.2 + 3.8 * throttle
      amplitude = radians (12 + 38 * throttle) * clamp 0.55 1.35 (1 - side * 0.22 * rollMix)
      omega = 2 * pi * frequency
      stroke = amplitude * sin phase
      strokeRate = amplitude * omega * cos phase
      raw = zipWith (stepStrip side stroke strokeRate input) [0 ..] states
      transported = transportSeparation side input raw
  in (transported, map resultState transported)

stepStrip :: Double -> Double -> Double -> VehicleInput -> Int -> StripState -> StripResult
stepStrip side stroke strokeRate input index old =
  let dt = stepSeconds input
      fraction = (fromIntegral index + 0.5) / fromIntegral stripCount
      spanM = 0.72
      dr = spanM / fromIntegral stripCount
      radius = dr * (fromIntegral index + 0.5)
      chord = lerp 0.20 0.10 fraction
      sweep = radians (lerp 24 (-8) fraction)
      twist = radians (lerp 19 7 fraction)
      velocity = bodyVelocityMS input
      rates = bodyRatesRadS input
      -- Body axes: +x forward, +y right, +z up. Both wings share vertical stroke motion.
      rotationalZ = vx rates * side * radius
      flapVelocityZ = radius * cos stroke * strokeRate
      chordVelocity = max 0.05 (vx velocity)
      normalVelocity = negate (vz velocity + rotationalZ + flapVelocityZ)
      planarSpeed = max 0.05 (sqrt (chordVelocity * chordVelocity + normalVelocity * normalVelocity))
      alpha = twist + atan2 (-normalVelocity) chordVelocity
      alphaRate = (alpha - stripPreviousAlpha old) / dt
      reynolds = planarSpeed * chord / 1.48e-5
      stallAngle = radians (clamp 10 18 (16 - 1.4 * logBase 10 (max 1 (150000 / reynolds))))
      dynamicDelay = clamp (-0.16) 0.16 (0.018 * alphaRate)
      target = smoothStep (stallAngle - radians 3) (stallAngle + radians 4)
                 (abs alpha - dynamicDelay)
      separating = target > stripSeparation old
      tau = if separating then 0.045 else 0.090
      UnitInterval relaxed = relaxSeparation (Seconds dt) (Seconds tau)
                               (clamp01 (stripSeparation old)) (clamp01 target)
      levTarget = if abs alpha > stallAngle && alphaRate * alpha > 0 then 1 else 0
      levRelax = 1 - exp (-dt / if levTarget > stripLevStrength old then 0.025 else 0.080)
      lev = clamp 0 1 (stripLevStrength old + levRelax * (levTarget - stripLevStrength old))
      crossSpeed = unMetresPerSecond (crossflowBaseline 0.32 (Radians sweep)
                       (MetresPerSecond planarSpeed))
      attachedCl = clamp (-1.9) 1.9 (2 * pi * alpha)
      separatedCl = 1.05 * sin (2 * alpha)
      rotationalCl = clamp (-0.65) 0.65 (0.5 * chord * alphaRate / planarSpeed)
      cl = lerp attachedCl separatedCl relaxed + rotationalCl + signum alpha * 0.45 * lev
      aspectRatio = spanM * spanM / (spanM * 0.15)
      inducedCd = cl * cl / (pi * 0.82 * aspectRatio)
      attachedCd = 0.025 + inducedCd
      separatedCd = 0.20 + 1.20 * sin alpha * sin alpha
      cd = lerp attachedCd separatedCd relaxed
      sideCd = 0.018 + 0.08 * relaxed
      area = chord * dr
      q = 0.5 * 1.225 * planarSpeed * planarSpeed
      lift = q * area * cl
      drag = q * area * cd
      flowX = chordVelocity / planarSpeed
      flowZ = normalVelocity / planarSpeed
      quasiForce = Vec3 (-drag * flowX - lift * flowZ)
                        (-side * q * area * sideCd * signum crossSpeed)
                        (-drag * flowZ + lift * flowX)
      normalAcceleration = (normalVelocity - stripPreviousNormalVelocity old) / dt
      addedMass = clamp (-25) 25 (-1.225 * pi * chord * chord * dr * normalAcceleration / 4)
      force = addVec quasiForce (Vec3 0 0 addedMass)
      position = Vec3 0 (side * radius * cos stroke) (radius * sin stroke)
      moment = crossVec position force
      power = abs (vz force * flapVelocityZ)
      next = StripState relaxed alpha normalVelocity lev
  in StripResult next force moment power

transportSeparation :: Double -> VehicleInput -> [StripResult] -> [StripResult]
transportSeparation _side input results = zipWith update [0 ..] results
  where
    dt = stepSeconds input
    dr = 0.72 / fromIntegral stripCount
    count = length results
    separationAt i = stripSeparation . resultState $ results !! clampInt 0 (count - 1) i
    update i result =
      let fraction = (fromIntegral i + 0.5) / fromIntegral stripCount
          sweep = radians (lerp 24 (-8) fraction)
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

wrapAngle :: Double -> Double
wrapAngle angle = atan2 (sin angle) (cos angle)

firstIndex :: (a -> Bool) -> [a] -> Int
firstIndex predicate = go 0
  where
    go _ [] = -1
    go index (value : rest)
      | predicate value = index
      | otherwise = go (index + 1) rest
