# frozen_string_literal: true

module Born2Flap
  module UI
    # Umgss — a tiny, SASS-like *styling* language for view controllers.
    #
    # It is deliberately NOT visual: like SASS it declares rules and variables,
    # but the values are *semantic tokens* (tone: accent, size: xl, spacing: m)
    # rather than pixels/colors. The C++ host resolves those tokens in
    # Born2FlapUiTheme.h — the thin rendering layer is the only place that turns
    # a token into a concrete Slate value. So the Brain stays behaviour + intent,
    # and styling stays declarative + host-resolved.
    #
    # Grammar (indentation-scoped, SASS — no braces, no semicolons):
    #
    #   $accent: accent          // variable (token alias)
    #
    #   Banner                   // bare component rule → default for every Banner
    #     tone: accent
    #     size: xl
    #
    #   Banner[tone=good]        // attribute-qualified rule
    #     size: l
    #
    #   Banner#brand             // id selector (matched via `id` in umghaml)
    #     size: xxl
    #
    #   .primary                 // semantic class (matched via `class` in umghaml)
    #     tone: accent
    #
    #   // and `/* block comments */` are ignored, as are blank lines.
    #
    # `apply` walks a neutral tree and merges matched rules as *defaults*: an
    # explicit prop already on the node (from the .umghaml) always wins, so the
    # template can override a themed default without the theme overriding intent.
    class Umgss
      Rule = Struct.new(:selector, :props, keyword_init: true)

      # Semantic tokens the C++ theme layer (Born2FlapUiTheme.h + renderer)
      # resolves. Every key here is read from props on the host side — no
      # dead tokens. (tone→color, size→font px, spacing→layout gap,
      # align/valign→alignment, weight→bold, wrap→autowrap.)
      STYLE_KEYS = %i[tone size spacing align valign weight wrap].freeze

      attr_reader :variables, :rules

      def initialize(variables = {}, rules = [])
        @variables = variables
        @rules = rules
      end

      def self.parse(source)
        vars = {}
        rules = []
        # Stack of open rules: { indent:, selector:, props: {} }.
        stack = []

        lines(source).each do |indent, line|
          # Any rule indented deeper than `indent` has ended.
          while stack.any? && indent <= stack.last[:indent]
            done = stack.pop
            rules << Rule.new(selector: done[:selector], props: done[:props])
          end

          if line.start_with?("$")
            key, value = split_pair(line[1..])
            vars[key.to_sym] = parse_value(value)
          elsif stack.any? && indent > stack.last[:indent] && line.include?(":")
            # A property declaration, nested under the current rule.
            key, value = split_pair(line)
            stack.last[:props][key.to_sym] = parse_value(value)
          else
            # A selector line (indented lines without `:` become nested rules).
            stack << { indent: indent, selector: line, props: {} }
          end
        end

        while stack.any?
          done = stack.pop
          rules << Rule.new(selector: done[:selector], props: done[:props])
        end

        # Resolve `$var` references inside rule props after all vars are known.
        rules.each do |rule|
          rule.props.each do |key, value|
            next unless value.is_a?(String) && value.start_with?("$")

            name = value[1..].to_sym
            rule.props[key] = vars[name] if vars.key?(name)
          end
        end

        new(vars, rules)
      end

      def self.parse_file(path)
        parse(File.read(path))
      end

      # Merge themed defaults into a neutral tree (mutates and returns it).
      def apply(tree)
        tree.each do |node|
          next if node.type == :root

          rules.each do |rule|
            next unless matches?(node, rule.selector)

            rule.props.each do |key, value|
              sym = key.to_sym
              next unless STYLE_KEYS.include?(sym)
              next if node.attrs.key?(sym)

              node.attrs[sym] = value
            end
          end
        end
        tree
      end

      # True when `node` satisfies `selector`. Supports:
      #   Banner                 → type match
      #   Banner[tone=good]      → type + attribute conditions
      #   Banner#brand           → type + id
      #   Banner.primary         → type + class
      #   Banner#brand.primary   → type + id + class
      #   #brand / .primary      → id-only / class-only
      def matches?(node, selector)
        sel = selector.strip

        if (m = sel.match(/\A([A-Za-z_][A-Za-z0-9_]*)\[([^\]]+)\]\z/))
          return false unless node.type == m[1].downcase.to_sym

          return match_conditions?(node, m[2])
        end

        type, id, classes = parse_compound(sel)
        return false if type && node.type != type
        return false if id && node.attrs[:id].to_s != id

        classes.all? { |klass| class_match?(node, klass) }
      end

      class << self
        private

        # Return `[indent, stripped]` pairs, comments removed, blanks dropped.
        def lines(source)
          strip_comments(source)
            .lines
            .map { |l| [l[/\A[ \t]*/].length, l.strip] }
            .reject { |_, s| s.empty? }
        end

        def strip_comments(source)
          source.gsub(%r{/\*.*?\*/}m, "")
                .lines
                .map { |line| line.sub(%r{//.*$}, "") }
                .join
        end

        def split_pair(line)
          key, value = line.split(":", 2)
          [key.to_s.strip, value.to_s.strip]
        end

        def parse_value(value)
          case value
          when /\A-?\d+\z/      then value.to_i
          when /\A-?\d+\.\d+\z/ then value.to_f
          when /\A"(.*)"\z/m    then Regexp.last_match(1)
          when /\A'(.*)'\z/m    then Regexp.last_match(1)
          when "true"           then true
          when "false"          then false
          when "nil"            then nil
          else value
          end
        end
      end

      private

      # Split `Banner#brand.primary` → [type, id, classes].
      def parse_compound(sel)
        type = nil
        if (m = sel.match(/\A([A-Za-z_][A-Za-z0-9_]*)/))
          type = m[1].downcase.to_sym
          rest = sel[m[0].length..].to_s
        else
          rest = sel
        end

        id = nil
        classes = []
        cursor = rest
        loop do
          if cursor.start_with?("#")
            m = cursor.match(/\A#([A-Za-z_][A-Za-z0-9_-]*)/)
            id = m[1] if m
            cursor = (m ? cursor[m[0].length..] : cursor[1..]).to_s
          elsif cursor.start_with?(".")
            m = cursor.match(/\A\.([A-Za-z_][A-Za-z0-9_-]*)/)
            classes << m[1] if m
            cursor = (m ? cursor[m[0].length..] : cursor[1..]).to_s
          else
            break
          end
        end

        [type, id, classes]
      end

      def match_conditions?(node, cond)
        cond.split(",").map(&:strip).all? do |clause|
          if clause.include?("=")
            key, value = clause.split("=", 2).map(&:strip)
            node.attrs[key.to_sym].to_s == value
          else
            node.attrs.key?(clause.to_sym)
          end
        end
      end

      def class_match?(node, name)
        node.attrs[:class].to_s.split(/\s+/).include?(name)
      end
    end
  end
end
