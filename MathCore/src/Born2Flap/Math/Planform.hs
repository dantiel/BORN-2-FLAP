{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | Wing planform geometry — the physical outline of one wing (root → tip).
--
-- A wing is defined as a *shape*: a polyline of spanwise stations, each giving
-- the leading-edge position, chord, geometric twist and dihedral. Every
-- aerodynamic quantity is DERIVED from that outline:
--
--   * chord      — linear interpolation of the station chords
--   * sweep      — the slope of the leading-edge curve (implicit; back-sweep +)
--   * twist      — linear interpolation of the station incidences
--   * dihedral   — linear interpolation of the station dihedrals
--   * area / mean chord / aspect ratio — integrated from @chord(y)@
--
-- Sweep is therefore never a parameter you set: you move the leading-edge
-- points and sweep follows. That is what lets a single module represent a bird
-- (broad two-panel armwing + swept tapered handwing), a butterfly (very low
-- aspect ratio, rounded outline), a dragonfly (long, straight, high aspect
-- ratio) or a pterosaur (swept, pointed) with no per-family special cases — the
-- editor only ever edits the outline.
--
-- The robust default is 'defaultBirdWing'; the strip solver consumes it through
-- the derived accessors, and the future strip × patch mesh is generated from
-- 'shapeStationsAt'.
module Born2Flap.Math.Planform
  ( WingShape(..)
  , ShapeStation(..)
  , SpanStation(..)
  , defaultBirdWing
    -- * Derived geometry (sweep implicit)
  , shapeLeadingEdgeX
  , shapeChord
  , shapeSweepRad
  , shapeTwistRad
  , shapeDihedralRad
  , shapeSection
  , shapeStructure
  , shapeArea
  , shapeMeanChord
  , shapeAspectRatio
  , shapeStationsAt
  ) where

import Data.List (sortOn)
import Born2Flap.Math.Section
  ( SectionProfile(..), defaultBirdArmSection, defaultBirdHandSection )
import Born2Flap.Math.Structure
  ( StructureProfile(..), defaultBirdArmStructure, defaultBirdHandStructure )

-- | One wing's outline: its spanwise extent plus an ordered list of shape
-- stations from root (fraction 0) to tip (fraction 1).
data WingShape = WingShape
  { wsSpanM    :: !Double          -- ^ half-span (spanwise extent) [m]
  , wsStations :: ![ShapeStation]  -- ^ ordered root → tip
  } deriving stock (Eq, Show)

-- | A control point of the outline at a given span fraction.
data ShapeStation = ShapeStation
  { ssFraction :: !Double  -- ^ span fraction 0..1 (should be increasing)
  , ssLeadX    :: !Double  -- ^ leading-edge x offset (chordwise/streamwise) [m]
  , ssChordM   :: !Double  -- ^ local chord [m]
  , ssTwistDeg :: !Double  -- ^ geometric incidence [deg]
  , ssDihedDeg :: !Double  -- ^ dihedral angle [deg]
  , ssSection  :: !SectionProfile  -- ^ camber / reflex / membrane material
  , ssStructure :: !StructureProfile  -- ^ bending / torsion / damping material
  } deriving stock (Eq, Show)

-- | A resampled spanwise station — the solver-facing derived geometry of one
-- strip. This is the bridge to the future strip × patch mesh (strip @i@ of @n@
-- sits at the strip midpoint).
data SpanStation = SpanStation
  { spanFraction :: !Double  -- ^ 0..1
  , spanLeadX    :: !Double  -- ^ leading-edge x offset [m]
  , spanChord    :: !Double  -- ^ chord [m]
  , spanSweep    :: !Double  -- ^ leading-edge sweep [rad]
  , spanTwist    :: !Double  -- ^ geometric incidence [rad]
  , spanDihed    :: !Double  -- ^ dihedral [rad]
  , spanSection  :: !SectionProfile  -- ^ camber / reflex / membrane material
  , spanStructure :: !StructureProfile  -- ^ bending / torsion / damping material
  } deriving stock (Eq, Show)

-- | Conservative, robust default: a ~1.44 m-span bird with a broad armwing and
-- a tapered, swept, washed-out handwing. Defined purely as an outline; sweep is
-- the slope of the leading edge (≈15° on the armwing → ≈24° on the handwing).
defaultBirdWing :: WingShape
defaultBirdWing = WingShape
  { wsSpanM = 0.72
  , wsStations =
      [ ShapeStation 0.00 0.000 0.220 14.00 0.0 defaultBirdArmSection defaultBirdArmStructure  -- shoulder
      , ShapeStation 0.45 0.087 0.185  9.95 0.0 defaultBirdArmSection defaultBirdArmStructure  -- elbow
      , ShapeStation 1.00 0.262 0.090  5.00 0.0 defaultBirdHandSection defaultBirdHandStructure  -- tip
      ]
  }

-- | Local leading-edge x offset [m] at a span fraction.
shapeLeadingEdgeX :: WingShape -> Double -> Double
shapeLeadingEdgeX = stationField ssLeadX

-- | Local chord [m] at a span fraction.
shapeChord :: WingShape -> Double -> Double
shapeChord = stationField ssChordM

-- | Local leading-edge sweep [rad], DERIVED as the slope of the leading-edge
-- curve (back-sweep positive). There is no sweep parameter: change the outline
-- and sweep follows automatically.
shapeSweepRad :: WingShape -> Double -> Double
shapeSweepRad wp frac =
  let sts = normalise (wsStations wp)
      c = clamp 0 1 frac
  in if length sts < 2
       then 0
       else let i = segmentIndex sts c
                a = sts !! i
                b = sts !! (i + 1)
                dx = ssLeadX b - ssLeadX a
                dy = wsSpanM wp * (ssFraction b - ssFraction a)
            in atan2 dx (max 1.0e-9 dy)

-- | Local geometric twist [rad] at a span fraction. Washout = tip incidence
-- lower than root incidence.
shapeTwistRad :: WingShape -> Double -> Double
shapeTwistRad wp frac = radians (stationField ssTwistDeg wp frac)

-- | Local dihedral [rad] at a span fraction (0 = flat wing). Not yet consumed
-- by the strip solver, which assumes wings flap in the vertical plane.
shapeDihedralRad :: WingShape -> Double -> Double
shapeDihedralRad wp frac = radians (stationField ssDihedDeg wp frac)

-- | Local section profile (camber / reflex / membrane) at a span fraction,
-- interpolated field-by-field like the other station scalars.
shapeSection :: WingShape -> Double -> SectionProfile
shapeSection wp frac = SectionProfile
  (sectionField spRestCamber wp frac)
  (sectionField spReflex wp frac)
  (sectionField spMembraneK wp frac)
  (sectionField spMembraneTau wp frac)
  where
    sectionField f w f' = stationField (\s -> f (ssSection s)) w f'

-- | Local structural profile (bending / torsion / damping) at a span fraction,
-- interpolated field-by-field like the other station scalars.
shapeStructure :: WingShape -> Double -> StructureProfile
shapeStructure wp frac = StructureProfile
  (structureField stBendEI wp frac)
  (structureField stTwistGJ wp frac)
  (structureField stTauBend wp frac)
  (structureField stTauTwist wp frac)
  where
    structureField f w f' = stationField (\s -> f (ssStructure s)) w f'

-- | Single-wing planform area [m²]: trapezoidal over the chord polyline.
shapeArea :: WingShape -> Double
shapeArea wp =
  let sts = normalise (wsStations wp)
  in if length sts < 2
       then 0
       else sum (zipWith trapezoid sts (tail sts))
  where
    trapezoid a b =
      let dy = wsSpanM wp * (ssFraction b - ssFraction a)
      in 0.5 * dy * (ssChordM a + ssChordM b)

-- | Mean chord = single-wing area / half-span [m].
shapeMeanChord :: WingShape -> Double
shapeMeanChord wp = shapeArea wp / max 1.0e-6 (wsSpanM wp)

-- | Full-vehicle aspect ratio @b²/S@ for the complete wing pair.
shapeAspectRatio :: WingShape -> Double
shapeAspectRatio wp =
  let halfSpan = wsSpanM wp
  in 2 * halfSpan * halfSpan / max 1.0e-6 (shapeArea wp)

-- | Resample the wing into @n@ spanwise stations (strip midpoints) carrying the
-- derived geometry. Strip @i@ of @n@; feeds the solver and the future mesh.
shapeStationsAt :: WingShape -> Int -> [SpanStation]
shapeStationsAt wp n
  | n <= 0 = []
  | otherwise =
      [ SpanStation frac (shapeLeadingEdgeX wp frac) (shapeChord wp frac)
          (shapeSweepRad wp frac) (shapeTwistRad wp frac) (shapeDihedralRad wp frac)
          (shapeSection wp frac) (shapeStructure wp frac)
      | i <- [0 .. n - 1]
      , let frac = (fromIntegral i + 0.5) / fromIntegral n
      ]

-- | Interpolate a scalar station field at a span fraction (linear in fraction).
stationField :: (ShapeStation -> Double) -> WingShape -> Double -> Double
stationField f wp frac =
  let sts = normalise (wsStations wp)
      c = clamp 0 1 frac
  in case sts of
       []  -> 0
       [s] -> f s
       _   -> let i = segmentIndex sts c
                  a = sts !! i
                  b = sts !! (i + 1)
                  t = clamp 0 1 ((c - ssFraction a) / max 1.0e-9 (ssFraction b - ssFraction a))
              in lerp (f a) (f b) t

-- | Index of the segment bracketing fraction @c@ (0..n-2).
segmentIndex :: [ShapeStation] -> Double -> Int
segmentIndex sts c = go 0
  where
    n = length sts
    go i
      | i >= n - 1 = n - 2
      | c <= ssFraction (sts !! (i + 1)) = i
      | otherwise = go (i + 1)

-- | Drop non-finite stations, sort by fraction, and collapse duplicate spans so
-- the outline is always a well-formed, strictly-increasing polyline.
normalise :: [ShapeStation] -> [ShapeStation]
normalise = dedupe . sortOn ssFraction . filter finiteStation
  where
    finiteStation s =
      all finite [ ssFraction s, ssLeadX s, ssChordM s, ssTwistDeg s, ssDihedDeg s
                 , spRestCamber (ssSection s), spReflex (ssSection s)
                 , spMembraneK (ssSection s), spMembraneTau (ssSection s)
                 , stBendEI (ssStructure s), stTwistGJ (ssStructure s)
                 , stTauBend (ssStructure s), stTauTwist (ssStructure s) ]
    dedupe [] = []
    dedupe (x : xs) = x : dedupe (dropWhile (\s -> ssFraction s <= ssFraction x) xs)

finite :: Double -> Bool
finite v = not (isNaN v || isInfinite v)

lerp :: Double -> Double -> Double -> Double
lerp from to amount = from + (to - from) * amount

clamp :: Ord a => a -> a -> a -> a
clamp low high = max low . min high

radians :: Double -> Double
radians degrees = degrees * pi / 180