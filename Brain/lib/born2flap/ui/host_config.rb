# frozen_string_literal: true

module Born2Flap
  module UI
    # The native-host mutation contract — the "host" half of react-native-umg.
    #
    # React Native's renderer is defined by a HostConfig: a fixed set of
    # imperative operations the reconciler can rely on, independent of the
    # platform (UIKit, Android Views, …). Here the host is UMG — later Web and
    # React Native. The Reconciler stays host-agnostic: it only produces a flat
    # patch. This module names the canonical host operations and translates the
    # patch into them, so a host (C++/UMG today) implements a stable, tiny
    # interface instead of re-deriving tree semantics.
    #
    #   Reconciler (Ruby, host-agnostic) ──patch──▶ HostConfig ──host ops──▶ UMG/Web/RN
    #
    # Patch op → host mutation:
    #   :replace      + node → create_instance(type, props)   (mount subtree)
    #   :replace      + nil  → remove_instance                 (destroy subtree)
    #   :insert_child        → append_child(parent, index, node)
    #   :remove_child        → remove_child(parent, index)
    #   :update_props        → update_props(instance, changed)
    #
    # `set_material_params` is NOT produced from a tree patch — it is emitted
    # directly by EffectDriver on the continuous `:ui_effect` channel and maps to
    # UMaterialInstanceDynamic::SetScalarParameterValue in the UMG host.
    module HostConfig
      # Canonical host operations every renderer implements.
      HOST_OPS = %i[
        create_instance
        remove_instance
        append_child
        remove_child
        update_props
        set_material_params
      ].freeze

      module_function

      # Translate a reconciler patch into a host-operation stream. Pure and
      # order-preserving: the host consumes it without knowing how the diff was
      # computed. `replace` is split into `create_instance`/`remove_instance`
      # so the host never deals with the ambiguous mount/destroy duality.
      def translate(patch)
        patch.map do |op|
          case op[:op]
          when :replace
            op[:node] ? [:create_instance, op[:path], op[:node]] : [:remove_instance, op[:path]]
          when :insert_child
            [:append_child, op[:path], op[:index], op[:node]]
          when :remove_child
            [:remove_child, op[:path], op[:index]]
          when :update_props
            [:update_props, op[:path], op[:props]]
          end
        end
      end
    end
  end
end