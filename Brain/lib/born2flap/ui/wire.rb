# frozen_string_literal: true

require "json"

module Born2Flap
  module UI
    # The JSON wire format — the bridge between the Ruby Brain and the C++/UMG
    # host. Without mruby embedding, the Brain runs standalone and the op streams
    # cross the process boundary as JSON (pipe/file/socket). This module is the
    # single serializer for that boundary.
    #
    # Two streams share one schema (an array of op objects):
    #   HostConfig.translate(patch)  → :ui_patch   (discrete tree commits)
    #   EffectDriver.drive(telemetry) → :ui_effect  (continuous material params)
    #
    # Wire format (path = array of child indices from root, root itself `[]`):
    #   {"op":"create_instance","path":[0],"node":{"type":"Overlay","props":{...},"children":[...]}}
    #   {"op":"remove_instance","path":[0]}
    #   {"op":"append_child","path":[0],"index":1,"node":{...}}
    #   {"op":"remove_child","path":[0],"index":1}
    #   {"op":"update_props","path":[0,0],"props":{"text":"yo"}}
    #   {"op":"set_material_params","path":[0],"params":{"BlurRadius":0.5}}
    module Wire
      module_function

      # Serialize a host-op stream (canonical tuples from HostConfig/EffectDriver)
      # into a JSON array string.
      def dump_ops(ops)
        JSON.generate(ops.map { |op| serialize_op(op) })
      end

      # Serialize a full neutral tree (the initial mount / a debug snapshot).
      def dump_tree(node)
        JSON.generate(serialize_node(node))
      end

      def serialize_op(op)
        case op[0]
        when :create_instance
          { "op" => "create_instance", "path" => op[1], "node" => serialize_node(op[2]) }
        when :remove_instance
          { "op" => "remove_instance", "path" => op[1] }
        when :append_child
          { "op" => "append_child", "path" => op[1], "index" => op[2], "node" => serialize_node(op[3]) }
        when :remove_child
          { "op" => "remove_child", "path" => op[1], "index" => op[2] }
        when :update_props
          { "op" => "update_props", "path" => op[1], "props" => stringify(op[2]) }
        when :set_material_params
          { "op" => "set_material_params", "path" => op[1], "params" => stringify(op[2]) }
        else
          raise ArgumentError, "unknown host op: #{op.inspect}"
        end
      end

      # A Node (or any object responding to type/attrs/children) → JSON hash.
      def serialize_node(node)
        {
          "type" => Emitter.umg_type(node.type),
          "props" => stringify(node.attrs),
          "children" => node.children.map { |child| serialize_node(child) }
        }
      end

      def stringify(hash)
        hash.each_with_object({}) { |(key, value), out| out[key.to_s] = value }
      end
    end
  end
end
