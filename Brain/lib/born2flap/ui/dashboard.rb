# frozen_string_literal: true

module Born2Flap
  module UI
    # Composes the live cockpit + radio base layer with whichever modal
    # overlays are open (menu / poi / settings / rc / channels / splash) into a
    # single neutral tree for the reconciler. Pure function of telemetry — the
    # Brain is stateless; C++ telemetry is the single source of truth and every
    # user action is routed back to C++ game logic, then reflected here on the
    # next tick.
    module Dashboard
      module_function

      def build(telemetry)
        telemetry = {} unless telemetry.is_a?(Hash)
        children = []
        children << Views::FlightHud.render(:cockpit, telemetry)
        children << Views::Radio.render(:panel, telemetry) if telemetry[:radio_visible] != false
        children << Views::Channels.render(:panel, telemetry) if telemetry[:channels]
        children << Views::Rc.render(:panel, telemetry) if telemetry[:rc]
        children << Views::Poi.render(:panel, telemetry) if telemetry[:poi]
        children << Views::TuningEditor.render(:editor, {}) if telemetry[:settings]
        children << Views::Menu.render(:panel, telemetry) if telemetry[:menu]
        children << Views::Splash.render(:panel, telemetry) if telemetry[:splash]
        Node.new(:overlay, { align: "left", valign: "top" }, children)
      end
    end
  end
end