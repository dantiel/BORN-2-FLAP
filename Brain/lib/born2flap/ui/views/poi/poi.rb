# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # F8 POI-select overlay (was native C++ ABorn2FlapPoiOverlay). Lists the
      # current level's launch points; selecting one teleports (poi.<index>).
      class Poi < ViewController
        view :panel, template: "poi.umghaml"

        def panel_locals(locals = {})
          pois = locals[:pois] || []
          { rows: poi_rows(pois) }
        end

        private

        def poi_rows(pois)
          pois.each_with_index.map do |p, i|
            name = (p[:name] || "POI").to_s.gsub('"', "'")
            tone = p[:selected] ? "good" : "accent"
            %(      %Button{ label: "#{name}", action: "poi.#{i}", tone: "#{tone}" })
          end.join("\n")
        end
      end
    end
  end
end
