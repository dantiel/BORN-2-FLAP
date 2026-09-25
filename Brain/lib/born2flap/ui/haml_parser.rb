# frozen_string_literal: true

module Born2Flap
  module UI
    # UMGHAML: a small, generic HAML-subset parser. It parses `%Widget` trees
    # (indentation, shorthand `#id` / `.class`, `{ attr: value }` hashes and
    # text lines `= "..."` / `| literal`) into a neutral Node tree.
    #
    # Deliberately NOT tied to HTML: `%Button` produces a `button` Node, and
    # only the HTML emitter turns it into `<button>`. This is the grammar layer
    # of the polymorphic UI.
    class HamlParser
      ParseError = Class.new(StandardError)

      def self.parse(source)
        new(source).parse
      end

      def initialize(source)
        @source = source
      end

      def parse
        root = Node.new(:root)
        stack = [[-1, root]]

        @source.each_line do |raw|
          line = raw.chomp
          next if line.strip.empty?

          indent = line[/\A */].length
          body = line[indent..].to_s

          kind, payload = parse_line(body)
          next if kind == :skip

          stack.pop while stack.length > 1 && stack.last[0] >= indent
          parent = stack.last[1]

          if kind == :text
            if parent.type == :text
              parent.attrs[:value] = payload
            else
              parent << Node.new(:text, { value: payload })
            end
          else
            node = Node.new(payload[0], payload[1])
            parent << node
            stack << [indent, node]
          end
        end

        root
      end

      private

      # Returns [:skip, nil], [:text, String] or [:node, [name, attrs]].
      def parse_line(body)
        body = body.strip
        return [:skip, nil] if body.start_with?("-#") || body.start_with?("/")

        if body.start_with?("%")
          [:node, parse_tag(body[1..])]
        elsif body.start_with?("=")
          expression = body[1..].strip
          value = expression =~ /\A["'](.*)["']\z/m ? $1 : expression
          [:text, value]
        elsif body.start_with?("|")
          [:text, body[1..].strip]
        elsif body.start_with?("-")
          [:skip, nil] # server-side code: ignored by the static parser
        else
          [:text, body]
        end
      end

      def parse_tag(rest)
        rest = rest.strip
        match = rest.match(/\A([A-Za-z_][A-Za-z0-9_]*)/)
        raise ParseError, "expected widget name in #{rest.inspect}" unless match

        name = match[1]
        rest = rest[match[0].length..].to_s

        attrs, rest = parse_shorthand(rest)

        attrs.merge!(parse_attr_hash(rest.strip)) unless rest.strip.empty?
        [name, attrs]
      end

      # `%Button#id.one.two` → [{ id: "id", class: "one two" }, remaining]
      def parse_shorthand(rest)
        attrs = {}
        cursor = rest
        loop do
          break unless cursor.start_with?("#", ".")

          if cursor.start_with?("#")
            cursor = cursor[1..]
            match = cursor.match(/\A([A-Za-z_][A-Za-z0-9_-]*)/)
            attrs[:id] = match[1] if match
          else
            cursor = cursor[1..]
            match = cursor.match(/\A([A-Za-z_][A-Za-z0-9_-]*)/)
            if match
              attrs[:class] = [attrs[:class], match[1]].compact.join(" ")
            end
          end
          cursor = (match ? cursor[match[0].length..] : cursor[1..]).to_s
        end
        [attrs, cursor]
      end

      def parse_attr_hash(src)
        src = src.strip
        raise ParseError, "malformed attributes: #{src.inspect}" unless src.start_with?("{") && src.end_with?("}")

        inner = src[1..-2]
        split_top_level(inner, ",").each_with_object({}) do |pair, out|
          next if pair.strip.empty?

          key, value = split_pair(pair)
          out[key] = value
        end
      end

      def split_pair(pair)
        pair = pair.strip
        if pair =~ /\A["']([^"']+)["']\s*=>\s*(.+)\z/
          [$1.to_sym, parse_value($2)]
        elsif pair =~ /\A([A-Za-z_][A-Za-z0-9_]*):\s*(.*)\z/
          [$1.to_sym, parse_value($2)]
        else
          raise ParseError, "malformed attribute pair: #{pair.inspect}"
        end
      end

      def parse_value(value)
        value = value.strip
        case value
        when /\A:\w+\z/ then value[1..].to_sym
        when /\A-?\d+\z/ then value.to_i
        when /\A-?\d+\.\d+\z/ then value.to_f
        when "true" then true
        when "false" then false
        when "nil" then nil
        when /\A"(.*)"\z/m then $1
        when /\A'(.*)'\z/m then $1
        when /\A\[(.*)\]\z/ then split_top_level($1, ",").map { |e| parse_value(e) }
        else value
        end
      end

      # Splits on a delimiter that is not nested inside quotes, brackets,
      # braces or parentheses.
      def split_top_level(src, delimiter)
        parts = []
        depth = 0
        quote = nil
        current = +""

        src.each_char do |ch|
          if quote
            current << ch
            quote = nil if ch == quote
            next
          end

          case ch
          when "\"", "'" then quote = ch
          when "[", "(", "{" then depth += 1
          when "]", ")", "}" then depth -= 1
          end

          if ch == delimiter && depth.zero?
            parts << current
            current = +""
          else
            current << ch
          end
        end
        parts << current
        parts
      end
    end
  end
end