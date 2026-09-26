# frozen_string_literal: true

require "minitest/autorun"
$LOAD_PATH.unshift File.expand_path("../lib", __dir__)
require "born2flap"

module Born2Flap
  module Audio
    class AeroAudioTest < Minitest::Test
      def t(overrides = {})
        AeroAudio::Telemetry.default.merge(overrides)
      end

      def test_telemetry_defaults_and_immutable_merge
        base = AeroAudio::Telemetry.default
        assert_equal 3.0, base.wingbeat_hz
        assert_equal 5.0, base.listener_distance

        merged = base.merge(airspeed: 42.0)
        assert_equal 42.0, merged.airspeed
        assert_equal 0.0, base.airspeed
      end

      def test_wind_power_grows_cubically_with_airspeed
        still = AeroAudio.compute(:wind, t(airspeed: 0.0, listener_distance: 0.5))
        fast = AeroAudio.compute(:wind, t(airspeed: 30.0, listener_distance: 0.5))
        assert_equal 0.0, still["gain"]
        assert_in_delta 1.0, fast["gain"], 0.02 # (30/30)^3 = 1, near-field att ≈ 1
        assert_equal 0.0, still["brightness"]
        assert_in_delta 0.75, fast["brightness"], 0.02 # 30/40
      end

      def test_servo_groan_and_strain_track_load_and_stall
        idle = AeroAudio.compute(:servo_l, t(servo_load_l: 0.0, stall_margin: 1.0))
        loaded = AeroAudio.compute(:servo_l, t(servo_load_l: 1.0, stall_margin: 0.0))
        assert_equal 0.0, idle["groan"]
        assert_equal 0.0, idle["strain"]
        assert_in_delta 1.0, loaded["groan"]
        assert_in_delta 1.0, loaded["strain"]
        assert_operator idle["groan_pitch"], :>, loaded["groan_pitch"] # droops
      end

      def test_servo_chirp_follows_sweep_rate
        slow = AeroAudio.compute(:servo_r, t(sweep_rate: 0.0))
        fast = AeroAudio.compute(:servo_r, t(sweep_rate: 400.0))
        assert_equal 0.0, slow["chirp"]
        assert_in_delta 1.0, fast["chirp"]
      end

      def test_wing_tone_is_resonance_locked
        locked = AeroAudio.compute(:wing, t(phase_error_rad: 0.0))
        ragged = AeroAudio.compute(:wing, t(phase_error_rad: 0.5))
        assert_in_delta 1.0, locked["tone"]
        assert_equal 0.0, ragged["tone"]
      end

      def test_leaves_audible_only_low_and_fast
        high = AeroAudio.compute(:leaves, t(airspeed: 20.0, altitude: 100.0))
        low = AeroAudio.compute(:leaves, t(airspeed: 20.0, altitude: 0.0))
        assert_equal 0.0, high["gain"]      # above canopy
        assert_operator low["gain"], :>, 0.0
      end

      def test_distance_attenuates
        near = AeroAudio.compute(:wind, t(airspeed: 30.0, listener_distance: 0.5))
        far = AeroAudio.compute(:wind, t(airspeed: 30.0, listener_distance: 100.0))
        assert_operator near["gain"], :>, far["gain"]
      end

      def test_doppler_shifts_pitch_with_approach
        coming = AeroAudio.compute(:wind, t(approach_speed: 100.0))
        going = AeroAudio.compute(:wind, t(approach_speed: -100.0))
        assert_operator coming["pitch"], :>, 1.0
        assert_operator going["pitch"], :<, 1.0
      end

      def test_pan_follows_bearing
        left = AeroAudio.compute(:wind, t(listener_bearing: -Math::PI / 2))
        right = AeroAudio.compute(:wind, t(listener_bearing: Math::PI / 2))
        assert_in_delta(-1.0, left["pan"])
        assert_in_delta 1.0, right["pan"]
      end

      def test_unknown_voice_raises
        assert_raises(ArgumentError) { AeroAudio.compute(:nope, t) }
      end
    end

    class AudioDriverTest < Minitest::Test
      def test_drive_publishes_audio_ops_on_bus
        bus = EventBus.new
        driver = AudioDriver.new(bus: bus)
        received = []
        bus.subscribe(:audio_effect) { |payload| received << payload[:ops] }

        ops = driver.drive(AeroAudio::Telemetry.default)
        bus.drain

        assert_equal AeroAudio::VOICES.length, ops.length
        assert_equal [:wind, :wing, :servo_l, :servo_r, :leaves], ops.map { |o| o[1].to_sym }
        assert_equal :set_audio_params, ops[0][0]
        assert_equal 1, received.length
      end
    end

    class AudioWireTest < Minitest::Test
      def test_serialize_audio_op
        json = UI::Wire.dump_ops([[:set_audio_params, "wind", { "gain" => 0.5 }]])
        parsed = JSON.parse(json).first
        assert_equal "set_audio_params", parsed["op"]
        assert_equal "wind", parsed["voice"]
        assert_equal 0.5, parsed["params"]["gain"]
      end
    end
  end
end