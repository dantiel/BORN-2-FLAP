# frozen_string_literal: true

module Born2Flap
  module UI
    # Neutral widget-tree node: the single source of truth for the polymorphic
    # UI. Produced by the UMGHAML parser, consumed by every renderer (UMG,
    # HTML, React Native). Carries no target-specific rendering.
    class Node
      attr_accessor :type, :attrs, :children

      def initialize(type, attrs = {}, children = [])
        @type = type.to_s.downcase.to_sym
        @attrs = attrs
        @children = children
      end

      def <<(child)
        @children << child
        self
      end

      def [](key)
        @attrs[key.to_sym]
      end

      # Depth-first traversal; yields each node (including self).
      def each(&block)
        return enum_for(__method__) unless block_given?

        yield self
        @children.each { |child| child.each(&block) }
      end

      def find(type)
        target = type.to_s.downcase.to_sym
        each { |node| return node if node.type == target }
        nil
      end

      def to_h
        {
          type: @type,
          attrs: @attrs,
          children: @children.map(&:to_h)
        }
      end

      def inspect
        "#<UI::Node #{@type} #{@attrs.inspect} children=#{@children.length}>"
      end
    end
  end
end