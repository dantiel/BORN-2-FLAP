# frozen_string_literal: true

module Born2Flap
  module Audio
    # AERO-AUDIO-PHYSICS ENGINE — the spectral transformation of aerodynamic
    # forces into sound. This is the "small aeroaudiophysicsengine" the killer
    # feature demands: it maps wing geometry + flow behaviour + servo load +
    # the MathCore phase-lock RESONANCE into per-voice spectral parameters.
    #
    # Every voice is a PURE function (Telemetry → params), exactly like the UI
    # Effects layer — headless-testable without Unreal. The host (C++ synth in
    # Born2FlapAudioEngine.h / a USynthComponent in UMG) is a "dumb" renderer
    # that turns the params into PCM. The physics stays in ONE place: here.
    #
    # Five voices, one physics:
    #   wind     — broadband aerodynamic noise, power ∝ airspeed³ (Lighthill),
    #              cutoff ∝ airspeed (faster flight = brighter hiss).
    #   wing     — "the voice of the great spirit": a deep phase-locked
    #              fundamental + downstroke whoosh. Its *purity* (tone) is the
    #              MathCore resonance δ — locked (δ→0) sings clean, wind-broken
    #              (δ grows) breathes ragged.
    #   servo_l/r— the cricket/groan duality: sweep rate stridulates a high
    #              chirp, hinge load pulls it down into a guttural groan that
    #              drops in pitch and hardens with distortion as stall nears.
    #   leaves   — high-band foliage rustle, only audible low over the canopy,
    #              agitated by thermal turbulence.
    #
    # Perspective is applied uniformly: inverse-square distance attenuation,
    # Doppler pitch shift from approach speed, sine-law stereo pan from bearing,
    # and air absorption (highs roll off with distance).
    #
    #   Haskell (Physik) ──► Telemetry ──► AeroAudio (pure) ──► :audio_effect
    module AeroAudio
      # Immutable snapshot of physics telemetry. `merge` returns a new snapshot.
      Telemetry = Struct.new(
        :time, :wingbeat_hz, :airspeed, :aoa_deg, :altitude, :thermal_strength,
        # servo / resonance / stall state (from B2F_FirmwareOutput + Resonance)
        :servo_load_l, :servo_load_r,   # 0..1 hinge-torque load (0 idle, 1 stall)
        :flap_angle_deg,                # current flap deviation (servo position)
        :sweep_rate,                    # deg/s servo position change (chirp driver)
        :phase_error_rad,               # MathCore resonance δ (rad)
        :k_gain_mod,                    # phase-advance demand (0.5..1.5)
        :stall_margin,                  # 0..1 (1 far from stall, 0 stalled)
        # perspective (listener-relative)
        :listener_distance,             # meters
        :listener_bearing,              # rad (-π..π), 0 = straight ahead
        :approach_speed,                # m/s along listener axis (+ = approaching)
        keyword_init: true
      ) do
        DEFAULTS = {
          time: 0.0, wingbeat_hz: 3.0, airspeed: 0.0, aoa_deg: 0.0,
          altitude: 0.0, thermal_strength: 0.0,
          servo_load_l: 0.0, servo_load_r: 0.0, flap_angle_deg: 0.0,
          sweep_rate: 0.0, phase_error_rad: 0.0, k_gain_mod: 1.0,
          stall_margin: 1.0,
          listener_distance: 5.0, listener_bearing: 0.0, approach_speed: 0.0
        }.freeze

        def self.default
          new(**DEFAULTS)
        end

        def merge(overrides)
          self.class.new(**to_h.merge(overrides))
        end
      end

      VOICES = %i[wind wing servo_l servo_r leaves].freeze
      SPEED_OF_SOUND = 343.0 # m/s

      # A servo voice is the same physics, only its hinge-load input differs.
      SERVO = lambda do |load, t|
        {
          "gain" => clamp01(0.3 + 0.7 * load),
          "chirp" => clamp01(t.sweep_rate / 400.0),
          "groan" => clamp01(load),
          "strain" => clamp01(1.0 - t.stall_margin),
          "sweep" => 6.0 + 10.0 * clamp01(t.sweep_rate / 400.0),
          "groan_pitch" => 90.0 - 40.0 * load,          # droops under load
          "brightness" => clamp01(1.0 - load)            # highs yield to groan
        }
      end

      # Intrinsic spectral transformation per voice (before perspective).
      INTRINSIC = {
        wind: lambda { |t|
          {
            "gain" => clamp01((t.airspeed / 30.0)**3),
            "brightness" => clamp01(t.airspeed / 40.0),
            "gust_depth" => clamp01(0.1 + 0.6 * t.thermal_strength),
            "gust_rate" => 0.4 + 0.6 * t.wingbeat_hz
          }
        },
        wing: lambda { |t|
          {
            "rate" => t.wingbeat_hz,
            "gain" => clamp01((t.wingbeat_hz / 8.0) * (0.3 + 0.7 * clamp01(t.airspeed / 20.0))),
            "tone" => clamp01(1.0 - t.phase_error_rad.abs / 0.5),
            "whoosh" => clamp01(t.airspeed / 25.0),
            "brightness" => clamp01(t.airspeed / 40.0)
          }
        },
        servo_l: lambda { |t| SERVO.call(t.servo_load_l, t) },
        servo_r: lambda { |t| SERVO.call(t.servo_load_r, t) },
        leaves: lambda { |t|
          foliage = clamp01(1.0 - t.altitude / 50.0)
          {
            "gain" => clamp01(t.airspeed / 20.0) * foliage,
            "turbulence" => clamp01(0.15 + 0.7 * t.thermal_strength),
            "brightness" => 1.0
          }
        }
      }.freeze

      module_function

      def clamp01(value)
        clamp(value, 0.0, 1.0)
      end

      def clamp(value, lo, hi)
        [[value.to_f, lo].max, hi].min
      end

      # Perspective: distance attenuation, Doppler pitch, sine-law pan, and air
      # absorption. Applied to every voice uniformly — sound is always heard
      # *from somewhere*, and that somewhere is listener-relative.
      def perspective(t)
        dist = [t.listener_distance, 0.5].max
        att = 1.0 / (1.0 + (dist / 10.0)**2)
        doppler = clamp((SPEED_OF_SOUND + t.approach_speed) / SPEED_OF_SOUND, 0.6, 1.6)
        pan = clamp(Math.sin(t.listener_bearing), -1.0, 1.0)
        damp = clamp01(1.0 - dist / 200.0)
        { att: att, doppler: doppler, pan: pan, damp: damp }
      end

      # Full transformation for one voice (intrinsic + perspective).
      def compute(voice, telemetry)
        intrinsic = INTRINSIC.fetch(voice.to_sym) do
          raise ArgumentError, "unknown audio voice: #{voice.inspect}"
        end.call(telemetry)
        apply_perspective!(intrinsic, telemetry)
        intrinsic
      end

      # All voices at once → { voice => params }.
      def mix(telemetry)
        VOICES.each_with_object({}) { |voice, out| out[voice] = compute(voice, telemetry) }
      end

      def apply_perspective!(params, telemetry)
        p = perspective(telemetry)
        params["gain"] = (params.fetch("gain", 0.0) * p[:att]).round(4)
        params["pitch"] = p[:doppler].round(4)
        params["pan"] = p[:pan].round(4)
        # Air absorption rolls the highs off with distance.
        params["brightness"] = (params["brightness"] * p[:damp]).round(4) if params.key?("brightness")
        params
      end
    end
  end
end
