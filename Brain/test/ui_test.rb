# frozen_string_literal: true

require "minitest/autorun"
$LOAD_PATH.unshift File.expand_path("../lib", __dir__)
require "born2flap"

module Born2Flap
  module UI
    HUD_SOURCE = <<~HAML
      %Overlay{ id: "HUD" }
        %VBox
          %Text{ bind: "title" }
            = "BORN-2-FLAP"
          %Slider{ bind: "throttle", min: 0, max: 100 }
          %Button{ onPress: "toggle_flap", class: "primary" }
            = "FLAP"
    HAML

    class HamlParserTest < Minitest::Test
      def test_parses_tag_tree_with_nesting
        tree = HamlParser.parse(HUD_SOURCE)

        overlay = tree.children.first
        assert_equal :overlay, overlay.type
        assert_equal "HUD", overlay[:id]

        vbox = overlay.children.first
        assert_equal :vbox, vbox.type
        assert_equal 3, vbox.children.length

        text = vbox.children[0]
        assert_equal :text, text.type
        assert_equal "title", text[:bind]
        assert_equal "BORN-2-FLAP", text[:value]

        slider = vbox.children[1]
        assert_equal :slider, slider.type
        assert_equal "throttle", slider[:bind]
        assert_equal 0, slider[:min]
        assert_equal 100, slider[:max]

        button = vbox.children[2]
        assert_equal :button, button.type
        assert_equal "toggle_flap", button[:onPress]
        assert_equal "primary", button[:class]
        assert_equal "FLAP", button.children.first[:value]
      end

      def test_shorthand_id_and_class
        tree = HamlParser.parse("%Button#flap.primary.secondary")
        button = tree.children.first
        assert_equal "flap", button[:id]
        assert_equal "primary secondary", button[:class]
      end

      def test_attr_hash_value_types
        tree = HamlParser.parse(
          "%Gauge{ bind: :airspeed, min: 0.0, max: 120.5, visible: true, gone: nil, ticks: [0, 1, 2] }"
        )
        gauge = tree.children.first
        assert_equal :airspeed, gauge[:bind]
        assert_in_delta 0.0, gauge[:min]
        assert_in_delta 120.5, gauge[:max]
        assert_equal true, gauge[:visible]
        assert_nil gauge[:gone]
        assert_equal [0, 1, 2], gauge[:ticks]
      end

      def test_comments_and_code_lines_are_skipped
        tree = HamlParser.parse("%Overlay\n  -# hidden\n  - ruby_code\n  %Text\n    = \"hi\"")
        overlay = tree.children.first
        assert_equal 1, overlay.children.length
        assert_equal :text, overlay.children.first.type
      end

      def test_literal_text_line
        tree = HamlParser.parse("%Text\n  | literal text")
        text = tree.children.first
        assert_equal "literal text", text[:value]
      end
    end

    class NodeTest < Minitest::Test
      def test_find_and_each
        tree = HamlParser.parse(HUD_SOURCE)
        assert_equal :slider, tree.find(:slider).type
        assert_nil tree.find(:gauge)
        assert_equal 7, tree.each.to_a.length # root + overlay + vbox + text + slider + button + text
      end

      def test_to_h
        tree = HamlParser.parse("%Text{ value: \"hi\" }")
        assert_equal({ type: :text, attrs: { value: "hi" }, children: [] }, tree.children.first.to_h)
      end
    end

    class StoreTest < Minitest::Test
      def screen_reducer
        ->(state, action) do
          state ||= { current: :main_menu }
          action[:type] == "UI_NAVIGATE" ? { current: action[:to] } : state
        end
      end

      def hud_reducer
        ->(state, action) do
          state ||= {}
          action[:type] == "HUD_BIND" ? state.merge(action[:bind] => action[:value]) : state
        end
      end

      def test_dispatch_and_subscribe
        store = Store.new(Reducer.combine(screen: screen_reducer),
                          { screen: { current: :boot } })
        seen = []
        store.subscribe { |state, _action| seen << state[:screen][:current] }

        store.dispatch(type: "UI_NAVIGATE", to: :free_flight)
        assert_equal :free_flight, store.state[:screen][:current]
        assert_equal [:free_flight], seen
      end

      def test_combine_reducers_isolates_slices
        root = Reducer.combine(screen: screen_reducer, hud: hud_reducer)
        store = Store.new(root)

        store.dispatch(type: "UI_NAVIGATE", to: :race)
        store.dispatch(type: "HUD_BIND", bind: :throttle, value: 0.7)

        assert_equal({ current: :race }, store.state[:screen])
        assert_equal({ throttle: 0.7 }, store.state[:hud])
      end

      def test_combine_reducers_returns_same_state_when_unchanged
        root = Reducer.combine(screen: screen_reducer, hud: hud_reducer)
        store = Store.new(root)
        store.dispatch(type: "UI_NAVIGATE", to: :race) # establish state
        before = store.state

        store.dispatch(type: "UNKNOWN")
        assert_same before, store.state
      end

      def test_reducers_may_not_dispatch
        holder = {}
        evil = ->(_state, _action) { holder[:store].dispatch(type: "X") }
        store = Store.new(evil)
        holder[:store] = store
        assert_raises(Store::Error) { store.dispatch(type: "GO") }
      end
    end

    class EmitterTest < Minitest::Test
      def test_umg_emitter_maps_to_unreal_widgets
        tree = HamlParser.parse(HUD_SOURCE)
        json = UMGEmitter.emit(tree.children.first)

        assert_equal "Overlay", json["type"]
        assert_equal "HUD", json.dig("props", "id")
        vbox = json["children"].first
        assert_equal "VerticalBox", vbox["type"]
        assert_equal "Slider", vbox["children"][1]["type"]
        assert_equal "throttle", vbox["children"][1]["props"]["bind"]
      end

      def test_html_emitter_produces_html_not_custom_tags
        tree = HamlParser.parse("%Button{ onPress: \"toggle_flap\", class: \"primary\" }\n  = \"FLAP\"")
        html = HtmlEmitter.emit(tree.children.first)
        assert_includes html, "<button"
        assert_includes html, 'class="primary"'
        assert_includes html, 'data-on-press="toggle_flap"'
        assert_includes html, "<span class=\"text\">FLAP</span>"
        assert_includes html, "</button>"
      end

      def test_html_emitter_slider_becomes_range_input
        tree = HamlParser.parse("%Slider{ bind: \"throttle\", min: 0, max: 100 }")
        html = HtmlEmitter.emit(tree.children.first)
        assert_includes html, "<input"
        assert_includes html, 'type="range"'
        assert_includes html, 'min="0"'
        assert_includes html, 'max="100"'
      end

      def test_rn_emitter_produces_jsx_with_dispatch
        tree = HamlParser.parse("%Button{ onPress: \"toggle_flap\" }\n  = \"FLAP\"")
        jsx = RNEmitter.emit(tree.children.first)
        assert_includes jsx, "<TouchableOpacity"
        assert_includes jsx, "onPress={() => dispatch('toggle_flap')}"
        assert_includes jsx, "<Text>FLAP</Text>"
      end

      def test_rn_emitter_vbox_is_column
        tree = HamlParser.parse("%VBox\n  %Text{ value: \"x\" }")
        jsx = RNEmitter.emit(tree.children.first)
        assert_includes jsx, "flexDirection:'column'"
      end
    end
  end
end