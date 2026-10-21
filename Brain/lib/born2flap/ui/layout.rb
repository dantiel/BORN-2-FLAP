# frozen_string_literal: true
module Born2Flap
  module UI
    # Composite views expand to the same portable primitives as handwritten HAML.
    module Layout
      module_function
      def expand(node)
        node.children = node.children.map { |child| expand(child) }
        return node unless node.type == :scrollpage
        raise ArgumentError, 'ScrollPage needs header, content, footer' unless node.children.length == 3
        header, content, footer = node.children
        top = node[:safe_top] || 130
        bottom = node[:safe_bottom] || 76
        scroll_attrs = {scroll_blur: true, pad_top: top, pad_bottom: bottom,
          edge: node[:edge] || 0.12, edge_bottom: node[:edge_bottom] || 0.10}
        scroll_attrs[:scrollbar_padding] = node[:scrollbar_padding] if node[:scrollbar_padding]
        scroll = Node.new(:scrollbox, scroll_attrs, [content])
        Node.new(:overlay, {}, [scroll,
          Node.new(:overlay, {valign: 'top'}, [header]),
          Node.new(:overlay, {valign: 'bottom'}, [footer])])
      end
    end
  end
end