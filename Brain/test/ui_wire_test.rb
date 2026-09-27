# frozen_string_literal: true

require "minitest/autorun"
$LOAD_PATH.unshift File.expand_path("../lib", __dir__)
require "born2flap"

module Born2Flap
  module UI
    class WireTest < Minitest::Test
      HUD = <<~HAML
        %Overlay{ id: "HUD" }
          %VBox
            %Text{ bind: "title" }
              = "BORN-2-FLAP"
            %Slider{ bind: "throttle", min: 0, max: 100 }
      HAML

      def build_ops
        tree = HamlParser.parse(HUD).children.first
        root = Root.new
        mount = root.render(tree)
        changed = HamlParser.parse(HUD.gsub("BORN-2-FLAP", "BORN-2-FLAP II")).children.first
        patch = root.render(changed)
        HostConfig.translate(mount) + HostConfig.translate(patch)
      end

      def test_dump_tree_is_valid_json
        tree = HamlParser.parse(HUD).children.first
        parsed = JSON.parse(Wire.dump_tree(tree))
        assert_equal "Overlay", parsed["type"]
        assert_equal "HUD", parsed["props"]["id"]
        assert_equal 1, parsed["children"].length
        assert_equal "VerticalBox", parsed["children"][0]["type"]
      end

      def test_dump_ops_round_trips
        ops = build_ops
        json = Wire.dump_ops(ops)
        parsed = JSON.parse(json)

        assert parsed.is_a?(Array)
        refute_empty parsed
        assert_equal %w[create_instance update_props], parsed.map { |o| o["op"] }.uniq.sort

        create = parsed.find { |o| o["op"] == "create_instance" }
        assert_equal "Overlay", create["node"]["type"]
        assert_equal [], create["path"]

        update = parsed.find { |o| o["op"] == "update_props" }
        assert_equal [0, 0], update["path"]
        assert_equal "BORN-2-FLAP II", update["props"]["value"]
      end

      def test_effect_ops_serialize
        driver = EffectDriver.new
        driver.bind(:speed_blur, path: [0, 1])
        ops = driver.drive(Effects::Telemetry.default.merge(airspeed: 18.0))
        json = Wire.dump_ops(ops)
        parsed = JSON.parse(json)

        assert_equal "set_material_params", parsed[0]["op"]
        assert_equal [0, 1], parsed[0]["path"]
        assert_in_delta 0.6, parsed[0]["params"]["BlurRadius"], 0.001
      end
    end
  end
end