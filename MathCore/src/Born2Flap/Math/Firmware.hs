{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | Faithful emulation of the PteronautOS @Ornithopter::_computeServoMixer@
-- flapping control law (the non-Zephyrus pilot-input path). This is the
-- "firmware in the simulation": CRSF radio channels in, wing-servo angle
-- commands out, reacting identically to the real firmware's mixer kernel.
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
  , pilotToRc
  , crsfToNorm
  , crsfToFloat
  , normToRaw
  , computeServoMixer
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
crsfToNorm raw = crsfToFloat raw (-1) 1

normToRaw :: Double -> Double
normToRaw norm = 992 + norm * 819.5

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
  } deriving stock (Eq, Show)

defaultPilotInput :: PilotInput
defaultPilotInput = PilotInput 0 0 0 0

pilotToRc :: PilotInput -> RcChannels
pilotToRc pilot = RcChannels
  { rcAileron  = normToRaw (clamp (-1) 1 (piRoll pilot))
  , rcElevator = normToRaw (clamp (-1) 1 (piPitch pilot))
  , rcThrottle = crsfRawMin + clamp 0 1 (piThrottle pilot) * (crsfRawMax - crsfRawMin)
  , rcRudder   = normToRaw (clamp (-1) 1 (piYaw pilot))
  , rcArm      = 1811
  , rcFreq     = 1500
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

ampMaxDeg, freqMinHz, servoSpeedMsDefault :: Double
ampMaxDeg = 55
freqMinHz = 0.5
servoSpeedMsDefault = 70

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
  , profFlappingAngleDeg:: !Double  -- -15..+15, flap stroke centre offset
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
  [ FlightProfile 30 50 (-4) 0 40 60 50 0 0 0 0 0 0 0 0 0 0 0
  , FlightProfile 50 50 (-4) 0 40 60 50 0 0 0 0 0 0 0 0 0 0 0
  , FlightProfile 70 50   2  0 40 60 50 0 0 0 0 0 0 0 0 0 0 0
  ]

-- ── Runtime mixer params ───────────────────────────────────────────
--
-- Hardware/kernel knobs that live outside the flight profiles (servo speed,
-- battery-affected servo rate, flap frequency window ceiling, servo µs range).

data FirmwareParams = FirmwareParams
  { fwProfile       :: !FlightProfile
  , fwServoSpeedMs  :: !Double  -- ^ ms per 60° stroke (25..250); servo selection
  , fwFlapBaseFreqDh:: !Double  -- ^ deci-Hz (10..200), CH6 frequency ceiling
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
  , fwRudderYawWeight = 65
  , fwRudderRollWeight = 35
  , fwAnchorGain = anchorGainDefault
  , fwCadenceGain = cadenceGainDefault
  }

-- ── Firmware state (persistent across mixer ticks) ─────────────────

data FirmwareState = FirmwareState
  { fwWasFlapping      :: !Bool
  , fwPrevThrottlePct  :: !Double   -- sentinel < 0 = seed without slew kick
  , fwThrottleRateLPF  :: !Double
  , fwPrevAileronNorm  :: !Double   -- sentinel < -1.5 = seed without roll kick
  , fwAileronRateLPF   :: !Double
  , fwOscillator       :: !OscillatorState
  } deriving stock (Eq, Show)

defaultFirmwareState :: FirmwareState
defaultFirmwareState = FirmwareState
  { fwWasFlapping = False
  , fwPrevThrottlePct = -1
  , fwThrottleRateLPF = 0
  , fwPrevAileronNorm = -2
  , fwAileronRateLPF = 0
  , fwOscillator = defaultOscillator
  }

-- ── Mixer output ───────────────────────────────────────────────────
--
-- Wing servo angle commands in *degrees* (0..180, neutral = 100°), matching
-- the firmware's angleLeft/angleRight before the µs quantisation. The µs
-- step and integer truncation are PWM artefacts with no physical meaning for
-- a continuous game simulation, so they are deliberately omitted.

data MixerOutput = MixerOutput
  { mixIsFlapping      :: !Bool
  , mixFlapHz          :: !Double   -- 0 in glide
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

computeServoMixer :: RcChannels -> FirmwareParams -> FirmwareState -> Double
                  -> (MixerOutput, FirmwareState)
computeServoMixer rc params state dt
  | dt <= 0 = (glideOutput, state)
  | otherwise =
      let prof = fwProfile params
          aileronNorm = crsfToNorm (rcAileron rc)
          elevatorNorm = crsfToNorm (rcElevator rc)
          throttleUsF = rcThrottle rc
          armed = rcArm rc > 992
          wasFlapping = fwWasFlapping state
          threshold = if wasFlapping then flapThresholdUs - flapHysteresisUs
                                     else flapThresholdUs
          isFlapping = armed && throttleUsF > threshold
      in if isFlapping
           then flappingBranch prof aileronNorm elevatorNorm rc params state dt
           else (glideOutput { mixIsFlapping = False }, glideTransition state)

  where
    glideOutput = MixerOutput
      { mixIsFlapping = False, mixFlapHz = 0, mixThrottlePct = 0
      , mixLeftWingDeg = neutralAngleDeg, mixRightWingDeg = neutralAngleDeg
      , mixLeftFlapDevDeg = 0, mixRightFlapDevDeg = 0
      , mixRudderUs = 1500, mixStrokeFerL = 0, mixReturnFerL = 0, mixPhase = 0
      }

-- | Glide: decay oscillator, reset slew sentinels (firmware else-branch).
glideTransition :: FirmwareState -> FirmwareState
glideTransition state = state
  { fwWasFlapping = False
  , fwPrevThrottlePct = -1
  , fwThrottleRateLPF = 0
  , fwPrevAileronNorm = -2
  , fwAileronRateLPF = 0
  }

flappingBranch :: FlightProfile -> Double -> Double -> RcChannels
               -> FirmwareParams -> FirmwareState -> Double
               -> (MixerOutput, FirmwareState)
flappingBranch prof aileronNorm elevatorNorm rc params state dt =
  let throttleUsF = rcThrottle rc
      -- Frequency command: CH6 (independent) blended toward throttle.
      independentFreq01 = crsfToNorm (rcFreq rc) * 0.5 + 0.5
      throttlePct = clamp 0 1 ((throttleUsF - flapThresholdUs) / (crsfRawMax - flapThresholdUs))
      freq01 = orniThrottleFrequencyCommand independentFreq01 throttlePct
                (profThrottleFrequencyMix prof)
      freqMax = fwFlapBaseFreqDh params * 0.1
      freqHz = freqMinHz + freq01 * (freqMax - freqMinHz)
      cadenceTarget = freqHz * 6.283185307

      -- Amplitude = throttle % of the servo-speed-limited max at this freq.
      degPerSec = 60 / (fwServoSpeedMs params * 0.001)
      ampMaxHz = min ampMaxDeg (degPerSec / (2 * freqHz))
      amplitude = throttlePct * ampMaxHz

      -- Steering commands (deg).
      aileronCmd = aileronNorm * profAileronScale prof * 0.01 * steerMaxDeg
      elevatorCmd = elevatorNorm * profElevatorScale prof * 0.01 * steerMaxDeg
      flapCenterCmd = profFlappingAngleDeg prof

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

      -- Rudder → L/R differential (ferocity + amplitude).
      rudderNorm = crsfToNorm (rcRudder rc)
      rudderFer = rudderNorm * profRudderFerocityRange prof * 0.01 * differentialMax
      rudderAmpDiff = rudderNorm * profRudderAmplitudeDiff prof * 0.01

      strokeFerL = strokeFer + rudderFer
      strokeFerR = strokeFer - rudderFer
      returnFerL = returnFer + rudderFer
      returnFerR = returnFer - rudderFer

      -- Shared reversal threshold from BASE ferocities.
      limiarShared = limiarFromFerocities strokeFer returnFer

      -- Throttle-rate slew → symmetric skew boost/brake.
      (prevThrottle, throttleLPF) = slewUpdate (fwPrevThrottlePct state)
                                       (fwThrottleRateLPF state) throttlePct dt
      throttleRateBoost = orniThrottleSkewRateShift throttleLPF
                            (profThrottleSkewRateMix prof)
      strokeSkewEff = profStrokeSkew prof + thrustCentreShift thrust + throttleRateBoost
      returnSkewEff = profReturnSkew prof + thrustCentreShift thrust + throttleRateBoost

      -- Aileron + aileron-rate → differential amplitude (roll).
      aileronRollShift = orniAileronRollShift aileronNorm (profAileronSkewMix prof)
      (prevAileron, aileronLPF) = slewUpdate (fwPrevAileronNorm state)
                                    (fwAileronRateLPF state) aileronNorm dt
      aileronRateBoost = orniAileronRollRateShift aileronLPF (profAileronSkewRateMix prof)
      rollAmpDiff = clamp (-0.9) 0.9 (rudderAmpDiff + aileronRollShift + aileronRateBoost)
      amplitudeL = amplitude * (1 + rollAmpDiff)
      amplitudeR = amplitude * (1 - rollAmpDiff)

      -- Oscillator advance → phase, then shaped wave pulse.
      (phase, nextOsc) = advanceOscillator cadenceTarget 1 (fwAnchorGain params) dt
                           (fwOscillator state)
      pulseL = shapeWave phase strokeFerL returnFerL limiarShared
                 (profFerocityShapeMix prof) strokeSkewEff returnSkewEff
      pulseR = shapeWave phase strokeFerR returnFerR limiarShared
                 (profFerocityShapeMix prof) strokeSkewEff returnSkewEff

      degL = amplitudeL * pulseL
      degR = amplitudeR * pulseR

      angleLeft = neutralAngleDeg + (aileronCmd + elevatorCmd + flapCenterCmd - degL) * angularMultiplier
      angleRight = neutralAngleDeg + (aileronCmd - elevatorCmd - flapCenterCmd + degR) * angularMultiplier

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
        , mixLeftFlapDevDeg = degL
        , mixRightFlapDevDeg = degR
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