{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

module Born2Flap.Math.Types where

newtype Seconds = Seconds { unSeconds :: Double }
  deriving stock (Eq, Ord, Show)

newtype Radians = Radians { unRadians :: Double }
  deriving stock (Eq, Ord, Show)

newtype MetresPerSecond = MetresPerSecond { unMetresPerSecond :: Double }
  deriving stock (Eq, Ord, Show)

newtype UnitInterval = UnitInterval { unUnitInterval :: Double }
  deriving stock (Eq, Ord, Show)

data Vec3 = Vec3
  { x :: !Double
  , y :: !Double
  , z :: !Double
  } deriving stock (Eq, Show)

data ElementGeometry = ElementGeometry
  { radiusM :: !Double
  , chordM :: !Double
  , sweep :: !Radians
  , twist :: !Radians
  } deriving stock (Eq, Show)

data ElementState = ElementState
  { angleOfAttack :: !Radians
  , reynoldsNumber :: !Double
  , crossflowVelocity :: !MetresPerSecond
  , separationFraction :: !UnitInterval
  } deriving stock (Eq, Show)

data WingLoads = WingLoads
  { forceN :: !Vec3
  , momentNm :: !Vec3
  , mechanicalPowerW :: !Double
  } deriving stock (Eq, Show)

clamp01 :: Double -> UnitInterval
clamp01 value = UnitInterval (max 0.0 (min 1.0 value))
