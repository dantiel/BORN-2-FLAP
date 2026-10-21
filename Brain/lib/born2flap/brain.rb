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
      @booted = false
      bind_core_events
    end

    def tick(telemetry = {})
      @bus.drain
      react(telemetry) if telemetry && !telemetry.empty?
      @view_model = @view_model.merge(screen: @router.current).freeze
    end

    # Imperative command to the game core (Unreal). Publishes :command, which
    # Transport serializes into the NDJSON frame the C++ host applies. This is
    # the orchestrator's outbound channel — everything Ruby decides the core
    # should DO travels through here (non-time-critical only).
    def command(name, payload = {})
      @bus.publish(:command, name: name.to_s, payload: payload)
    end

    # Internal event publication (Brain-local wiring + plugin communication),
    # distinct from the outbound command channel above.
    def publish(event_name, payload = {})
      @bus.publish(event_name, payload)
    end

    private

    # Called each tick with the latest telemetry (the native core's sensory
    # feed). The first real telemetry means the core is up: emit a one-time
    # boot command so the game core logs the orchestrator's presence. Later
    # phases map telemetry → state-machine transitions and issue flow commands.
    def react(telemetry)
      unless @booted
        @booted = true
        command("boot", ruby: RUBY_VERSION, phase: @router.current.to_s)
      end
      # While the C++ level-loading screen is up, hold at :boot so nothing renders
      # over the movie player (belt-and-braces on top of Dashboard's loading guard).
      if telemetry[:loading]
        @router.navigate(:boot, remember: false) unless @router.current == :boot
        return
      end
      # Native lifecycle is authoritative across travel and direct-level boots.
      # Repeated telemetry must not grow navigation history every frame.
      target = telemetry[:frontend] || (telemetry[:menu] && !telemetry[:menu_inlevel]) ? :main_menu : :free_flight
      @router.navigate(target, remember: false) unless @router.current == target
    end

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