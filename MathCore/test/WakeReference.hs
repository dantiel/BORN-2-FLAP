-- Diagnostic horseshoe-vortex calculation, independent of the production polar.
module WakeReference (wakeAt, liftingLineReference) where
import Born2Flap.Math.Types
import Born2Flap.Math.Vehicle
import Data.List (sortOn)
import Data.Ord (Down(..))

sub a b=addVec a (scaleVec (-1) b)
dot (Vec3 a b c) (Vec3 d e f)=a*d+b*e+c*f
unit v=scaleVec (1/max 1e-12(magnitude v)) v
segment a b p =
  let r1=sub p a;r2=sub p b;c=crossVec r1 r2
  in scaleVec (dot (sub b a) (sub(unit r1)(unit r2))/(4*pi*max 1e-20(dot c c))) c

wakeAt :: Double -> Double -> [StripResult] -> Vec3 -> Vec3
wakeAt side dr results p = foldr addVec zeroVec (map horseshoe results) where
  horseshoe result =
    let (position,_,span,_,flow,_)=resultFrame result
        direction=scaleVec (-1)(unit flow)
        dl=scaleVec (side*dr) span
        a=sub position(scaleVec 0.5 dl);b=addVec position(scaleVec 0.5 dl)
        far=scaleVec 100 direction
        reference=crossVec flow dl
        gamma= -dot (stripLift(resultState result)) reference/(airDensity*max 1e-12(dot reference reference))
    in scaleVec gamma (foldr addVec zeroVec [segment a b p,segment b (addVec b far) p,segment (addVec a far) a p])

solve []=[]
solve rows =
  let p:rs=sortOn (Down . abs . head) rows
      pn=map (/head p) p
      rest=[tail(zipWith (\x y -> x-head r*y) r pn)|r<-rs]
      xs=solve rest
  in (last pn-sum(zipWith (*) (tail(init pn)) xs)):xs

-- Independent one-chord Weissinger lattice. Joined span from left tip to
-- right tip avoids fictitious isolated wing-root trailing vortices. Normal
-- flow is enforced at 3/4 chord; bound circulation acts at 1/4 chord.
-- Inputs: actual section geometry, chord and zero-lift angle; no production
-- lift slope, efficiency, force projection or circulation law is called.
liftingLineReference :: Vec3 -> [(StripResult,Double,Double)] -> ([Vec3],Vec3 -> Vec3)
liftingLineReference freestream sections = (forces,wake) where
  pos r=let (p,_,_,_,_,_)=resultFrame r in p
  positions=[pos r|(r,_,_)<-sections]
  middle=zipWith (\a b ->scaleVec 0.5(addVec a b)) positions (drop 1 positions)
  ends=[sub (scaleVec 1.5(head positions)) (scaleVec 0.5(positions!!1))]++middle++
       [sub (scaleVec 1.5(last positions)) (scaleVec 0.5(positions!!(length positions-2)))]
  edges=zip ends (tail ends)
  far=scaleVec (-100)(unit freestream)
  vortex (a,b) p=foldr addVec zeroVec [segment a b p,segment b (addVec b far) p,segment (addVec a far) a p]
  equation (r,c,alpha0)=
    let (p,chord,_,normal,flow,_)=resultFrame r
        cp=sub p (scaleVec (0.5*c) chord)
        rhs=dot normal flow+magnitude flow*alpha0
    in [dot normal(vortex edge cp)|edge<-edges]++[rhs]
  gammas=solve(map equation sections)
  forces=[scaleVec (-airDensity*g)(crossVec flow(sub b a)) | (g,((a,b),(r,_,_)))<-zip gammas(zip edges sections),
          let (_,_,_,_,flow,_)=resultFrame r]
  wake p=foldr addVec zeroVec [scaleVec g(vortex edge p)|(g,edge)<-zip gammas edges]
