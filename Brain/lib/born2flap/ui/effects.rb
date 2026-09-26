# frozen_string_literal: true

module Born2Flap
  module UI
    # Physics-driven material effects — the "crown jewels" of react-native-umg.
    #
    # Every effect is a PURE function (Telemetry → material params). The telemetry
    # snapshot comes from the Haskell core (wingbeat, airspeed, AoA, thermals) and
    # the resulting params are UMaterialInstanceDynamic scalar names the UMG host
    # will call SetScalarParameterValue with verbatim. Because the layer is pure,
    # it is fully headless-testable without Unreal — exactly like the Reconciler.
    #
    #   Haskell (Physik) ──► Telemetry ──► Effects (pure) ──► :ui_effect ──► UMG
    module Effects
      # Immutable snapshot of physics telemetry. `merge` returns a new snapshot,
      # never mutates — keeping the effect layer side-effect-free.
      Telemetry = Struct.new(
        :time, :wingbeat_hz, :airspeed, :aoa_deg, :altitude,
        :thermal_strength, :focus_depth,
        keyword_init: true
      ) do
        DEFAULTS = {
          time: 0.0, wingbeat_hz: 3.0, airspeed: 0.0, aoa_deg: 0.0,
          altitude: 0.0, thermal_strength: 0.0, focus_depth: 0.0
        }.freeze

        def self.default
          new(**DEFAULTS)
        end

        def merge(overrides)
          self.class.new(**to_h.merge(overrides))
        end
      end

      # One effect = one pure function (telemetry → material params).
      # Keys are UMaterialInstanceDynamic scalar-parameter names, applied by the
      # UMG host as-is. No renderer knowledge leaks in here.
      EFFECTS = {
        # Glow pulses with the wingbeat — the UI breathes with the bird.
        flap_glow: lambda { |t|
          pulse = 0.5 + 0.5 * Math.sin(2 * Math::PI * t.wingbeat_hz * t.time)
          {
            "EmissiveIntensity" => 0.2 + 0.8 * pulse,
            "GlowRadius" => 0.1 + 0.9 * pulse
          }
        },
        # Progressive blur scales with airspeed — faster flight, softer UI.
        speed_blur: lambda { |t|
          s = clamp01(t.airspeed / 30.0)
          { "BlurRadius" => s, "BlurStrength" => s }
        },
        # Menus shimmer like rising thermals, driven by thermal strength.
        thermal_shimmer: lambda { |t|
          s = clamp01(t.thermal_strength)
          {
            "ShimmerAmplitude" => s,
            "ShimmerFrequency" => 2.0 + 6.0 * s,
            "ShimmerSpeed" => 1.0 + 3.0 * s
          }
        },
        # Refraction like air over a wing, driven by angle of attack.
        aero_lensing: lambda { |t|
          a = clamp(t.aoa_deg / 15.0, -1.0, 1.0)
          { "RefractionIndex" => 1.0 + 0.05 * a, "Distortion" => a.abs }
        },
        # Depth-of-field: everything off the focus plane softens with distance.
        progressive_blur: lambda { |t|
          d = (t.altitude - t.focus_depth).abs
          { "FocusDepth" => t.focus_depth, "BlurNear" => d, "BlurFar" => d * 0.5 }
        }
      }.freeze

      module_function

      def clamp01(value)
        clamp(value, 0.0, 1.0)
      end

      def clamp(value, lo, hi)
        [[value.to_f, lo].max, hi].min
      end

      def compute(effect, telemetry)
        EFFECTS.fetch(effect.to_sym) do
          raise ArgumentError, "unknown effect: #{effect.inspect}"
        end.call(telemetry)
      end

      def names
        EFFECTS.keys
      end
    end
  end
end
