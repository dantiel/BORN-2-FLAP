-- Rigid fuselage + two articulated wings. Generalized velocity order is
-- [vCG.x,vCG.y,vCG.z,omega.x,omega.y,omega.z,leftRate,rightRate].
-- Added fluid carries inertia, not weight. Normal-only fluid inertia is
-- assembled at its chordwise distribution, including its pitching moment.
module Born2Flap.Math.SpatialDynamics (spatialSystem, multiply) where

import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Model (WingModel(..))
import Born2Flap.Math.Planform
import Born2Flap.Math.Servo (reflectedDriveInertia)

type Matrix = [[Double]]

add, subtractM :: Matrix -> Matrix -> Matrix
add = zipWith (zipWith (+))
subtractM = zipWith (zipWith (-))
scale :: Double -> Matrix -> Matrix
scale a = map (map (*a))
multiply :: Matrix -> [Double] -> [Double]
multiply m v = map (sum . zipWith (*) v) m
outer :: [Double] -> Matrix
outer v = [map (*a) v | a <- v]
zero :: Matrix
zero = replicate 8 (replicate 8 0)
diagonal :: [Double] -> Matrix
diagonal ds = [[if i==j then d else 0 | j<-[0..7::Int]] | (i,d)<-zip [0..] ds]
cross :: [Double] -> [Double] -> [Double]
cross [a,b,c] [d,e,f] = [b*f-c*e,c*d-a*f,a*e-b*d]
cross _ _ = error "spatial vector dimension"

-- Host locked tensor is the dry, flat-wing tensor about the configured CG.
-- Subtract the same flat wing quadrature before adding its moving geometry;
-- this keeps both the total dry mass and the configured flat-wing CG exact.
spatialSystem :: WingModel -> Double -> Double -> (Double,Double,Double)
              -> Double -> (Double,Double) -> [Double] -> Vec3
              -> (Matrix,[Double],[Double])
spatialSystem model mass cg (ix,iy,iz) density (left,right) velocity (Vec3 gx gy gz) =
  let wp=wmWing model
      n=16 :: Int
      stations=[(shapeLeadingEdgeX wp f,shapeChord wp f,wsShoulderM wp+wsSpanM wp*f)
               | i<-[0..n-1], let f=(fromIntegral i+0.5)/fromIntegral n]
      area=sum[c | (_,c,_)<-stations]
      dr=wsSpanM wp/fromIntegral n
      -- Uniform dry chord and flat-plate normal added mass have different
      -- second moments. Two-point quadrature reproduces each exactly.
      -- Each point Jacobian is affine in [1,radius,x]. Integrate those six
      -- moments once instead of allocating a full 8x8 matrix per mass point.
      covariance fluid = foldl' (zipWith (zipWith (+))) (replicate 3 (replicate 3 0))
        [scale weight (outer [1,r,-lead-c*(0.5+k/spread)-cg])
         | (lead,c,r)<-stations,k<-[-1,1],
           let weight=if fluid then density*pi*c*c*dr/8 else referenceWingMassKg*c/area/2,
           let spread=if fluid then sqrt 32 else sqrt 12]
      dryCov=covariance False; fluidCov=covariance True
      gram cov rows = [map (sum . zipWith (*) weighted) rows | row<-rows, let weighted=multiply cov row]
      derivative cov rows rates =
        let product a b=[map (sum . zipWith (*) weighted) b | row<-a,let weighted=multiply cov row]
        in add (product rates rows) (product rows rates)
      wing side angle =
        let s=sin angle;c=cos angle;h=0.04
            z=[0,0,0];one=[1,0,0]
            joint a = if side<0 then [a,z] else [z,a]
            jx=[one,z,z,z,[h,s,0],[0,-side*c,0]]++joint z
            jy=[z,one,z,[-h,-s,0],z,[0,0,1]]++joint [0,-side*s,0]
            jz=[z,z,one,[0,side*c,0],[0,0,-1],z]++joint [0,c,0]
            dx=[z,z,z,z,[0,c,0],[0,side*s,0]]++joint z
            dy=[z,z,z,[0,-c,0],z,z]++joint [0,-side*c,0]
            dz=[z,z,z,[0,-side*s,0],z,z]++joint [0,-s,0]
            normal=[z,[-side*s,0,0],[c,0,0],[side*h*s,side,0],[0,0,-c],[0,0,-side*s]]++joint [0,1,0]
            dn=[z,[-side*c,0,0],[-s,0,0],[side*h*c,0,0],[0,0,s],[0,0,-side*c]]++joint z
            dryWing=foldl' add zero (map (gram dryCov) [jx,jy,jz])
            total=add dryWing (gram fluidCov normal)
            slope=foldl' add (derivative fluidCov normal dn)
              (zipWith (derivative dryCov) [jx,jy,jz] [dx,dy,dz])
        in (dryWing,total,slope)
      (flat,_,_)=wing (-1) 0
      (flatR,_,_)=wing 1 0
      dryFlat=add flat flatR
      bodyOnly = [[if i<6 && j<6 then a else 0 | (j,a)<-zip [0::Int ..] row] | (i,row)<-zip [0::Int ..] dryFlat]
      base=subtractM (diagonal [mass,mass,mass,ix,iy,iz,reflectedDriveInertia,reflectedDriveInertia]) bodyOnly
      (dryL,totalL,dl)=wing (-1) left
      (dryR,totalR,drate)=wing 1 right
      dry=add base (add dryL dryR)
      m=add base (add totalL totalR)
      mdot=add (scale (velocity!!6) dl) (scale (velocity!!7) drate)
      momentum=multiply m velocity
      v=take 3 velocity; omega=take 3(drop 3 velocity)
      p=take 3 momentum; angular=take 3(drop 3 momentum)
      coadjoint=cross omega p ++ zipWith (+) (cross omega angular) (cross v p) ++ [0,0]
      -- Unknown body translation is an INERTIAL acceleration expressed in
      -- body axes, not the derivative of body-coordinate velocity. Remove
      -- its rotating-basis term from the Euler-Poincare inertial load.
      basis=multiply m (cross omega v ++ replicate 5 0)
      energyGradient=replicate 6 0 ++ [0.5*sum(zipWith (*) velocity (multiply d velocity)) | d<-[dl,drate]]
      bias=zipWith (-) (zipWith (+) (multiply mdot velocity) coadjoint) (zipWith (+) basis energyGradient)
      gravity=multiply dry ([gx,gy,gz]++replicate 5 0)
  in (m,bias,gravity)
