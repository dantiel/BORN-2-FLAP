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

      # Screens owned by the Brain's Router that show the menu (or a menu-like
      # front-end) instead of the in-flight HUD.
      MENU_SCREENS = %i[boot main_menu hangar settings quit].freeze

      def fit(tree, width, height)
        Node.new(:scalebox, {}, [Node.new(:sizebox, { width: width, height: height }, [tree])])
      end

      def build(telemetry, screen: :free_flight)
        telemetry = {} unless telemetry.is_a?(Hash)
        children = []

        if MENU_SCREENS.include?(screen.to_sym)
          # Front-end: only the menu (over the world backdrop). The in-game
          # cockpit/radio are *not* composed here — they are flight screens.
          if telemetry[:settings]
            children << fit(Views::TuningEditor.render(:editor, telemetry), 1050, 780)
          elsif telemetry[:rc]
            children << fit(Views::Rc.render(:panel, telemetry), 950, 850)
          else
            children << fit(Views::Menu.render(:panel, telemetry), 1420, 800)
          end
        else
          # In-flight: persistent HUD layer (cockpit + radio) plus whichever
          # modal overlays are open. F7 (hud=false) collapses cockpit + radio.
          children << Views::FlightHud.render(:cockpit, telemetry) if telemetry[:hud] != false
          children << Views::Radio.render(:panel, telemetry) if telemetry[:radio_visible] != false && telemetry[:hud] != false
          children << Views::Channels.render(:panel, telemetry) if telemetry[:channels] && telemetry[:hud] != false
          children << Views::Rc.render(:panel, telemetry) if telemetry[:rc]
          children << Views::Poi.render(:panel, telemetry) if telemetry[:poi]
          children << fit(Views::TuningEditor.render(:editor, telemetry),1050,780) if telemetry[:settings]
          children << fit(Views::Menu.render(:panel, telemetry),1420,800) if telemetry[:menu]
        end

        children << Views::Splash.render(:panel, telemetry) if telemetry[:splash]
        Node.new(:overlay, { align: "left", valign: "top" }, children)
      end
    end
  end
end