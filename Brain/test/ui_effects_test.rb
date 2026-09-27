# frozen_string_literal: true

require "minitest/autorun"
$LOAD_PATH.unshift File.expand_path("../lib", __dir__)
require "born2flap"

module Born2Flap
  module UI
    class EffectsTest < Minitest::Test
      def test_telemetry_defaults_and_immutable_merge
        t = Effects::Telemetry.default
        assert_equal 3.0, t.wingbeat_hz
        assert_equal 0.0, t.airspeed

        t2 = t.merge(airspeed: 42.0)
        assert_equal 42.0, t2.airspeed
        assert_equal 0.0, t.airspeed # original unchanged
      end

      def test_flap_glow_peaks_at_quarter_cycle
        t = Effects::Telemetry.default.merge(wingbeat_hz: 2.0, time: 0.125)
        params = Effects.compute(:flap_glow, t)
        assert_in_delta 1.0, params["EmissiveIntensity"], 0.001 # sin(π/2) = 1
        assert_in_delta 1.0, params["GlowRadius"], 0.001
      end

      def test_speed_blur_scales_and_clamps_with_airspeed
        slow = Effects.compute(:speed_blur, Effects::Telemetry.default.merge(airspeed: 0.0))
        fast = Effects.compute(:speed_blur, Effects::Telemetry.default.merge(airspeed: 60.0))
        assert_equal 0.0, slow["BlurRadius"]
        assert_equal 1.0, fast["BlurRadius"] # clamped at 1.0
      end

      def test_thermal_shimmer_amplitude_bounded
        hot = Effects.compute(:thermal_shimmer, Effects::Telemetry.default.merge(thermal_strength: 1.0))
        assert_equal 1.0, hot["ShimmerAmplitude"]
        assert_in_delta 8.0, hot["ShimmerFrequency"]
      end

      def test_aero_lensing_refraction_follows_aoa
        pos = Effects.compute(:aero_lensing, Effects::Telemetry.default.merge(aoa_deg: 15.0))
        neg = Effects.compute(:aero_lensing, Effects::Telemetry.default.merge(aoa_deg: -15.0))
        assert_operator pos["RefractionIndex"], :>, 1.0
        assert_operator neg["RefractionIndex"], :<, 1.0
      end

      def test_progressive_blur_grows_with_altitude_offset
        near = Effects.compute(:progressive_blur, Effects::Telemetry.default.merge(altitude: 100.0, focus_depth: 100.0))
        far = Effects.compute(:progressive_blur, Effects::Telemetry.default.merge(altitude: 200.0, focus_depth: 100.0))
        assert_equal 0.0, near["BlurNear"]
        assert_equal 100.0, far["BlurNear"]
      end

      def test_unknown_effect_raises
        assert_raises(ArgumentError) { Effects.compute(:nope, Effects::Telemetry.default) }
      end
    end

    class EffectDriverTest < Minitest::Test
      def test_drive_publishes_material_ops_on_bus
        bus = EventBus.new
        driver = EffectDriver.new(bus: bus)
        received = []
        bus.subscribe(:ui_effect) { |payload| received << payload[:ops] }

        driver.bind(:speed_blur, path: [0, 0])
        ops = driver.drive(Effects::Telemetry.default.merge(airspeed: 15.0))
        bus.drain

        assert_equal 1, ops.length
        assert_equal :set_material_params, ops[0][0]
        assert_equal [0, 0], ops[0][1]
        assert_in_delta 0.5, ops[0][2]["BlurRadius"]
        assert_equal 1, received.length
      end

      def test_bind_tree_binds_declared_effects
        source = <<~HAML
          %Overlay{ id: "HUD" }
            %Panel{ material: "FrostedGlass", effect: "speed_blur" }
            %Text{ effect: "flap_glow" }
        HAML
        tree = HamlParser.parse(source)
        driver = EffectDriver.new
        driver.bind_tree(tree)

        assert_equal 2, driver.bindings.length
        assert_equal [0, 0], driver.bindings[0].path
        assert_equal :speed_blur, driver.bindings[0].effect
        assert_equal [0, 1], driver.bindings[1].path
        assert_equal :flap_glow, driver.bindings[1].effect
      end

      def test_bind_unknown_effect_raises
        assert_raises(ArgumentError) { EffectDriver.new.bind(:nope, path: [0]) }
      end
    end
  end
end
