# frozen_string_literal: true

module Born2Flap
  module UI
    # Renderers for the neutral Node tree. One grammar, many faces.
    #
    #   UMGEmitter.emit(node)  → Hash   (JSON wire format for Unreal)
    #   HtmlEmitter.emit(node) → String (HTML)
    #   RNEmitter.emit(node)   → String (React Native JSX)
    module Emitter
      # Widget primitives → per-renderer native widgets.
      # html = [tag, default_class]; rn = React Native component name.
      PRIMITIVES = {
        overlay:  { umg: "Overlay",        html: ["div", "overlay"],  rn: "View" },
        panel:    { umg: "Border",         html: ["div", "panel"],    rn: "View" },
        vbox:     { umg: "VerticalBox",    html: ["div", "vbox"],     rn: "View" },
        hbox:     { umg: "HorizontalBox",  html: ["div", "hbox"],     rn: "View" },
        text:     { umg: "TextBlock",      html: ["span", "text"],    rn: "Text" },
        button:   { umg: "Button",         html: ["button", nil],     rn: "TouchableOpacity" },
        slider:   { umg: "Slider",         html: ["input", nil],      rn: "Slider" },
        progress: { umg: "ProgressBar",    html: ["progress", nil],   rn: "ProgressBar" },
        image:    { umg: "Image",          html: ["img", nil],        rn: "Image" },
        list:     { umg: "ListView",       html: ["ul", nil],         rn: "FlatList" },
        gauge:    { umg: "Gauge",          html: ["div", "gauge"],    rn: "View" },
        spacer:   { umg: "Spacer",         html: ["div", "spacer"],   rn: "View" }
      }.freeze

      module_function

      def umg_type(type)
        mapping = PRIMITIVES[type.to_sym]
        mapping ? mapping[:umg] : "CustomWidget"
      end

      def html_spec(type)
        mapping = PRIMITIVES[type.to_sym]
        mapping ? mapping[:html] : ["div", nil]
      end

      def rn_type(type)
        mapping = PRIMITIVES[type.to_sym]
        mapping ? mapping[:rn] : "View"
      end

      def kebab(key)
        key.to_s.gsub(/([a-z0-9])([A-Z])/, '\1-\2').tr("_", "-").downcase
      end

      def escape(text)
        text.to_s
            .gsub("&", "&amp;")
            .gsub("<", "&lt;")
            .gsub(">", "&gt;")
            .gsub('"', "&quot;")
      end
    end

    # Emits the neutral tree as a JSON-friendly hash using Unreal widget names.
    class UMGEmitter
      def self.emit(node)
        {
          "type" => Emitter.umg_type(node.type),
          "props" => node.attrs.each_with_object({}) { |(k, v), o| o[k.to_s] = v },
          "children" => node.children.map { |child| emit(child) }
        }
      end
    end

    # Emits the neutral tree as an HTML string.
    class HtmlEmitter
      DIRECT_ATTRS = %i[id class min max value src alt placeholder step].freeze
      VOID_TAGS = %w[img input progress br hr].freeze

      def self.emit(node)
        new.render(node)
      end

      def render(node)
        if node.type == :text
          value = node.attrs[:value] || node.attrs[:text] || ""
          return %(<span class="text">#{Emitter.escape(value)}</span>)
        end

        tag, default_class = Emitter.html_spec(node.type)
        attrs = build_attrs(node, default_class)
        attrs["type"] ||= "range" if node.type == :slider

        attr_str = attrs.map { |k, v| %(#{k}="#{Emitter.escape(v)}") }.join(" ")

        if VOID_TAGS.include?(tag)
          %(<#{tag}#{attr_str.empty? ? "" : " #{attr_str}"} />)
        else
          inner = node.children.map { |child| render(child) }.join
          %(<#{tag}#{attr_str.empty? ? "" : " #{attr_str}"}>#{inner}</#{tag}>)
        end
      end

      private

      def build_attrs(node, default_class)
        out = {}
        node.attrs.each do |key, value|
          next if key == :class

          if DIRECT_ATTRS.include?(key)
            out[key.to_s] = value.to_s
          else
            out["data-#{Emitter.kebab(key)}"] = value.to_s
          end
        end

        classes = [default_class, node.attrs[:class]].compact
        out["class"] = classes.join(" ") unless classes.empty?
        out
      end
    end

    # Emits the neutral tree as React Native JSX.
    class RNEmitter
      def self.emit(node)
        new.render(node)
      end

      def render(node)
        component = Emitter.rn_type(node.type)

        if node.type == :text
          value = node.attrs[:value] || node.attrs[:text] || ""
          return %(<Text>#{Emitter.escape(value)}</Text>)
        end

        props = build_props(node)
        if node.children.empty?
          %(<#{component}#{props.empty? ? "" : " #{props}"} />)
        else
          inner = node.children.map { |child| render(child) }.join
          %(<#{component}#{props.empty? ? "" : " #{props}"}>#{inner}</#{component}>)
        end
      end

      private

      def build_props(node)
        parts = []

        case node.type
        when :vbox then parts << "style={{flexDirection:'column'}}"
        when :hbox then parts << "style={{flexDirection:'row'}}"
        end

        node.attrs.each do |key, value|
          parts << prop(key, value)
        end
        parts.compact.join(" ")
      end

      def prop(key, value)
        case key
        when :class, :id, :bind
          nil
        when :onPress, :on_press, :on_click
          %(onPress={() => dispatch('#{value}')})
        when :onChange, :on_change
          %(onChange={(v) => dispatch('#{value}', v)})
        when :min, :max, :value, :step
          %(#{key}={#{js_literal(value)}})
        else
          %(#{key}={#{js_literal(value)}})
        end
      end

      def js_literal(value)
        case value
        when String then "'#{value}'"
        when Symbol then "'#{value}'"
        when true, false then value.to_s
        when nil then "null"
        when Array then "[#{value.map { |v| js_literal(v) }.join(', ')}]"
        else value.to_s
        end
      end
    end
  end
end
