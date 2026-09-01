# frozen_string_literal: true

module Born2Flap
  class Brain
    ROUTES = %i[boot main_menu free_flight race hangar settings quit].freeze

    attr_reader :bus, :router, :plugins, :view_model

    def initialize
      @bus = EventBus.new
      @router = Router.new(routes: ROUTES, initial: :boot)
      @plugins = PluginRegistry.new
      @view_model = { screen: :boot, backend: :unknown, message: "Starting" }
      bind_core_events
    end

    def tick
      @bus.drain
      @view_model = @view_model.merge(screen: @router.current).freeze
    end

    def command(name, payload = {})
      @bus.publish(name, payload)
    end

    private

    def bind_core_events
      @bus.subscribe(:native_ready) do |payload|
        @view_model = @view_model.merge(
          backend: payload.fetch(:math_backend, :unavailable),
          message: "Ready"
        )
        @router.navigate(:main_menu)
      end

      @bus.subscribe(:navigate) do |payload|
        @router.navigate(payload.fetch(:to))
      end

      @bus.subscribe(:back) { @router.back }

      @bus.subscribe(:stall_started) do |payload|
        @plugins.each_with(:hud_publish) do |plugin|
          plugin.on_hud_message("Partial stall at element #{payload[:element]}")
        end
      end
    end
  end
end
