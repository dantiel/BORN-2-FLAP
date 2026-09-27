# frozen_string_literal: true

module Born2Flap
  module UI
    # Drives material effects from physics telemetry — the continuous-value half
    # of react-native-umg, mirroring React Native's split between discrete tree
    # commits (:ui_patch) and the continuous Animated path (:ui_effect).
    #
    # Bind a named effect to a widget path (either imperatively via `bind`, or
    # declaratively via `bind_tree` scanning `effect:` attrs), then each
    # `drive(telemetry)` computes the material params and publishes them as
    # `set_material_params` host ops on the `:ui_effect` channel.
    #
    #   Effects::Telemetry ──► EffectDriver.drive ──► :ui_effect ──► UMG host
    class EffectDriver
      Binding = Struct.new(:effect, :path, keyword_init: true)

      def initialize(bus: nil)
        @bus = bus
        @bindings = []
      end

      # Imperative binding: driver.bind(:speed_blur, path: [0, 0]).
      def bind(effect, path: [])
        name = effect.to_sym
        raise ArgumentError, "unknown effect: #{effect.inspect}" unless Effects.names.include?(name)

        @bindings << Binding.new(effect: name, path: path)
        self
      end

      # Declarative binding: walk a parsed tree and bind every node carrying an
      # `effect:` attr to its index path (root itself is `[]`, matching the
      # Reconciler's path convention).
      def bind_tree(tree)
        walk(tree, []) do |node, path|
          bind(node[:effect], path: path) if node[:effect]
        end
        self
      end

      # Compute material params for every binding and publish them. Returns the
      # host-op stream (nil-safe for headless tests) and, when a bus is wired,
      # publishes as `:ui_effect`.
      def drive(telemetry)
        ops = @bindings.map do |binding|
          [:set_material_params, binding.path, Effects.compute(binding.effect, telemetry)]
        end
        @bus.publish(:ui_effect, ops: ops) if @bus && !ops.empty?
        ops
      end

      def bindings
        @bindings.dup
      end

      private

      def walk(node, path, &block)
        yield node, path
        node.children.each_with_index { |child, i| walk(child, path + [i], &block) }
      end
    end
  end
end
