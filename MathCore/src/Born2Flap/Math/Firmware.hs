{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | RC mixer derived from PteronautOS @Ornithopter::_computeServoMixer@.
-- The simulator extends glide mixing and opposite aileron stroke timing;
-- it is not a bit-identical firmware reference. CRSF channels still command
-- physical servos, never body forces or attitude targets.
--
-- The oscillator/waveform (@Born2Flap.Math.Waveform@) is *only* the servo
-- motion generator here — it is not a physical dimension. The physical
-- dimensions (servo struggle against aerodynamic load, wing aerodynamics)
-- live in @Born2Flap.Math.Servo@ and @Born2Flap.Math.Wing@.
module Born2Flap.Math.Firmware
  ( RcChannels(..)
  , defaultRcChannels
  , FirmwareParams(..)
  , defaultFirmwareParams
  , FlightProfile(..)
  , defaultFlightProfiles
  , FirmwareState(..)
  , defaultFirmwareState
  , MixerOutput(..)
  , PilotInput(..)
  , defaultPilotInput
  , pilot4
  , pilotToRc
  , crsfToNorm
  , crsfToFloat
  , normToRaw
    , computeServoMixer
    , strokeAmpMaxDeg
    , glideAileronMaxDeg
    , glideRudderMaxDeg
    ) where

import Born2Flap.Math.Waveform
  ( OscillatorState(..), defaultOscillator, advanceOscillator
  , limiarFromFerocities, shapeWave )

-- ── CRSF channel set ───────────────────────────────────────────────
--
-- Raw CRSF values span 172 (988 µs) … 1811 (2012 µs), neutral 992 (1500 µs).

data RcChannels = RcChannels
  { rcAileron  :: !Double   -- ^ roll stick
  , rcElevator :: !Double   -- ^ pitch stick
  , rcThrottle :: !Double   -- ^ flap throttle (glide below threshold)
  , rcRudder   :: !Double   -- ^ yaw stick
  , rcArm      :: !Double   -- ^ > 992 = armed
  , rcFreq     :: !Double   -- ^ CH6 flap-frequency modulator
  , rcProfile  :: !Double   -- ^ flight-profile selector (multi-position)
  } deriving stock (Eq, Show)

defaultRcChannels :: RcChannels
defaultRcChannels = RcChannels
  { rcAileron = 992, rcElevator = 992, rcThrottle = 172, rcRudder = 992
  , rcArm = 1811, rcFreq = 1500, rcProfile = 992
  }

crsfRawMin, crsfRawMax :: Double
crsfRawMin = 172
crsfRawMax = 1811

crsfToFloat :: Double -> Double -> Double -> Double
crsfToFloat raw outMin outMax =
  let t = (raw - crsfRawMin) / (crsfRawMax - crsfRawMin)
  in outMin + t * (outMax - outMin)

crsfToNorm :: Double -> Double
crsfToNorm raw = clamp (-1) 1 ((raw - 992) / if raw < 992 then 820 else 819)

normToRaw :: Double -> Double
normToRaw norm = 992 + clamp (-1) 1 norm * if norm < 0 then 820 else 819

-- ── Logical pilot input (normalised sticks) ────────────────────────
--
-- The public boundary uses logical channels, not raw CRSF. These map onto
-- the firmware's CRSF channel space faithfully: the throttle stick spans
-- the full CRSF throttle range (so the firmware's own flap threshold at
-- ~8% stick travel is preserved), and the -1..1 sticks map onto the
-- neutral-992 CRSF span via @normToRaw@.

data PilotInput = PilotInput
  { piThrottle :: !Double   -- ^ 0..1 flap throttle (glide below firmware threshold)
  , piRoll     :: !Double   -- ^ -1..1 aileron (roll)
  , piPitch    :: !Double   -- ^ -1..1 elevator (pitch)
  , piYaw      :: !Double   -- ^ -1..1 rudder (yaw)
  , piSpeedMod :: !Double   -- ^ 0..1 flapping speed modifier (base frequency)
  , piCoupled  :: !Bool     -- ^ throttle-coupled (True) vs independent (False)
  } deriving stock (Eq, Show)

defaultPilotInput :: PilotInput
defaultPilotInput = PilotInput 0 0 0 0 0.5 True

-- | Legacy four-channel constructor: neutral speed modifier + coupled throttle.
pilot4 :: Double -> Double -> Double -> Double -> PilotInput
pilot4 t r p y = PilotInput t r p y 0.5 True

pilotToRc :: PilotInput -> RcChannels
pilotToRc pilot = RcChannels
  { rcAileron  = normToRaw (clamp (-1) 1 (piRoll pilot))
  , rcElevator = normToRaw (clamp (-1) 1 (piPitch pilot))
  , rcThrottle = crsfRawMin + clamp 0 1 (piThrottle pilot) * (crsfRawMax - crsfRawMin)
  , rcRudder   = normToRaw (clamp (-1) 1 (piYaw pilot))
  , rcArm      = 1811
  , rcFreq     = normToRaw (clamp (-1) 1 (piSpeedMod pilot * 2 - 1))
  , rcProfile  = 992
  }

-- ── Constants (OrnithopterConfig.h) ────────────────────────────────

flapThresholdUs, flapHysteresisUs :: Double
flapThresholdUs = 303
flapHysteresisUs = 50

steerMaxDeg, angularMultiplier, neutralAngleDeg :: Double
steerMaxDeg = 60
angularMultiplier = 2
neutralAngleDeg = 100

rollAuthorityGain :: Double
rollAuthorityGain = 2.0

-- | Differential-dihedral roll authority (flapping branch only). This is kept
-- SMALL on purpose: a static flap-centre tilt lifts one wing's lift inward and
-- the other's outward, so the LEFT/RIGHT LATERAL FORCES ADD (a sideslip), not
-- cancel into a roll couple. The roll couple that remains is only the weak
-- cos(dihedral) vertical lift difference. A large gain here makes the roll
-- stick mostly sideslip and yaw the bird instead of banking it (measured:
-- full aileron gave Fy=-3 N, Mz=+0.7 N·m against a roll Mx of only +0.09 N·m).
-- (Roll authority is physical: a differential stroke bounded by the servo's
--  own travel — see `rollBudgetDeg` in flappingBranch below. No arbitrary gain.)

-- | Static glide control limits. The glide branch holds its stroke POSITION:
-- no pump, no flap cadence (mixFlapHz = 0). The held differential geometry
-- reaches the body through three coupled channels, all independent of the
-- flap rate once the servo has arrived:
--   1. the weight-support reaction (the wing is the structural surface the
--      fuselage lies on; each strip carries the weight component normal to it),
--   2. the low-speed normal-pressure floor (a slowly drifting or rolling wing
--      still grips the air instead of slipping through it),
--   3. the implicit body-roll coupling in the Newton solve (roll rate and the
--      two flap rates are one coupled system).
-- | Full-stick glide differential authority at AileronScale=100. The game's
-- default AileronScale=40 then commands ±70° per wing: the glued-wing law
-- the user demands — a slow full-stick sweep leaves BOTH wings horizontal
-- in the world while the fuselage counter-rotates 70° (exactly half the
-- 140° differential, the geometric fixed point of the glued system).
glideAileronMaxDeg :: Double
glideAileronMaxDeg = 175

glideRudderMaxDeg :: Double
glideRudderMaxDeg = 8

-- | Amplitude-differential roll authority. The downstroke (power stroke) is
-- stronger on the side that must rise, so each downstroke pushes the bird's
-- roll momentum into the turn — the downgoing wing produces more lift (and,
-- below stall, less separation) while the upgoing wing stalls slightly more,
-- and the resulting induced-drag asymmetry yaws the nose INTO the turn
-- (proverse yaw). This is the PRIMARY roll mechanism: differential lift at the
-- spanwise arm rolls the fuselage, and it does not sideslip like a static
-- dihedral tilt. Scales with @profAileronScale@.
-- (No arbitrary amplitude-differential gain: the aileron differential spends
--  the servo's physical travel budget, not a hand-tuned multiplier.)

-- Full-throttle stroke amplitude is NOT a tuning knob: it is the largest
-- amplitude the selected servo can still trace at the base frequency ceiling
-- (margin * servoDegS / (2*pi*freqCeiling)), bounded only by the mechanical
-- linkage. A slower ceiling therefore yields a deeper stroke, and vice versa —
-- amplitude and cadence trade against each other at the servo limit.
mechanicalMaxStrokeDeg, freqMinHz, servoSpeedMsDefault, servoSpeedMargin :: Double
mechanicalMaxStrokeDeg = 70
freqMinHz = 0.5
servoSpeedMsDefault = 40
servoSpeedMargin = 0.85

ferocityMin, ferocityMax, differentialMax :: Double
ferocityMin = 0
ferocityMax = 8
differentialMax = 4

skewMin, skewMax :: Double
skewMin = -100
skewMax = 100

skewRateGain, skewRateLpfTau, rollRateGain :: Double
skewRateGain = 10
skewRateLpfTau = 0.10
rollRateGain = 0.1

anchorGainDefault, cadenceGainDefault :: Double
anchorGainDefault = 0
cadenceGainDefault = 20

-- ── Flight profile ─────────────────────────────────────────────────
--
-- Mirrors @FlightProfileParams@. The 18 fields in declaration order; the
-- firmware aggregate-initialises the first 12 and zero-fills the rest.

data FlightProfile = FlightProfile
  { profStrokeFerocity  :: !Double  -- 0..100
  , profReturnFerocity  :: !Double  -- 0..100
  , profGlideAngleDeg   :: !Double  -- -15..+15
  , profTailElevatorAngleDeg :: !Double  -- -15..+15, physical tail mounting incidence
  , profAileronScale    :: !Double  -- 0..100
  , profElevatorScale   :: !Double  -- 0..100
  , profRudderFerocityRange :: !Double  -- 0..100
  , profRudderAmplitudeDiff :: !Double  -- 0..100
  , profElevatorFerocityMix :: !Double  -- 0..100
  , profThrottleFrequencyMix :: !Double  -- 0..100
  , profFerocityShapeMix    :: !Double  -- 0..100
  , profStrokeSkew          :: !Double  -- -100..+100
  , profReturnSkew          :: !Double  -- -100..+100
  , profThrottleThrustShapeMix :: !Double  -- 0..100
  , profThrottleThrustExpo    :: !Double  -- -100..+100
  , profAileronSkewMix        :: !Double  -- 0..100
  , profThrottleSkewRateMix   :: !Double  -- 0..100
  , profAileronSkewRateMix    :: !Double  -- 0..100
  } deriving stock (Eq, Show)

-- | The three firmware tuning sets (flightProfiles[0..2]). Profile 1 is the
-- default active profile.
defaultFlightProfiles :: [FlightProfile]
defaultFlightProfiles =
  [ FlightProfile 30 50 (-4) (-2.3) 40 60 50 60 0 0 0 0 0 0 0 0 0 0
  , FlightProfile 62 42 (-4) (-2.3) 40 60 50 60 0 0 0 0 0 0 0 0 0 0
  , FlightProfile 70 50   2  (-2.3) 40 60 50 60 0 0 0 0 0 0 0 0 0 0
  ]

-- ── Runtime mixer params ───────────────────────────────────────────
--
-- Hardware/kernel knobs that live outside the flight profiles (servo speed,
-- battery-affected servo rate, flap frequency window ceiling, servo µs range).

data FirmwareParams = FirmwareParams
  { fwProfile       :: !FlightProfile
  , fwServoSpeedMs  :: !Double  -- ^ ms per 60° stroke (25..250); servo selection
  , fwFlapBaseFreqDh:: !Double  -- ^ deci-Hz (10..200), CH6 frequency ceiling
  , fwFlapCentreDeg :: !Double  -- ^ flap stroke centre trim (dihedral) [deg], separate from tail incidence
  , fwVerticalMountIncidenceDeg :: !Double  -- ^ real wing mount incidence (nose-up +) [deg]
  , fwRudderYawWeight :: !Double  -- 0..100
  , fwRudderRollWeight:: !Double  -- 0..100
  , fwAnchorGain      :: !Double  -- 0..100 beat-locking stiffness
  , fwCadenceGain     :: !Double  -- 0..100 (used only in Zephyrus path)
  } deriving stock (Eq, Show)

defaultFirmwareParams :: FirmwareParams
defaultFirmwareParams = FirmwareParams
  { fwProfile = defaultFlightProfiles !! 1
  , fwServoSpeedMs = servoSpeedMsDefault
  , fwFlapBaseFreqDh = 50
  , fwFlapCentreDeg = 0
  , fwVerticalMountIncidenceDeg = 0
  , fwRudderYawWeight = 65
  -- Roll moves the wings against the air; it must not secretly steer the tail.
  , fwRudderRollWeight = 0
  , fwAnchorGain = anchorGainDefault
  , fwCadenceGain = cadenceGainDefault
  }

-- Servo no-load slew rate [deg/s] from the ms-per-60° selection.
servoDegPerSec :: FirmwareParams -> Double
servoDegPerSec params = 60000 / max 1 (fwServoSpeedMs params)

-- Full-throttle stroke amplitude [deg]: the largest amplitude the selected
-- servo can still trace at the base frequency ceiling (before the speed
-- modifier is applied). Derived from servo speed, never a tuning field.
strokeAmpMaxDeg :: FirmwareParams -> Double
strokeAmpMaxDeg params =
  let baseFreqHz = fwFlapBaseFreqDh params * 0.1
  in min mechanicalMaxStrokeDeg
       (servoSpeedMargin * servoDegPerSec params / max 1e-6 (2*pi * baseFreqHz))

-- ── Firmware state (persistent across mixer ticks) ─────────────────

data FirmwareState = FirmwareState
  { fwWasFlapping      :: !Bool
  , fwPrevThrottlePct  :: !Double   -- sentinel < 0 = seed without slew kick
  , fwThrottleRateLPF  :: !Double
  , fwPrevAileronNorm  :: !Double   -- sentinel < -1.5 = seed without roll kick
  , fwAileronRateLPF   :: !Double
  , fwKGainMod         :: !Double   -- ONDAS phase-advance demand (1.0 = nominal)
  , fwOscillator       :: !OscillatorState
  } deriving stock (Eq, Show)

defaultFirmwareState :: FirmwareState
defaultFirmwareState = FirmwareState
  { fwWasFlapping = False
  , fwPrevThrottlePct = -1
  , fwThrottleRateLPF = 0
  , fwPrevAileronNorm = -2
  , fwAileronRateLPF = 0
  , fwKGainMod = 1.0
  , fwOscillator = defaultOscillator
  }

-- ── Mixer output ───────────────────────────────────────────────────
--
-- Wing shaft commands in degrees (neutral = 100°), before hardware PWM clamps.
-- Physical flap travel is bounded separately in the hinge actuator. The µs
-- step and integer truncation are PWM artefacts with no physical meaning for
-- a continuous game simulation, so they are deliberately omitted.

data MixerOutput = MixerOutput
  { mixIsFlapping      :: !Bool
    , mixFlapHz          :: !Double   -- glide: 0 (static stroke, no cadence)
  , mixThrottlePct     :: !Double   -- 0 in glide
  , mixLeftWingDeg     :: !Double   -- left wing servo command (deg)
  , mixRightWingDeg    :: !Double   -- right wing servo command (deg)
  , mixLeftFlapDevDeg  :: !Double   -- left flap deviation from neutral (deg)
  , mixRightFlapDevDeg :: !Double   -- right flap deviation from neutral (deg)
  , mixRudderUs        :: !Double   -- crest rudder command (µs)
  , mixStrokeFerL      :: !Double   -- 0..8 post-mix downstroke ferocity (L)
  , mixReturnFerL      :: !Double   -- 0..8 post-mix upstroke ferocity (L)
  , mixPhase           :: !Double   -- flap phase [rad], in [0, 2π)
  } deriving stock (Eq, Show)

-- ── Stateless helpers (OrnithopterConfig.h constexpr) ──────────────

clamp :: Ord a => a -> a -> a -> a
clamp low high = max low . min high

clamp01 :: Double -> Double
clamp01 = clamp 0 1

orniThrottleFrequencyCommand :: Double -> Double -> Double -> Double
orniThrottleFrequencyCommand independentFreq01 throttle01 couplingPercent =
  let mix = clamp01 (couplingPercent * 0.01)
      independent = clamp01 independentFreq01
      throttle = clamp01 throttle01
  in independent + (throttle - independent) * mix

orniThrottleThrustExpo :: Double -> Double -> Double
orniThrottleThrustExpo throttle01 expoPercent =
  let x = clamp01 throttle01
      e = clamp (-1) 1 (expoPercent * 0.01)
  in if e >= 0 then (1 - e) * x + e * (x * x)
     else (1 + e) * x - e * ((2 - x) * x)

data ThrustShape = ThrustShape
  { thrustDwellBoost :: !Double  -- ferocity units
  , thrustCentreShift :: !Double -- skew units
  } deriving stock (Eq, Show)

orniThrottleThrustShape :: Double -> Double -> Double -> ThrustShape
orniThrottleThrustShape throttle01 mixPercent expoPercent =
  let mix = clamp01 (mixPercent * 0.01)
      shaped = orniThrottleThrustExpo throttle01 expoPercent
      alpha = shaped * mix
  in ThrustShape (alpha * (ferocityMax - ferocityMin)) (alpha * skewMax)

orniAileronRollShift :: Double -> Double -> Double
orniAileronRollShift aileronNorm couplingPercent =
  let mix = clamp01 (couplingPercent * 0.01)
      a = clamp (-1) 1 aileronNorm
  in a * mix

orniThrottleSkewRateShift :: Double -> Double -> Double
orniThrottleSkewRateShift throttleRatePerSec rateMixPercent =
  let mix = clamp01 (rateMixPercent * 0.01)
  in clamp skewMin skewMax (throttleRatePerSec * skewRateGain * mix)

orniAileronRollRateShift :: Double -> Double -> Double
orniAileronRollRateShift aileronRatePerSec rateMixPercent =
  let mix = clamp01 (rateMixPercent * 0.01)
  in clamp (-1) 1 (aileronRatePerSec * rollRateGain * mix)

-- ── The mixer kernel ───────────────────────────────────────────────

computeServoMixer :: Bool -> RcChannels -> FirmwareParams -> FirmwareState -> Double
                  -> (MixerOutput, FirmwareState)
computeServoMixer coupled rc params state dt =
  let aileronNorm = crsfToNorm (rcAileron rc)
      elevatorNorm = crsfToNorm (rcElevator rc)
      throttleUsF = rcThrottle rc
      armed = rcArm rc > 992
      wasFlapping = fwWasFlapping state
      threshold = if wasFlapping then flapThresholdUs - flapHysteresisUs
                                 else flapThresholdUs
      isFlapping = armed && throttleUsF > threshold
  in if isFlapping
       then flappingBranch coupled (fwProfile params) aileronNorm elevatorNorm rc params state dt
       else glideBranch (fwProfile params) aileronNorm elevatorNorm rc params state dt

-- | Glide: STATIC stroke positions — the wings hold their commanded angle and
-- do not flap. The common centre trims pitch (glide angle + elevator, a
-- position is the right mechanism for a static trim). Aileron commands a
-- static differential stroke: positive roll or yaw raises the RIGHT wing and
-- lowers the left. The coupled inertia produces the opposite fuselage motion.
-- Subsequent bank/yaw comes from local surface forces at their own lever arms,
-- including the fixed tail; a held wing angle is not a prescribed body bank.
glideBranch :: FlightProfile -> Double -> Double -> RcChannels
            -> FirmwareParams -> FirmwareState -> Double
            -> (MixerOutput, FirmwareState)
glideBranch prof aileronNorm elevatorNorm rc params state _dt =
  let rudderNorm = crsfToNorm (rcRudder rc)
      -- Pitch trim: the common stroke centre carries the glide angle + elevator.
      common = negate (profGlideAngleDeg prof + elevatorNorm * profElevatorScale prof * 0.01 * steerMaxDeg)
      -- Roll commands hinge position; the multibody solve supplies recoil.
      aileronDiff = aileronNorm * profAileronScale prof * 0.01 * glideAileronMaxDeg
      -- Yaw initiates the SAME bank as roll; the fixed tail then follows it.
      rudderDiff = rudderNorm * profRudderAmplitudeDiff prof * 0.01 * glideRudderMaxDeg
      left = clamp (-80) 80 (common - aileronDiff - rudderDiff)
      right = clamp (-80) 80 (common + aileronDiff + rudderDiff)
      rudderMix = clamp (-1) 1 (rudderNorm * fwRudderYawWeight params * 0.01
                                + aileronNorm * fwRudderRollWeight params * 0.01)
      output = MixerOutput
        { mixIsFlapping = False, mixFlapHz = 0, mixThrottlePct = 0
        , mixLeftWingDeg = neutralAngleDeg - left * angularMultiplier
        , mixRightWingDeg = neutralAngleDeg + right * angularMultiplier
        , mixLeftFlapDevDeg = left, mixRightFlapDevDeg = right
        , mixRudderUs = 1500 + rudderMix * 500
        , mixStrokeFerL = 0, mixReturnFerL = 0
        , mixPhase = oscPhase (fwOscillator state)
        }
  in (output, glideTransition state)

-- | Glide: reset slew sentinels (firmware else-branch). The oscillator idles —
-- the glide stroke is static and needs no cadence.
glideTransition :: FirmwareState -> FirmwareState
glideTransition state = state
  { fwWasFlapping = False
  , fwPrevThrottlePct = -1
  , fwThrottleRateLPF = 0
  , fwPrevAileronNorm = -2
  , fwAileronRateLPF = 0
  }

flappingBranch :: Bool -> FlightProfile -> Double -> Double -> RcChannels
               -> FirmwareParams -> FirmwareState -> Double
               -> (MixerOutput, FirmwareState)
flappingBranch coupled prof aileronNorm elevatorNorm rc params state dt =
  let throttleUsF = rcThrottle rc
      -- Speed modifier (0..1): scales the base frequency AND, in independent
      -- mode, is the frequency command. Mouse wheel / RC CH6 drive it.
      speedMod01 = clamp 0 1 (crsfToNorm (rcFreq rc) * 0.5 + 0.5)
      throttlePct = clamp 0 1 ((throttleUsF - flapThresholdUs) / (crsfRawMax - flapThresholdUs))
      coupling = if coupled then 100 else 0
      -- Coupled throttle spools cadence up early, then primarily grows travel.
      -- Independent mode takes the frequency command from the speed modifier.
      freqThrottle = let u = 1 - throttlePct in 1 - u * u * u * u
      freq01 = orniThrottleFrequencyCommand speedMod01 freqThrottle coupling
      speedFactor = 0.5 + speedMod01  -- 0.5x..1.5x base frequency
      -- Keep commanded travel independent of actuator speed. The complete
      -- waveform below determines the speed-limited cadence instead.
      servoSpeedDegS = servoDegPerSec params
      -- servoSpeedMargin (top-level) reserves a fraction of the no-load rate so
      -- the full geometric stroke stays trackable under aero load instead of
      -- back-driving the servo every downstroke.
      -- Apply the actuator ceiling once, to the actual shaped arc below.
      -- A full-travel ceiling here would waste speed at partial throttle.
      freqMax = fwFlapBaseFreqDh params * 0.1 * speedFactor
      requestedHz = freqMinHz + freq01 * (freqMax - freqMinHz)
      cadenceTarget = freqHz * 6.283185307

      -- Roll commands opposite stroke-centre offsets. Yaw combines an offset
      -- of the same sign with unequal travel, anchored toward the downstroke.
      -- Both controls enter the physical wing joints, not the host attitude.
      elevatorCmd = elevatorNorm * profElevatorScale prof * 0.01 * steerMaxDeg
      -- Flap Centre owns stroke position. Tail incidence and wing mount
      -- incidence are separate physical surface angles.
      flapCentreTrim = fwFlapCentreDeg params

      -- Elevator → ferocity asymmetry.
      elevFerScale = profElevatorFerocityMix prof * 0.01 * (ferocityMax - ferocityMin)
      elevUpBoost = max elevatorNorm 0 * elevFerScale
      elevDownBoost = max (-elevatorNorm) 0 * elevFerScale

      -- Throttle → thrust shape (dwell + centre-skew).
      thrust = orniThrottleThrustShape throttlePct (profThrottleThrustShapeMix prof)
                 (profThrottleThrustExpo prof)

      strokeFer = ferocityMin + profStrokeFerocity prof * 0.01 * (ferocityMax - ferocityMin)
                  + elevUpBoost + thrustDwellBoost thrust
      returnFer = ferocityMin + profReturnFerocity prof * 0.01 * (ferocityMax - ferocityMin)
                  + elevDownBoost + thrustDwellBoost thrust

      -- Rudder → L/R differential travel.
      rudderNorm = crsfToNorm (rcRudder rc)
      rudderAmpDiff = rudderNorm * profRudderAmplitudeDiff prof * 0.01

      -- Keep yaw's reversals synchronized. Differential dwell/ferocity made
      -- one wing advance through the stroke before the other, reversing the
      -- position differential partway through a nominally one-sided turn.
      -- Unequal travel still gives unequal physical velocity and force.
      strokeFerL = strokeFer
      strokeFerR = strokeFer
      returnFerL = returnFer
      returnFerR = returnFer

      -- Shared reversal threshold from BASE ferocities.
      limiarShared = limiarFromFerocities strokeFer returnFer

      -- Throttle-rate slew → symmetric skew boost/brake.
      (prevThrottle, throttleLPF) = slewUpdate (fwPrevThrottlePct state)
                                       (fwThrottleRateLPF state) throttlePct dt
      throttleRateBoost = orniThrottleSkewRateShift throttleLPF
                            (profThrottleSkewRateMix prof)
      strokeSkewEff = profStrokeSkew prof + thrustCentreShift thrust + throttleRateBoost
      returnSkewEff = profReturnSkew prof + thrustCentreShift thrust + throttleRateBoost

      -- Throttle commands travel (coupled mode: throttle owns the stroke arc,
      -- the speed modifier owns frequency only). Dwell/ferocity must not
      -- silently shrink that travel: their peak-rate cost limits cadence below
      -- instead. The speedFactor (mouse wheel) must NOT scale amplitude here —
      -- that made the wheel grow/shrink the stroke in coupled mode, which is
      -- the throttle's job; the wheel should only fine-tune the cadence.
      ampMaxDeg = strokeAmpMaxDeg params
      amplitude = clamp 0 ampMaxDeg (ampMaxDeg * throttlePct)

      -- Aileron + aileron-rate → opposite stroke/return timing skew. Rudder
      -- owns amplitude differential. Both mechanisms act on the same wings.
      aileronRollShift = orniAileronRollShift aileronNorm (profAileronSkewMix prof)
      (prevAileron, aileronLPF) = slewUpdate (fwPrevAileronNorm state)
                                    (fwAileronRateLPF state) aileronNorm dt
      aileronRateBoost = orniAileronRollRateShift aileronLPF (profAileronSkewRateMix prof)
      rollSkew = clamp (-80) 80 (-100 * (aileronRollShift + aileronRateBoost))
      -- Aileron also skews the downstroke/return duration in opposite
      -- directions. Equal-amplitude wings can then have different downstroke
      -- speed and lift; merely phase-shifting identical strokes has little
      -- cycle-mean roll authority. Keep one oscillator and bounded reversals.
      reversalShift = clamp (-0.3) 0.3 (0.6 * (aileronRollShift + aileronRateBoost))
      reversalL = clamp (0.2 * 2*pi) (0.8 * 2*pi) (limiarShared * (1 - reversalShift))
      reversalR = clamp (0.2 * 2*pi) (0.8 * 2*pi) (limiarShared * (1 + reversalShift))
      yawAmpDiff = clamp (-0.9) 0.9 rudderAmpDiff
      -- Aileron commands the real flap hinge position in glide and powered
      -- flight. Reserve travel for its centre shift; there is no incidence servo.
      aileronCmd = aileronNorm
                   * profAileronScale prof * 0.01 * steerMaxDeg * rollAuthorityGain
      -- Positive yaw raises the RIGHT wing (matching aileron's stick-right
      -- convention) and must therefore also give the RIGHT wing the larger
      -- stroke, so the visual differential and the static centre tilt agree on
      -- which wing is the active one instead of pulling against each other.
      amplitudeL = clamp 0 ampMaxDeg (amplitude * (1 - yawAmpDiff))
      amplitudeR = clamp 0 ampMaxDeg (amplitude * (1 + yawAmpDiff))
      -- Anchor yaw's larger stroke toward the downstroke. A pure amplitude
      -- change initially raises that wing on an upstroke, recoiling the
      -- fuselage into the opposite bank. The static component also gives the
      -- fixed tail the same bank-to-yaw sense in glide and powered flight.
      -- These are hinge commands, never prescribed body moments or attitudes.
      yawCentre = rudderAmpDiff * (glideRudderMaxDeg + amplitude)
      -- Allocate the finite linkage travel BEFORE choosing the common centre.
      -- Previously strong pitch/anhedral consumed the stroke arc (down to five
      -- degrees), losing power while cadence still assumed the full amplitude.
      -- Keep the commanded arc and differential; stop the common trim at the
      -- mechanically reachable centre instead of silently shortening the beat.
      differentialLimit = 80 - (amplitudeL + amplitudeR)/2
      differential = clamp (-differentialLimit) differentialLimit (aileronCmd + yawCentre)
      commonLow = max (-80 + amplitudeL + differential) (-80 + amplitudeR - differential)
      commonHigh = min (80 - amplitudeL + differential) (80 - amplitudeR - differential)
      commonCentre = clamp commonLow commonHigh (-elevatorCmd - flapCentreTrim)
      centreL = commonCentre - differential
      centreR = commonCentre + differential
      -- Servo-honest frequency ceiling: the peak rate of a full sinusoidal
      -- stroke at this amplitude must not exceed the no-load servo rate.
      -- Ferocity (dwell/shape) deliberately does NOT enter this ceiling — a
      -- snapped stroke simply makes the servo lag on the transition (rounding
      -- the dwell), which is the correct physical inefficiency; it must not
      -- collapse the commanded cadence. The old wavePeakDerivativeBound blew up
      -- as dwell→1 at 100% ferocity (1/(1-d) → ∞) and starved the servo to a
      -- near-stop — the "servo becomes unable to drive the wing" bug.
      --
      -- The ceiling uses the BASE commanded amplitude, NOT max(L,R): aileron/
      -- rudder amplitude differentials (±0.4) otherwise inflated one wing and
      -- dragged the SHARED oscillator cadence down every time the pilot
      -- steered (the "flap slows down when turning" artefact). A differential
      -- wing may briefly saturate its own servo at the peak — the physically
      -- correct, local consequence — but must not slow the whole stroke.
      peakRatePerHz = 2*pi * amplitude   -- deg/s per Hz (sine peak, base stroke)
      freqHz = min requestedHz (servoSpeedMargin*servoSpeedDegS / max 1e-6 peakRatePerHz)

      -- Oscillator advance → phase, then shaped wave pulse.
      (phase, nextOsc) = advanceOscillator cadenceTarget (fwKGainMod state) (fwAnchorGain params) dt
                           (fwOscillator state)
      pulseL = shapeWave phase strokeFerL returnFerL reversalL
                 (profFerocityShapeMix prof) (strokeSkewEff + rollSkew) (returnSkewEff - rollSkew)
      pulseR = shapeWave phase strokeFerR returnFerR reversalR
                 (profFerocityShapeMix prof) (strokeSkewEff - rollSkew) (returnSkewEff + rollSkew)

      degL = amplitudeL * pulseL
      degR = amplitudeR * pulseR

      -- Convert the COMPLETE mixed shaft command into mirrored physical flap
      -- coordinates. The old ABI path discarded every centre/position term.
      left = clamp (-80) 80 (degL + centreL)
      right = clamp (-80) 80 (degR + centreR)
      angleLeft = neutralAngleDeg - left * angularMultiplier
      angleRight = neutralAngleDeg + right * angularMultiplier

      -- Crest rudder (SERVO_2WING_1RUD).
      rudderMix = clamp (-1) 1
                    (rudderNorm * fwRudderYawWeight params * 0.01
                     + aileronNorm * fwRudderRollWeight params * 0.01)
      rudderUs = 1500 + rudderMix * 500

      nextState = state
        { fwWasFlapping = True
        , fwPrevThrottlePct = prevThrottle
        , fwThrottleRateLPF = throttleLPF
        , fwPrevAileronNorm = prevAileron
        , fwAileronRateLPF = aileronLPF
        , fwOscillator = nextOsc
        }

      output = MixerOutput
        { mixIsFlapping = True
        , mixFlapHz = freqHz
        , mixThrottlePct = throttlePct
        , mixLeftWingDeg = angleLeft
        , mixRightWingDeg = angleRight
        , mixLeftFlapDevDeg = left
        , mixRightFlapDevDeg = right
        , mixRudderUs = rudderUs
        , mixStrokeFerL = strokeFerL
        , mixReturnFerL = returnFerL
        , mixPhase = phase
        }
  in (output, nextState)

-- | Shared low-pass slew for throttle-rate and aileron-rate transients.
-- Returns @(newPrev, newLPF)@. The sentinel seeds without a kick.
slewUpdate :: Double -> Double -> Double -> Double -> (Double, Double)
slewUpdate prevSentinel lpf value dt =
  let prev = if prevSentinel < -1.5 then value else prevSentinel
  in if dt > 0
       then let rate = (value - prev) / dt
                alpha = dt / (skewRateLpfTau + dt)
                newLpf = lpf + (rate - lpf) * alpha
            in (value, newLpf)
       else (value, lpf)