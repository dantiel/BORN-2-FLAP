# frozen_string_literal: true

require "minitest/autorun"
$LOAD_PATH.unshift File.expand_path("../lib", __dir__)
require "born2flap"

module Born2Flap
  module UI
    # Template.evaluate — inline Ruby (`- code`, `= expr`, `%Widget= expr`,
    # `#{...}`) compiled down to plain UMGHAML.
    class TemplateEvalTest < Minitest::Test
      def test_interpolation_still_works
        out = Template.evaluate('%Stat{ value: #{altitude} }', altitude: 42)
        assert_equal "%Stat{ value: 42 }", out.strip
      end

      def test_silent_code_loop_repeats_markup
        out = Template.evaluate(<<~'H', {})
          %VBox
            - 3.times do |i|
              %Stat{ value: #{i} }
        H
        assert_equal 3, out.scan("%Stat{ value:").length
        assert_includes out, "%Stat{ value: 2 }"
      end

      def test_silent_code_assignment_is_visible_to_interpolation
        out = Template.evaluate(<<~'H', {})
          - x = 21
          %Stat{ value: #{x * 2} }
        H
        assert_equal "%Stat{ value: 42 }", out.strip
      end

      def test_conditional_wraps_markup
        out = Template.evaluate(<<~'H', flag: true)
          %VBox
            - if flag
              %Banner{ text: "ON" }
            - else
              %Banner{ text: "OFF" }
        H
        assert_includes out, 'text: "ON"'
        refute_includes out, 'text: "OFF"'
      end

      def test_each_with_index_over_locals
        out = Template.evaluate(<<~'H', rows: [{ name: "A", v: 1 }, { name: "B", v: 2 }])
          %VBox
            - rows.each do |r|
              %Stat{ label: "#{r[:name]}", value: #{r[:v]} }
        H
        assert_equal 2, out.scan("%Stat{").length
        assert_includes out, 'label: "B"'
      end

      def test_inline_widget_output_becomes_text_child
        out = Template.evaluate('%TextBlock= altitude', altitude: 900)
        assert_includes out, "%TextBlock"
        assert_includes out, "= 900"
      end

      def test_inline_widget_output_with_attrs
        out = Template.evaluate('%TextBlock{ tone: "accent" }= "#{speed}"', speed: 12)
        assert_includes out, '%TextBlock{ tone: "accent" }'
        assert_includes out, "= 12"
      end

      def test_standalone_output_is_a_text_node
        out = Template.evaluate('= 2 + 2', {})
        assert_includes out, "= 4"
      end

      def test_comments_are_skipped
        out = Template.evaluate(<<~'H', {})
          %VBox
            -# hidden
            / also hidden
            %Banner{ text: "kept" }
        H
        assert_includes out, 'text: "kept"'
        refute_includes out, "hidden"
      end

      def test_parses_into_the_expected_tree
        source = <<~'H'
          %VBox
            - 2.times do |i|
              %Stat{ value: #{i}, tone: "info" }
        H
        tree = HamlParser.parse(Template.evaluate(source, {})).children.first
        assert_equal :vbox, tree.type
        assert_equal 2, tree.children.length
        assert_equal :stat, tree.children.first.type
        assert_equal 1, tree.children.last[:value]
      end

      def test_silent_line_children_render_at_the_silent_lines_indent
        src = <<~'H'
          %Panel
            %HBox
              %Button{ label: "PREV" }
            - if flag
              %Button{ label: "RESUME" }
            %Button{ label: "QUIT" }
        H
        tree = HamlParser.parse(Template.evaluate(src, flag: true)).children.first
        # tree is the `%Panel` node.
        assert_equal :panel, tree.type
        # RESUME must be a sibling of HBox/QUIT (children of Panel), not a child
        # of HBox.
        assert_equal %i[hbox button button], tree.children.map(&:type)
        assert_equal "RESUME", tree.children[1][:label]
        assert_equal "QUIT", tree.children[2][:label]

        # When the flag is false, the QUIT sibling must survive (it is outside
        # the `if`).
        off = HamlParser.parse(Template.evaluate(src, flag: false)).children.first
        assert_equal %i[hbox button], off.children.map(&:type)
        assert_equal "QUIT", off.children.last[:label]
      end

      def test_sibling_after_loop_is_not_duplicated
        out = Template.evaluate(<<~'H', pois: [{ name: "A" }, { name: "B" }])
          %VBox
            - pois.each_with_index do |p, i|
              %Button{ label: "#{p[:name]}" }
            %Button{ label: "CLOSE" }
        H
        tree = HamlParser.parse(out).children.first
        labels = tree.children.map { |n| n[:label] }
        assert_equal ["A", "B", "CLOSE"], labels
      end

      def test_fast_path_returns_plain_markup_unchanged
        src = '%Text{ value: "hi" }'
        assert_equal src, Template.evaluate(src, {})
      end
    end
  end
end