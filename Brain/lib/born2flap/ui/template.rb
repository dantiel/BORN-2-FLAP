# frozen_string_literal: true

module Born2Flap
  module UI
    # Template — inline-Ruby evaluation for `.umghaml` views.
    #
    # A `.umghaml` template is the *semantic structure* of a screen; dynamic
    # values and structure are written in Ruby, evaluated in one shared context:
    #
    #   #{expr}        → string interpolation (also inside `{...}` attrs)
    #   - code         → silent Ruby (assignments, loops, conditionals)
    #   = expr         → evaluated output as a text node
    #   %Widget= expr  → evaluated output as the widget's text content
    #
    #   Template.evaluate("%Stat{ value: #{altitude} }", altitude: 42)
    #   # => "%Stat{ value: 42 }"
    #
    #   Template.evaluate("%VBox\n- 3.times do |i|\n  %Stat{ value: #{i} }\n", {})
    #   # => "%VBox\n  %Stat{ value: 0 }\n  %Stat{ value: 1 }\n  %Stat{ value: 2 }\n"
    #
    # `evaluate` is a *preprocessor*: its result is plain UMGHAML, still parsed
    # by HamlParser, so the grammar stays the single source of truth. Only the
    # dynamic bits are evaluated; markup and indentation pass through intact.
    module Template
      module_function

      # `#{...}`-only fast path. Evaluates each expression independently against
      # `locals`; kept for callers that only need value interpolation.
      def interpolate(source, locals = {})
        return source unless source.include?('#{')

        context = Context.new(locals)
        source.gsub(INTERPOLATION) { context.instance_eval(Regexp.last_match(1)) }
      end

      # Full inline-Ruby evaluation → plain UMGHAML (ready for HamlParser).
      def evaluate(source, locals = {})
        return source unless source.is_a?(String) && !source.empty?
        return source unless source.include?('#{') ||
                            source.match?(/(^|\n) *[=\-]/) ||
                            source.match?(/%[A-Za-z_]\w*[^\n]*=/)

        context = Context.new(locals)
        program = Compiler.compile(source)
        context.instance_eval(program, '(umghaml-template)', 1)
      end

      INTERPOLATION = /#\{(.*?)\}/m

      # Compiles a `.umghaml` source into a Ruby program that, when evaluated,
      # returns the final plain-UMGHAML string. Indentation drives block
      # structure: markup/text lines nested under a `- ` line run inside that
      # Ruby block, and dedenting emits the matching `end`s. Silent `- ` lines
      # are "transparent" for markup nesting — their children render at the
      # indent the `- ` line itself sits at.
      class Compiler
        # A `- ` line like `else`/`elsif`/`when`/`rescue`/`ensure`/`in` belongs
        # to an already-open block; it never opens one of its own.
        CONTINUATIONS = /\A(else|elsif|when|rescue|ensure|in)\b/.freeze

        # The generated-program literal for a newline (`"\n"` as Ruby source).
        NEWLINE = %q{"\n"}

        def self.compile(source)
          new(source).compile
        end

        def initialize(source)
          @source = source
        end

        def compile
          out = ['_out = +""']
          ruby_stack = []   # [indent, open_blocks] → emits `end` on dedent
          indent_stack = [] # [indent, silent_delta] → tracks output indentation

          @source.each_line do |raw|
            line = raw.chomp
            next if line.strip.empty?

            indent = line[/\A */].length
            body = line[indent..].to_s
            stripped = body.strip

            kind = line_kind(stripped)
            next if kind == :skip

            # Close Ruby blocks. A continuation (`- else`, `- when`, …) keeps
            # its own block open, so it only closes *deeper* blocks; any other
            # line also closes a block opened at its own indent (a sibling).
            if continuation?(stripped)
              while ruby_stack.any? && ruby_stack.last[0] > indent
                _, n = ruby_stack.pop
                n.times { out << 'end' }
              end
            else
              while ruby_stack.any? && ruby_stack.last[0] >= indent
                _, n = ruby_stack.pop
                n.times { out << 'end' }
              end
            end

            # Nearest ancestor line + its accumulated silent-indent delta.
            indent_stack.pop while indent_stack.any? && indent_stack.last[0] >= indent
            parent_indent = indent_stack.any? ? indent_stack.last[0] : 0
            parent_delta = indent_stack.any? ? indent_stack.last[1] : 0

            if kind == :code
              code = stripped[1..].strip
              next if code.empty?

              out << code
              opened = block_openers(code)
              ruby_stack << [indent, opened] if opened.positive?
              # A silent `-` line consumes its own indent for its descendants.
              indent_stack << [indent, parent_delta + (indent - parent_indent)]
              next
            end

            output_indent = indent - parent_delta
            indent_stack << [indent, parent_delta]

            case kind
            when :output
              out << emit_text(output_indent, stripped[1..].strip)
            when :tag
              tag, expr = split_inline_tag(stripped)
              out << emit_line(output_indent, tag)
              out << emit_text(output_indent + 2, expr) if expr
            else
              out << emit_line(output_indent, stripped)
            end
          end

          while ruby_stack.any?
            _, n = ruby_stack.pop
            n.times { out << 'end' }
          end

          out << '_out'
          out.join("\n")
        end

        private

        def line_kind(stripped)
          return :skip if stripped.start_with?('-#', '/')
          return :code if stripped.start_with?('-')
          return :output if stripped.start_with?('=')
          return :tag if stripped.start_with?('%')

          :literal
        end

        def continuation?(stripped)
          stripped.start_with?('-') && stripped[1..].strip =~ CONTINUATIONS
        end

        # Emit a markup/text line, evaluating any `#{...}` fragments in place.
        def emit_line(indent, text)
          seq = [dq(pad(indent))]
          split_interpolation(text).each do |kind, val|
            seq << (kind == :lit ? dq(val) : "(#{val}).to_s")
          end
          seq << NEWLINE
          "_out << #{seq.join(' << ')}"
        end

        # Emit an evaluated output as a text line (`= value` in the result).
        def emit_text(indent, expr)
          seq = [dq(pad(indent)), dq('= '), "(#{expr}).to_s", NEWLINE]
          "_out << #{seq.join(' << ')}"
        end

        # Split a line into literal / interpolation fragments.
        def split_interpolation(text)
          segments = []
          pos = 0
          text.scan(INTERPOLATION) do
            match = Regexp.last_match
            segments << [:lit, text[pos...match.begin(0)]] if match.begin(0) > pos
            segments << [:expr, match[1]]
            pos = match.end(0)
          end
          segments << [:lit, text[pos..]] if pos < text.length
          segments
        end

        # Split `%Tag{...}= expr` into the tag part and the inline expression.
        def split_inline_tag(body)
          name = body.match(/\A%[A-Za-z_][A-Za-z0-9_]*/)
          return [body, nil] unless name

          idx = name[0].length
          # `#id` / `.class` shorthand
          loop do
            break unless idx < body.length && ['#', '.'].include?(body[idx])

            idx += 1
            idx += 1 while idx < body.length && body[idx] =~ /[A-Za-z0-9_-]/
          end
          # balanced `{...}` attribute hash
          idx = skip_balanced(body, idx) if idx < body.length && body[idx] == '{'

          j = idx
          j += 1 while j < body.length && body[j] =~ /[ \t]/
          return [body, nil] unless j < body.length && body[j] == '='

          [body[0...j], body[(j + 1)..].to_s.strip]
        end

        def skip_balanced(src, idx)
          depth = 0
          quote = nil
          while idx < src.length
            ch = src[idx]
            if quote
              quote = nil if ch == quote
            else
              case ch
              when '"', "'" then quote = ch
              when '{' then depth += 1
              when '}' then depth -= 1
              end
            end
            idx += 1
            break if depth.zero? && quote.nil?
          end
          idx
        end

        def block_openers(code)
          c = code.strip
          return 0 if c.empty? || c =~ CONTINUATIONS

          # A leading control keyword opens its own block; a trailing `do`
          # (while/until/for x do) is redundant then.
          return 1 if c =~ /\A(if|unless|while|until|for|case|begin|class|module|def)\b/

          return 1 if c =~ /\bdo(\s*\|[^|]*\|)?\s*\z/ # `3.times do |i|`
          return 1 if c.end_with?('{')                 # `items.each { |i|`

          0
        end

        def pad(indent)
          ' ' * indent
        end

        # Double-quoted Ruby string literal for an already interpolation-free
        # fragment: escape only `\` and `"` (a line never carries a newline).
        def dq(fragment)
          escaped = fragment.gsub(/[\\"]/) { |c| "\\#{c}" }
          %("#{escaped}")
        end
      end

      # The evaluation context: keys become methods; unknown keys are `nil`.
      class Context
        def initialize(locals)
          @locals = locals
          @lang = locals[:lang] || I18n::DEFAULT
          locals.each_key do |key|
            define_singleton_method(key) { @locals[key] }
          end
        end

        # Translation sugar for templates: `#{t('hud.altitude')}`.
        def t(key)
          I18n.t(key, lang: @lang)
        end

        def lang
          @lang
        end

        def [](key)
          @locals[key]
        end

        def fetch(key, default = nil)
          @locals.fetch(key, default)
        end

        def locals
          @locals
        end

        def method_missing(name, *args, &block)
          return @locals[name] if @locals.key?(name)
          return nil if args.empty? && block.nil?

          super
        end

        def respond_to_missing?(name, include_private = false)
          @locals.key?(name) || super
        end
      end
    end
  end
end