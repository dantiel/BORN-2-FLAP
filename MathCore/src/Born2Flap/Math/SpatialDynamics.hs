-- Rigid fuselage + two articulated wings. Generalized velocity order is
-- [vCG.x,vCG.y,vCG.z,omega.x,omega.y,omega.z,leftRate,rightRate].
-- Added fluid carries inertia, not weight. Normal-only fluid inertia is
-- assembled at its chordwise distribution, including its pitching moment.
module Born2Flap.Math.SpatialDynamics (spatialSystem, tailFluidInertia, multiply) where

import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Model (WingModel(..))
import Born2Flap.Math.Planform
import Born2Flap.Math.Servo (reflectedDriveInertia)
import Born2Flap.Math.Airframe (tailRootX, tailOutline)
import Data.List (sort)

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
      -- The section polar already supplies the complete steady quarter-chord
      -- force/moment, including the Kutta circulation's cancellation of the
      -- potential-flow Munk couple. Adding v x p_fluid again gives a spurious
      -- pitch moment even at constant translation (and U*flap-rate recoil).
      -- Retain added acceleration, rotating-basis, joint and gyroscopic terms.
      fluidMomentum=multiply (subtractM m dry) velocity
      polarOverlap=replicate 3 0 ++ cross v (take 3 fluidMomentum) ++ [0,0]
      -- Unknown body translation is an INERTIAL acceleration expressed in
      -- body axes, not the derivative of body-coordinate velocity. Remove
      -- its rotating-basis term from the Euler-Poincare inertial load.
      basis=multiply m (cross omega v ++ replicate 5 0)
      energyGradient=replicate 6 0 ++ [0.5*sum(zipWith (*) velocity (multiply d velocity)) | d<-[dl,drate]]
      bias=zipWith (-) (zipWith (+) (multiply mdot velocity) coadjoint)
             (zipWith (+) polarOverlap (zipWith (+) basis energyGradient))
      gravity=multiply dry ([gx,gy,gz]++replicate 5 0)
  in (m,bias,gravity)

-- Rigid fan's normal added-air inertia, using its visible polygon and actual
-- incidence. As for the wings, fluid contributes kinetic energy, not weight.
-- Keep rotation/basis terms, but exclude the Munk couple already included
-- in the complete quarter-chord section polar (same convention as wings).
tailFluidInertia :: Double -> Double -> Double -> [Double] -> (Matrix,[Double])
tailFluidInertia cg density incidence velocity =
  let ci=cos incidence; si=sin incidence; z=[0,0,0]
      rows=[[-si,0,0],z,[ci,0,0],[0,0,ci],
            [tailRootX*(1-ci)+ci*cg,-1,0],[0,0,si],z,z]
      m=scale density [map (sum . zipWith (*) weighted) rows
          | row<-rows,let weighted=multiply tailFluidCovariance row]
      momentum=multiply m velocity
      v=take 3 velocity; omega=take 3(drop 3 velocity)
      p=take 3 momentum; angular=take 3(drop 3 momentum)
      coadjoint=cross omega p ++ zipWith (+) (cross omega angular) (cross v p) ++ [0,0]
      basis=multiply m (cross omega v ++ replicate 5 0)
      polarOverlap=replicate 3 0 ++ cross v p ++ [0,0]
  in (m,zipWith (-) coadjoint (zipWith (+) basis polarOverlap))

-- Two-point chord quadrature reproduces the thin plate added mass and its
-- c^2/32 pitching variance. Span integration follows the actual fan edges.
tailFluidCovariance :: Matrix
tailFluidCovariance =
  let ys=map snd tailOutline; lo=minimum ys; dy=(maximum ys-lo)/64
      edges=zip tailOutline (tail tailOutline++take 1 tailOutline)
      section y=sort [x0+(y-y0)*(x1-x0)/(y1-y0)
        | ((x0,y0),(x1,y1))<-edges, (y0<=y && y<y1)||(y1<=y && y<y0)]
      pairs (a:b:rest)=(a,b):pairs rest
      pairs _=[]
  in foldl' add (replicate 3 (replicate 3 0))
       [scale (pi*c*c*dy/8) (outer [1,(a+b)/2+k*c/sqrt 32,y])
       | i<-[0..63::Int],let y=lo+(fromIntegral i+0.5)*dy,
         (a,b)<-pairs(section y),let c=b-a,k<-[-1,1]]
