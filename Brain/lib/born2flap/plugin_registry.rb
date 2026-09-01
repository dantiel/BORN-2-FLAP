# frozen_string_literal: true

require "set"

module Born2Flap
  class PluginRegistry
    PermissionError = Class.new(StandardError)
    DuplicatePluginError = Class.new(StandardError)

    CORE_CAPABILITIES = Set.new(%i[
      menu_read menu_navigate race_events race_rules vehicle_read
      vehicle_configure telemetry_read hud_publish storage_plugin
    ]).freeze

    Entry = Struct.new(:id, :plugin, :capabilities)

    def initialize(allowed_capabilities: CORE_CAPABILITIES)
      @allowed_capabilities = Set.new(allowed_capabilities)
      @entries = {}
    end

    def register(id:, plugin:, capabilities:)
      key = id.to_s.freeze
      raise DuplicatePluginError, key if @entries.key?(key)

      requested = Set.new(capabilities.map(&:to_sym))
      denied = requested - @allowed_capabilities
      raise PermissionError, denied.to_a.join(", ") unless denied.empty?

      @entries[key] = Entry.new(key, plugin, requested.freeze).freeze
    end

    def each_with(capability)
      required = capability.to_sym
      return enum_for(__method__, required) unless block_given?

      @entries.each_value do |entry|
        yield entry.plugin if entry.capabilities.include?(required)
      end
    end

    def ids
      @entries.keys.sort.freeze
    end
  end
end
