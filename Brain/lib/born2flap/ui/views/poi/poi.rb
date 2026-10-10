# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # F8 POI-select overlay (was native C++ ABorn2FlapPoiOverlay). Lists the
      # current level's launch points; selecting one teleports (poi.<index>).
      # The template owns the loop; this controller only sanitizes the names.
      class Poi < ViewController
        view :panel, template: "poi.umghaml"

        def panel_locals(locals = {})
          pois = (locals[:pois] || []).map do |p|
            { name: (p[:name] || "POI").to_s.gsub('"', "'"), selected: p[:selected] }
          end
          { pois: pois }
        end
      end
    end
  end
end
