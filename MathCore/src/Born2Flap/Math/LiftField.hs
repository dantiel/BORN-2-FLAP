-- | A convected, relaxing pressure patch at each aerodynamic strip.
-- Coordinates are body-frame components of air-frame vectors, not body-fixed
-- anchors. Pressure memory stores elastic energy; relaxation only removes it.
module Born2Flap.Math.LiftField
  ( PressureField(..), emptyPressureField, advancePressureField, transportVector
  , dotVector, unitVector
  ) where

import Born2Flap.Math.Types (Vec3(..))

data PressureField = PressureField
  { fieldDisplacement :: !Vec3 -- surface displacement relative to the air patch, m
  , fieldDirection :: !Vec3    -- last surface normal in current body coordinates
  , fieldLoad :: !Double      -- mean aerodynamic loading, N; never aircraft weight
  } deriving (Eq, Show)

emptyPressureField :: PressureField
emptyPressureField = PressureField (Vec3 0 0 0) (Vec3 0 0 0) 0

dotVector :: Vec3 -> Vec3 -> Double
dotVector (Vec3 a b c) (Vec3 d e f) = a*d+b*e+c*f

add :: Vec3 -> Vec3 -> Vec3
add (Vec3 a b c) (Vec3 d e f) = Vec3 (a+d) (b+e) (c+f)
scale :: Double -> Vec3 -> Vec3
scale s (Vec3 a b c) = Vec3 (s*a) (s*b) (s*c)
cross :: Vec3 -> Vec3 -> Vec3
cross (Vec3 a b c) (Vec3 d e f) = Vec3 (b*f-c*e) (c*d-a*f) (a*e-b*d)

unitVector :: Vec3 -> Vec3
unitVector v = scale (1 / max 1e-12 (sqrt (dotVector v v))) v

-- | Exact rotation into the next body basis. An air-frame vector must turn
-- OPPOSITE the body; Euler transport would grow its norm and stored energy.
transportVector :: Double -> Vec3 -> Vec3 -> Vec3
transportVector dt omega v
  | speed < 1e-12 = v
  | otherwise = add (scale (cos angle) v)
      (add (scale (-sin angle) (cross axis v))
           (scale ((1-cos angle)*dotVector axis v) axis))
 where
  speed = sqrt (dotVector omega omega)
  axis = scale (1/speed) omega
  angle = dt*speed

-- | Backward Euler Maxwell element. The patch convects with the local wind:
-- its normal offset integrates the supplied relative surface/air velocity.
-- The paired wake supplies the difference of two complete local velocities,
-- including translation, body/hinge rotation, bend and twist. Tangential
-- advection and change of surface orientation shed the old normal strain.
-- F.v*dt + (E_next-E_old) <= 0 for k>=0, tau>0, E=k*|offset|^2/2.
-- The caller transports the returned state once when the body advances.
advancePressureField :: Double -> Double -> Double -> Vec3 -> Vec3
                     -> PressureField -> (Vec3, PressureField)
advancePressureField dt tau stiffness normal velocity old =
  let n = unitVector normal
      displacement = (dotVector (fieldDisplacement old) n
                       + dt*dotVector velocity n) / (1+dt/max 1e-9 tau)
      offset = scale displacement n
  in (scale (-max 0 stiffness) offset, PressureField offset n (fieldLoad old))
