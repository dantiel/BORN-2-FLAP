# frozen_string_literal: true

module Born2Flap
  module UI
    # React-style reconciliation, done the Ruby way: compare two neutral Node
    # trees and produce a minimal patch (a list of operations) that turns the
    # previous tree into the next one. No Redux, no virtual-DOM engine — just
    # plain immutable-tree diffing.
    #
    # Patch operations:
    #   { op: :update_props, path: [...], props: { name => value|nil } }
    #   { op: :replace,      path: [...], node:  Node | nil }
    #   { op: :insert_child, path: [...], index: Integer, node: Node }
    #   { op: :remove_child, path: [...], index: Integer }
    #
    # `path` is a list of child indices from the root (the root itself is `[]`).
    # A `nil` prop value means "remove this attribute". Children are matched by
    # index; key-based matching (`id`/`key`) is a later refinement.
    class Reconciler
      def diff(previous, current)
        ops = []
        walk(previous, current, [], ops)
        ops
      end

      private

      def walk(old_node, new_node, path, ops)
        return if old_node.nil? && new_node.nil?

        if old_node.nil?
          ops << { op: :replace, path: path, node: new_node }
          return
        end
        if new_node.nil?
          ops << { op: :replace, path: path, node: nil }
          return
        end

        if old_node.type != new_node.type
          ops << { op: :replace, path: path, node: new_node }
          return
        end

        diff_props(old_node, new_node, path, ops)
        diff_children(old_node, new_node, path, ops)
      end

      def diff_props(old_node, new_node, path, ops)
        changed = {}
        (old_node.attrs.keys | new_node.attrs.keys).each do |key|
          next if old_node.attrs[key] == new_node.attrs[key]

          changed[key] = new_node.attrs[key]
        end
        ops << { op: :update_props, path: path, props: changed } unless changed.empty?
      end

      def diff_children(old_node, new_node, path, ops)
        length = [old_node.children.length, new_node.children.length].max
        length.times do |index|
          old_child = old_node.children[index]
          new_child = new_node.children[index]

          if old_child.nil?
            ops << { op: :insert_child, path: path, index: index, node: new_child }
          elsif new_child.nil?
            ops << { op: :remove_child, path: path, index: index }
          else
            walk(old_child, new_child, path + [index], ops)
          end
        end
      end
    end
  end
end
