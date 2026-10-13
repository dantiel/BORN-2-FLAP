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
  , scaleWing
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
  , referenceWingMassKg
  , shapeFlapInertia
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
  { wsSpanM     :: !Double          -- ^ half-span (spanwise extent) [m]
  , wsShoulderM :: !Double          -- ^ shoulder distance: wing-root hinge offset from body centreline [m]
  , wsStations  :: ![ShapeStation]  -- ^ ordered root → tip
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

-- | Reference kestrel ornithopter: 550 mm half-span (1.10 m full span) — 222 mm
-- inner spar + 32 mm servo arm to the elbow, 310 mm outer LE spar to the tip,
-- overlapping. Broad armwing with a tapered, swept, washed-out handwing. Defined
-- purely as an outline; sweep is the slope of the leading edge (≈15° on the
-- armwing → ≈24° on the handwing). Geometrically similar to the 0.87 m micro
-- variant, scaled 1.27× for the bigger bird and bigger servo.
defaultBirdWing :: WingShape
defaultBirdWing = WingShape
  { wsSpanM = 0.55
  , wsShoulderM = 0.0127  -- 1.27 cm shoulder: parallel-flapping ornithopter flies best with almost no shoulder width
  , wsStations =
      [ ShapeStation 0.00 0.000 0.279  9.00 16.0 defaultBirdArmSection defaultBirdArmStructure  -- shoulder
      , ShapeStation 0.45 0.110 0.235  5.00 16.0 defaultBirdArmSection defaultBirdArmStructure  -- elbow
      , ShapeStation 1.00 0.333 0.114  3.00 16.0 defaultBirdHandSection defaultBirdHandStructure  -- tip
      ]
  }

-- | Uniformly scale a wing outline by @s@: span, shoulder offset, leading-edge
-- offsets and chords all scale together. Used to size the wing to the bird's
-- mass — a 25 g bird gets a much smaller, lightly-loaded wing than a 1 kg
-- bird, so a real micro-servo (≈0.2–0.6 N·m) can drive it. Span and chord
-- scale together, so aspect ratio (and the aerodynamics) is unchanged.
scaleWing :: Double -> WingShape -> WingShape
scaleWing s wing = wing
  { wsSpanM     = wsSpanM wing * s
  , wsShoulderM = wsShoulderM wing * s
  , wsStations  = map scaleStation (wsStations wing)
  }
  where
    scaleStation st = st
      { ssLeadX  = ssLeadX st * s
      , ssChordM = ssChordM st * s
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

-- | Reference structural mass of ONE wing of the 1.10 m kestrel [kg].
-- Ornithopter membrane wings are extremely light: ~5 g each for this bird
-- (tensioned membrane + carbon spars). Scales with @wingScale³@ under geometric
-- similarity.
referenceWingMassKg :: Double
referenceWingMassKg = 0.005

-- | Moment of inertia of ONE wing about the flap hinge (the shoulder axis)
-- [kg·m²]. Two physical contributions, both integrated over the planform:
--
--   * __structural__ — the wing's own mass, distributed proportional to local
--     chord (deeper wing = more membrane + longer ribs).
--   * __added mass__ — the entrained air a flapping membrane drags with it,
--     @ρ·π·c²/4@ per unit span (2-D flat plate accelerating normal to itself).
--     For a light membrane wing this DOMINATES the structural term (~3× for
--     the 5 g kestrel wing).
shapeFlapInertia :: Double -> Double -> WingShape -> Double
shapeFlapInertia wingMass airRho wp =
  let span = wsSpanM wp
      shoulder = wsShoulderM wp
      n = 128
      samples = [ (frac, shapeChord wp frac)
                | i <- [0 .. n - 1]
                , let frac = (fromIntegral i + 0.5) / fromIntegral n ]
      dfrac = 1 / fromIntegral n
      -- ∫ r²·chord dfrac and ∫ r²·chord² dfrac, r = shoulder + span·frac.
      intR2C  = sum [ (shoulder + span * f) ** 2 * c * dfrac | (f, c) <- samples ]
      intR2C2 = sum [ (shoulder + span * f) ** 2 * c * c * dfrac | (f, c) <- samples ]
      area = shapeArea wp
      structural = wingMass / max 1.0e-9 area * span * intR2C
      added = airRho * (pi / 4) * span * intR2C2
  in structural + added

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