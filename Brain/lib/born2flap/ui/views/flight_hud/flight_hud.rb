# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # The glass cockpit — the semantic *authoring* source of the flight HUD.
      #
      # This is the same screen ABorn2FlapFlightHUD builds natively in C++ (see
      # Unreal/Born2Flap/Source/Born2Flap/UI/Born2FlapFlightHUD.cpp). The Brain
      # owns it here as a view controller: `cockpit.umghaml` is the structure,
      # `cockpit.umgss` the presentation tokens (resolved by Born2FlapUiTheme.h).
      # One controller, many faces + several endpoints:
      #
      #   render(:cockpit)  — the full glass cockpit
      #   render(:minimal)  — a bare altitude strip (no theme)
      class FlightHud < ViewController
        view :cockpit, template: "cockpit.umghaml", theme: "cockpit.umgss"
        view :minimal, template: "minimal.umghaml"

        # Ruby sugar: resolve locals for each endpoint, with defaults so
        # `render` works without telemetry (the static "ready" HUD).
        def cockpit_locals(locals = {})
          telemetry(locals)
        end

        def minimal_locals(locals = {})
          telemetry(locals)
        end

        private

        def telemetry(locals)
          climb = locals[:climb] || 0
          battery = locals[:battery] || 100
          {
            altitude: locals[:altitude] || 0,
            climb: climb,
            climb_tone: climb > 0.4 ? "good" : (climb < -0.4 ? "warn" : "normal"),
            speed: locals[:speed] || 0,
            battery: battery,
            battery_tone: battery > 50 ? "good" : (battery > 20 ? "warn" : "danger"),
            # Native HUD computes "4.18V × 3 = 12.5V"; the Brain accepts an
            # authored readout and falls back to the plain percent.
            battery_readout: locals[:battery_readout] || "#{battery.round}%",
            throttle: (locals[:throttle] || 0) * 100,
            status: locals[:status] || I18n.t("hud.ready", lang: locals[:lang])
          }
        end
      end
    end
  end
end