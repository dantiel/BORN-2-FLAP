-- Explicit dry mass budget. Collision geometry never enters these calculations.
module Born2Flap.Math.Airframe (airframeInertia, airframeMasses, fuselageDimensions, tailOutline) where
import Born2Flap.Math.Planform

-- Provisional load-bearing core, metres. Decorative feather volume is not
-- uniformly filled with the 38 g of battery, servos, electronics and structure.
fuselageDimensions :: (Double,Double,Double)
fuselageDimensions = (0.42,0.03,0.03)

airframeMasses :: Double -> (Double,Double,Double)
airframeMasses total = (max 0.0001 (total-2*wing-tailMass),wing,tailMass)
  where
    -- Preserve the legacy minimum-weight UI range without negative body mass.
    fraction = min 1 (total/0.0121)
    wing = 0.005*fraction
    tailMass = 0.002*fraction

-- Actual membrane tail polygon from Born2FlapRavenCrow.cpp, in body metres.
-- Its planform is ~39.5 cm long and ~50.8 cm wide; mass is only 2 g.
tailOutline :: [(Double,Double)]
tailOutline = [(mapX x,y*1.15425/100) | (x,y) <-
  [(-61.0013,5.1481),(-59.7605,-0.1170),(-61.0013,-5.9671),
   (-107.8416,-21.9963),(-110.0130,-17.5502),(-112.0293,0),
   (-109.7028,16.8482),(-108.4620,21.9963)]]
  where mapX x = (-38 + (x+61)/(-112.0293+61)*(-77.5+38))/100

-- Triangle quadrature exact for second moments of a uniform sheet.
tailSamples :: [(Double,Double,Double)]
tailSamples = concat [points a b c | (b,c) <- zip (drop 1 tailOutline) (drop 2 tailOutline)]
 where
  a = head tailOutline
  points (ax,ay) (bx,by) (cx,cy) =
   let area=abs ((bx-ax)*(cy-ay)-(by-ay)*(cx-ax))/2
   in [(area/3,wa*ax+wb*bx+wc*cx,wa*ay+wb*by+wc*cy) |
       (wa,wb,wc) <- [(2/3,1/6,1/6),(1/6,2/3,1/6),(1/6,1/6,2/3)]]

-- Dry locked-wing diagonal tensor about the requested total CG. The body
-- core centroid is solved from the component first moments, so subtracting
-- wing/tail mass does not also move or double-count the configured CG.
-- Wing roll second moment matches the hinge solver's planform integral.
-- The remaining off-diagonal and moving-CG terms are not modeled here.
airframeInertia :: Double -> Double -> (Double,Double,Double)
airframeInertia total cgX = (ix,iy,iz)
 where
  (body,wing,tailMass)=airframeMasses total
  wp=defaultBirdWing
  n=128 :: Int
  strips=[(shapeChord wp f, shapeLeadingEdgeX wp f, wsShoulderM wp+wsSpanM wp*f) |
          i <- [0..n-1], let f=(fromIntegral i+0.5)/fromIntegral n]
  area=sum [c | (c,_,_) <- strips]
  -- Distributed chord mass, with its analytic chordwise second moment.
  wings=[(wing*c/area, -lead-c/2, r, c*c/12) | (c,lead,r) <- strips]
  ta=sum [w | (w,_,_) <- tailSamples]
  ts=[(tailMass*w/ta,x,y) | (w,x,y) <- tailSamples]
  wingZ=0.04
  bodyX=(total*cgX-2*sum [m*x | (m,x,_,_) <- wings]-sum [m*x | (m,x,_) <- ts])/body
  bodyZ= -2*wing*wingZ/body
  (len,width,height)=fuselageDimensions
  bx=body*(width*width+height*height)/20 + body*bodyZ*bodyZ
  by=body*(len*len+height*height)/20 + body*((bodyX-cgX)^2+bodyZ*bodyZ)
  bz=body*(len*len+width*width)/20 + body*(bodyX-cgX)^2
  ix=bx+2*sum [m*(r*r+wingZ*wingZ) | (m,_,r,_) <- wings]+sum [m*y*y | (m,_,y) <- ts]
  iy=by+2*sum [m*((x-cgX)^2+variance+wingZ*wingZ) | (m,x,_,variance) <- wings]+sum [m*(x-cgX)^2 | (m,x,_) <- ts]
  iz=bz+2*sum [m*((x-cgX)^2+variance+r*r) | (m,x,r,variance) <- wings]+sum [m*((x-cgX)^2+y*y) | (m,x,y) <- ts]
