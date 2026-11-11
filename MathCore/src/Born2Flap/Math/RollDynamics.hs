-- | Implicit parallel-hinge roll dynamics. Coordinates are body roll and
-- left/right flap deviation; positive deviation raises either wing.
-- The static weight-support mass matrix and its Schur reduction
-- (supportMassMatrix / supportedRollSystem / supportGravity) were REMOVED:
-- weight is carried by the vehicle mass budget in the spatial solve, never by
-- a flap-gated cushion, and gravity acts at the CG — no pendulum torques.
module Born2Flap.Math.RollDynamics (stepRoll, rollMomentum, rollEnergy, stepRollWithMass, solveImplicitSystem, solveImplicitSystemCached) where

import Born2Flap.Math.Types (Vec3(..))
import Data.List (maximumBy, transpose)
import Data.Ord (comparing)

-- | Small coupled nonlinear system, with pivoted elimination and a bounded
-- line search. Used for roll, both hinges, and all four elastic velocities.
-- It avoids solving elasticity again for every trial column of a nested solve.
solveImplicitSystem :: ([Double] -> [Double]) -> [Double] -> Maybe [Double]
solveImplicitSystem residual initial = fmap fst (solveImplicitSystemCached Nothing residual initial)

-- A previous tangent is only a convergence accelerator. Every accepted result
-- still satisfies the current complete residual; a stale tangent is rebuilt.
solveImplicitSystemCached :: Maybe [[Double]] -> ([Double] -> [Double]) -> [Double] -> Maybe ([Double],Maybe [[Double]])
solveImplicitSystemCached previous residual initial = go (0::Int) initial (residual initial) previous
 where
  size = length initial
  norm xs = maximum (0:map abs xs)
  finite v = not (isNaN v || isInfinite v)
  go n v f cached
    | not (all finite (v++f)) = Nothing
    | norm f < 1e-7 = Just (v,cached)
    | n >= 20 = Nothing
    | otherwise =
      let h=1e-4
          shifted i amount = residual [x+(if j==i then amount else 0) | (j,x)<-zip [0..] v]
          column i = zipWith (\a b -> (a-b)/(2*h)) (shifted i h) (shifted i (-h))
          fresh=transpose [column i | i<-[0..size-1]]
          jac=case cached of Just m -> m; _ -> fresh
      in case eliminate (zipWith (\row rhs -> row++[rhs]) jac f) of
           Nothing -> case cached of Just _ -> go (n+1) v f Nothing; _ -> Nothing
           Just delta ->
             let trials=[let next=zipWith (\x d -> x-g*d) v delta in (next,residual next)
                         | g<-[1,0.5,0.25,0.125,0.0625,0.03125]]
             in case filter (\(_,r) -> all finite r && norm r<norm f) trials of
                  (next,r):_ ->
                    -- Broyden's secant update reuses the force evaluations we
                    -- already made. Rebuilding seven central-difference columns
                    -- every third iteration dominated the frame time. The full
                    -- residual tolerance and fresh-tangent fallback stay intact.
                    let step=zipWith (-) next v
                        change=zipWith (-) r f
                        square=sum(map (\s -> s*s) step)
                        updated=if square<1e-20 then jac else
                          zipWith (\row y ->
                            let error=y-sum(zipWith (*) row step)
                            in zipWith (\a s -> a+error*s/square) row step) jac change
                        usable=if all (all finite) updated then Just updated else Nothing
                    in go (n+1) next r usable
                  [] -> case cached of Just _ -> go (n+1) v f Nothing; _ -> Nothing
  eliminate [] = Just []
  eliminate rows =
    let (index,pivot)=maximumBy (comparing (abs.head.snd)) (zip [0::Int ..] rows)
        a=head pivot
        rest=[row | (i,row)<-zip [0..] rows,i/=index]
        reduce row=tail(zipWith (\x p -> x-head row/a*p) row pivot)
    in if abs a<1e-12 || not(finite a) then Nothing else do
         xs <- eliminate(map reduce rest)
         let first=(last pivot-sum(zipWith (*) (tail(init pivot)) xs))/a
         if finite first then Just(first:xs) else Nothing

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
stepRoll dt locked wing rotor = stepRollWithMass
  (Vec3 locked (-wing) wing, Vec3 (-wing) (wing+rotor) 0, Vec3 wing 0 (wing+rotor)) dt

-- The same constrained implicit solve accepts a reduced multibody mass matrix.
stepRollWithMass :: (Vec3,Vec3,Vec3) -> Double -> Vec3 -> (Double,Double)
                 -> (Vec3 -> Vec3) -> Maybe Vec3
stepRollWithMass (row0,row1,row2) dt old (angleL,angleR) loads
  | dt <= 0 || locked <= 0 || locked*joint-y row0*y row0 <= 0
      || dot row0 (cross row1 row2) <= 0 = Nothing
  | otherwise = newton (0::Int) old (residual old) Nothing
  where
    locked = x row0
    joint = y row1
    rightJoint = z row2
    multiply v = Vec3 (dot row0 v) (dot row1 v) (dot row2 v)
    limit = 80*pi/180
    bounds angle = ((-limit-angle)/dt,(limit-angle)/dt)
    -- Projected residual imposes unilateral joint stops. Projection uses a
    -- positive row scale and retains exactly the free-joint equation inside.
    constrained angle rate rowError =
      let (lo,hi)=bounds angle in rate - max lo (min hi (rate-rowError))
    residual v@(Vec3 _ l r) =
      let Vec3 a b c = sub (multiply (sub v old)) (scale dt (loads v))
      in Vec3 (a/locked) (constrained angleL l (b/joint)) (constrained angleR r (c/rightJoint))
    newton n v f previousJacobian
      | norm f < 1e-7 = Just v
      | n >= 24 = Nothing
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