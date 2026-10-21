-- | Implicit parallel-hinge roll dynamics. Coordinates are body roll and
-- left/right flap deviation; positive deviation raises either wing.
module Born2Flap.Math.RollDynamics (stepRoll, rollMomentum, rollEnergy) where

import Born2Flap.Math.Types (Vec3(..))

dot :: Vec3 -> Vec3 -> Double
dot (Vec3 a b c) (Vec3 d e f) = a*d+b*e+c*f
cross :: Vec3 -> Vec3 -> Vec3
cross (Vec3 a b c) (Vec3 d e f) = Vec3 (b*f-c*e) (c*d-a*f) (a*e-b*d)
add, sub :: Vec3 -> Vec3 -> Vec3
add (Vec3 a b c) (Vec3 d e f) = Vec3 (a+d) (b+e) (c+f)
sub a b = add a (scale (-1) b)
scale :: Double -> Vec3 -> Vec3
scale s (Vec3 a b c) = Vec3 (s*a) (s*b) (s*c)
norm :: Vec3 -> Double
norm (Vec3 a b c) = maximum [abs a,abs b,abs c]

massTimes :: Double -> Double -> Double -> Vec3 -> Vec3
massTimes locked wing rotor (Vec3 p l r) =
  Vec3 (locked*p-wing*l+wing*r) ((wing+rotor)*l-wing*p) ((wing+rotor)*r+wing*p)

rollMomentum :: Double -> Double -> Vec3 -> Double
rollMomentum locked wing (Vec3 p l r) = locked*p-wing*l+wing*r

rollEnergy :: Double -> Double -> Double -> Vec3 -> Double
rollEnergy locked wing rotor v = 0.5 * dot v (massTimes locked wing rotor v)

-- | Backward Euler with bounded Newton iteration. Loads returns external
-- body moment and the two generalized hinge torques (aero + motor). Motors
-- must NOT also enter the body row. End-stop impulses act internally and the
-- body momentum row remains enforced when a joint hits its travel limit.
-- Failure returns Nothing, so callers can roll back instead of injecting an
-- unconverged impulse. Inertia includes added air once, in this mass matrix.
stepRoll :: Double -> Double -> Double -> Double -> Vec3 -> (Double,Double)
         -> (Vec3 -> Vec3) -> Maybe Vec3
stepRoll dt locked wing rotor old (angleL,angleR) loads
  | dt <= 0 || locked <= 2*wing*wing/(wing+rotor) || wing <= 0 || rotor <= 0 = Nothing
  | otherwise = newton (0::Int) old (residual old) Nothing
  where
    joint = wing+rotor
    limit = 80*pi/180
    bounds angle = ((-limit-angle)/dt,(limit-angle)/dt)
    -- Projected residual imposes unilateral joint stops. Projection uses a
    -- positive row scale and retains exactly the free-joint equation inside.
    constrained angle rate rowError =
      let (lo,hi)=bounds angle in rate - max lo (min hi (rate-rowError))
    residual v@(Vec3 _ l r) =
      let Vec3 a b c = sub (massTimes locked wing rotor (sub v old)) (scale dt (loads v))
      in Vec3 (a/locked) (constrained angleL l (b/joint)) (constrained angleR r (c/joint))
    newton n v f previousJacobian
      | norm f < 1e-7 = Just v
      | n >= 18 = Nothing
      | otherwise =
          let h=1e-4
              col offset = scale (0.5/h) (sub (residual (add v offset)) (residual (sub v offset)))
              Vec3 a d g=col (Vec3 h 0 0)
              Vec3 b e h'=col (Vec3 0 h 0)
              Vec3 c f' i=col (Vec3 0 0 h)
              fresh=(Vec3 a b c,Vec3 d e f',Vec3 g h' i)
              jacobian=case previousJacobian of
                Just rows | n `mod` 3 /= 0 -> rows
                _ -> fresh
              (r1,r2,r3)=jacobian
              det=dot r1 (cross r2 r3)
              Vec3 fx fy fz=f
              delta=scale (1/det) (add (scale fx (cross r2 r3))
                              (add (scale fy (cross r3 r1)) (scale fz (cross r1 r2))))
              candidates=[(candidate,residual candidate) | s <- [1,0.5,0.25,0.125,0.0625,0.03125], let candidate=sub v (scale s delta)]
          in if abs det < 1e-12 then Nothing else
               case filter ((< norm f) . norm . snd) candidates of
                 (next,nextResidual):_ -> newton (n+1) next nextResidual (Just jacobian)
                 [] -> case previousJacobian of
                   Just _ -> newton (n+1) v f Nothing
                   Nothing -> Nothing