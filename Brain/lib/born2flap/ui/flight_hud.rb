# frozen_string_literal: true

module Born2Flap
  module UI
    # Backward-compatible facade for the glass cockpit view controller.
    #
    # `FlightHud.tree` / `FlightHud.build` are thin delegates to
    # `Views::FlightHud` (the real controller lives in views/flight_hud/).
    # Prefer `Views::FlightHud.render(:cockpit, telemetry)` in new code.
    module FlightHud
      def self.tree
        Views::FlightHud.render(:cockpit)
      end

      def self.build(telemetry = {})
        Views::FlightHud.render(:cockpit, telemetry)
      end
    end
  end
end
