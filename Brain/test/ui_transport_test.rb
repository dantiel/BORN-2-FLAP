# frozen_string_literal: true

require "minitest/autorun"
require "stringio"
$LOAD_PATH.unshift File.expand_path("../lib", __dir__)
require "born2flap"

module Born2Flap
  module UI
    class TransportTest < Minitest::Test
      HUD = <<~HAML
        %Overlay{ id: "HUD" }
          %VBox
            %Text{ bind: "title" }
              = "BORN-2-FLAP"
            %Slider{ bind: "throttle", min: 0, max: 100, effect: :speed_blur }
      HAML

      # The rendered TOP widget (not the document root) — keeps effect paths and
      # patch paths aligned under the root-is-`[]` convention.
      def top
        HamlParser.parse(HUD).children.first
      end

      def test_flush_emits_mount_frame
        io = StringIO.new
        bus = EventBus.new
        transport = Transport.new(bus: bus, io: io)
        root = Root.new(bus: bus)

        root.render(top)
        assert_operator transport.flush, :>, 0

        lines = io.string.lines
        assert_equal 1, lines.length
        ops = JSON.parse(lines.first)
        assert_equal ["create_instance"], ops.map { |o| o["op"] }.uniq
        assert_equal "Overlay", ops[0]["node"]["type"]
      end

      def test_patch_and_effect_share_one_frame_with_aligned_paths
        io = StringIO.new
        bus = EventBus.new
        transport = Transport.new(bus: bus, io: io)
        root = Root.new(bus: bus)
        driver = EffectDriver.new(bus: bus)

        tree = top
        root.render(tree)
        driver.bind_tree(tree) # slider effect → path [0,1]
        driver.drive(Effects::Telemetry.default.merge(airspeed: 30.0))
        transport.flush

        lines = io.string.lines
        assert_equal 1, lines.length
        ops = JSON.parse(lines.first)
        kinds = ops.map { |o| o["op"] }.uniq.sort
        assert_equal %w[create_instance set_material_params], kinds

        effect = ops.find { |o| o["op"] == "set_material_params" }
        assert_equal [0, 1], effect["path"]
        assert_in_delta 1.0, effect["params"]["BlurRadius"], 0.001
      end

      def test_flush_returns_zero_when_idle
        io = StringIO.new
        transport = Transport.new(bus: EventBus.new, io: io)
        assert_equal 0, transport.flush
        assert_equal "", io.string
      end

      def test_multiple_ticks_yield_multiple_frames
        io = StringIO.new
        bus = EventBus.new
        transport = Transport.new(bus: bus, io: io)
        driver = EffectDriver.new(bus: bus).bind(:speed_blur, path: [0, 1])

        driver.drive(Effects::Telemetry.default.merge(airspeed: 12.0)); transport.flush
        driver.drive(Effects::Telemetry.default.merge(airspeed: 24.0)); transport.flush

        lines = io.string.lines
        assert_equal 2, lines.length
        assert_in_delta 0.4, JSON.parse(lines[0]).first["params"]["BlurRadius"], 0.001
        assert_in_delta 0.8, JSON.parse(lines[1]).first["params"]["BlurRadius"], 0.001
      end
    end
  end
end
