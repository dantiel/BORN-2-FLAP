# frozen_string_literal: true

require "minitest/autorun"
$LOAD_PATH.unshift File.expand_path("../lib", __dir__)
require "born2flap"

module Born2Flap
  module UI
    class TemplateTest < Minitest::Test
      def test_interpolates_simple_expressions
        out = Template.interpolate('%Stat{ value: #{altitude} }', altitude: 42)
        assert_equal "%Stat{ value: 42 }", out
      end

      def test_missing_keys_read_as_nil_for_defaults
        out = Template.interpolate('%Stat{ value: #{speed || 0} }', {})
        assert_equal "%Stat{ value: 0 }", out
      end

      def test_injects_multiline_fragments_intact
        source = <<~'UMG'
          %Section{ title: "X" }
          #{rows}
        UMG
        fragment = <<~'FRAG'
            %Field{ label: "A" }
              %Slider{ value: 1 }
            %Field{ label: "B" }
              %Slider{ value: 2 }
        FRAG
        out = Template.interpolate(source, rows: fragment)
        assert_includes out, '%Section{ title: "X" }'
        assert_equal 2, out.scan("%Field").length
      end

      def test_no_interpolation_is_a_fast_path
        assert_equal '%Text{ value: "hi" }', Template.interpolate('%Text{ value: "hi" }', {})
      end
    end

    class UmgssTest < Minitest::Test
      def test_parses_variables_and_resolves_references
        css = Umgss.parse(<<~'UMGSS')
          $brand: xxl
          Banner#brand
            size: $brand
        UMGSS
        assert_equal "xxl", css.variables[:brand]
        assert_equal "xxl", css.rules.first.props[:size]
      end

      def test_applies_component_defaults_without_overriding
        css = Umgss.parse(<<~'UMGSS')
          Stat
            size: m
        UMGSS
        tree = Node.new(:root, {}, [
          Node.new(:stat, { value: 1 }),
          Node.new(:stat, { value: 2, size: "xl" })
        ])
        css.apply(tree)
        assert_equal "m", tree.children[0][:size]
        assert_equal "xl", tree.children[1][:size] # explicit wins
      end

      def test_id_and_class_selectors
        css = Umgss.parse(<<~'UMGSS')
          Banner#brand
            size: xxl
          .primary
            tone: accent
        UMGSS
        brand = Node.new(:banner, { id: "brand" })
        text = Node.new(:text, { class: "primary other" })
        assert css.matches?(brand, "Banner#brand")
        assert css.matches?(text, ".primary")
        refute css.matches?(brand, "Banner#other")
      end

      def test_attribute_selector
        css = Umgss.parse(<<~'UMGSS')
          Banner[tone=accent]
            size: xl
        UMGSS
        assert css.matches?(Node.new(:banner, { tone: "accent" }), "Banner[tone=accent]")
        refute css.matches?(Node.new(:banner, { tone: "good" }), "Banner[tone=accent]")
      end

      def test_inline_comments_are_stripped
        css = Umgss.parse(<<~'UMGSS')
          $s: s  // a spacing token
          Field
            spacing: $s
        UMGSS
        assert_equal "s", css.rules.first.props[:spacing]
      end
    end

    class ViewControllerTest < Minitest::Test
      def test_flight_hud_cockpit_applies_theme
        hud = Views::FlightHud.render(:cockpit)
        brand = hud.find(:banner)
        assert_equal "brand", brand[:id]
        assert_equal "xxl", brand[:size] # from cockpit.umgss
      end

      def test_flight_hud_multiple_endpoints
        full = Views::FlightHud.render(:cockpit)
        minimal = Views::FlightHud.render(:minimal)
        assert_equal :overlay, full.type
        assert_equal :vbox, minimal.type
        assert_equal 2, minimal.children.length
      end

      def test_facade_still_works
        hud = FlightHud.tree
        assert_equal :overlay, hud.type
        assert_equal 0, hud.find(:stat)[:value] # default altitude
      end

      def test_tuning_editor_expands_data_rows
        ed = Views::TuningEditor.render(:editor)
        assert_equal 12, ed.each.to_a.count { |n| n.type == :slider }
        assert_equal 3, ed.each.to_a.count { |n| n.type == :number }
        assert_equal 5, ed.each.to_a.count { |n| n.type == :section }
      end

      def test_unknown_endpoint_raises
        assert_raises(ArgumentError) { Views::FlightHud.render(:nope) }
      end
    end
  end
end