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

    class ReconcilerTest < Minitest::Test
      def setup
        @reconciler = Reconciler.new
      end

      def tree
        Node.new(:root, {}, [
          Node.new(:overlay, { id: "HUD" }, [
            Node.new(:slider, { bind: "throttle", min: 0, max: 100 })
          ])
        ])
      end

      def test_identical_trees_produce_empty_patch
        assert_empty @reconciler.diff(tree, tree)
      end

      def test_prop_change_produces_update_props
        changed = Node.new(:root, {}, [
          Node.new(:overlay, { id: "HUD" }, [
            Node.new(:slider, { bind: "throttle", min: 0, max: 100, value: 42 })
          ])
        ])
        patch = @reconciler.diff(tree, changed)

        assert_equal 1, patch.length
        assert_equal :update_props, patch[0][:op]
        assert_equal [0, 0], patch[0][:path]
        assert_equal({ value: 42 }, patch[0][:props])
      end

      def test_prop_removal_yields_nil_value
        stripped = Node.new(:root, {}, [
          Node.new(:overlay, { id: "HUD" }, [
            Node.new(:slider, { bind: "throttle" })
          ])
        ])
        patch = @reconciler.diff(tree, stripped)

        update = patch.find { |op| op[:op] == :update_props }
        assert_equal({ min: nil, max: nil }, update[:props])
      end

      def test_type_change_produces_replace
        swapped = Node.new(:root, {}, [
          Node.new(:overlay, { id: "HUD" }, [
            Node.new(:button, {})
          ])
        ])
        patch = @reconciler.diff(tree, swapped)

        replace = patch.find { |op| op[:op] == :replace }
        assert_equal :button, replace[:node].type
      end

      def test_added_child_produces_insert_child
        empty = Node.new(:root)
        filled = Node.new(:root, {}, [Node.new(:text, { value: "hi" })])
        patch = @reconciler.diff(empty, filled)

        assert_equal 1, patch.length
        assert_equal :insert_child, patch[0][:op]
        assert_equal [], patch[0][:path]
        assert_equal 0, patch[0][:index]
      end

      def test_removed_child_produces_remove_child
        filled = Node.new(:root, {}, [Node.new(:text, { value: "hi" })])
        empty = Node.new(:root)
        patch = @reconciler.diff(filled, empty)

        assert_equal 1, patch.length
        assert_equal :remove_child, patch[0][:op]
        assert_equal 0, patch[0][:index]
      end
    end

    class RootTest < Minitest::Test
      def test_render_publishes_patch_on_bus
        bus = EventBus.new
        root = Root.new(bus: bus)
        received = []
        bus.subscribe(:ui_patch) { |payload| received << payload[:patch] }

        t1 = Node.new(:root, {}, [Node.new(:text, { value: "a" })])
        t2 = Node.new(:root, {}, [Node.new(:text, { value: "b" })])

        root.render(t1)
        bus.drain
        root.render(t2)
        bus.drain

        assert_equal 2, received.length
        assert_equal :replace, received[0][0][:op] # first render mounts the tree
        assert_equal :update_props, received[1][0][:op]
      end

      def test_render_same_tree_publishes_nothing
        bus = EventBus.new
        root = Root.new(bus: bus)
        count = 0
        bus.subscribe(:ui_patch) { |_payload| count += 1 }

        tree = Node.new(:root, {}, [Node.new(:text, { value: "a" })])
        root.render(tree)
        bus.drain
        patch = root.render(tree)
        bus.drain

        assert_nil patch
        assert_equal 1, count
      end
    end

    class HostConfigTest < Minitest::Test
      def test_translate_maps_replace_with_node_to_create_instance
        patch = [{ op: :replace, path: [], node: Node.new(:overlay) }]
        assert_equal [[:create_instance, [], patch[0][:node]]], HostConfig.translate(patch)
      end

      def test_translate_maps_replace_with_nil_to_remove_instance
        patch = [{ op: :replace, path: [0], node: nil }]
        assert_equal [[:remove_instance, [0]]], HostConfig.translate(patch)
      end

      def test_translate_maps_child_and_prop_ops
        node = Node.new(:text, { value: "hi" })
        patch = [
          { op: :insert_child, path: [], index: 0, node: node },
          { op: :remove_child, path: [], index: 1 },
          { op: :update_props, path: [0], props: { value: 42 } }
        ]
        assert_equal [
          [:append_child, [], 0, node],
          [:remove_child, [], 1],
          [:update_props, [0], { value: 42 }]
        ], HostConfig.translate(patch)
      end

      def test_host_ops_are_stable
        assert_equal %i[create_instance remove_instance append_child remove_child update_props],
                     HostConfig::HOST_OPS
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