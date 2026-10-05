# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # Startup splash (was native C++ ABorn2FlapSplash): loading bar + percent.
      # Telemetry drives splash_progress (0..1) and splash_opacity (0..1).
      class Splash < ViewController
        view :panel, template: "splash.umghaml"

        def panel_locals(locals = {})
          progress = (locals[:splash_progress] || 0.0).to_f
          {
            splash_progress: progress,
            splash_pct: (progress * 100.0).round
          }
        end
      end
    end
  end
end
