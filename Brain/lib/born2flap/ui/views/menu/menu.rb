# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # The post-splash main menu / level selector, authored semantically in
      # UMGHAML (was native C++ ABorn2FlapMenu). Telemetry supplies the world
      # catalog; the view renders the selected world's card + weather/time
      # selects + FLY/PREV/NEXT and (when opened in-level) RESUME/FLIGHT DESK.
      class Menu < ViewController
        view :panel, template: "menu.umghaml"

        def panel_locals(locals = {})
          worlds = locals[:worlds] || []
          index = (locals[:world_index] || 0).to_i
          world = worlds[index] || worlds.first || {}
          weathers = world[:weathers] || ["clear"]
          times = world[:times] || ["day"]
          in_level = locals[:menu_inlevel] ? true : false
          world_id = (world[:id] || "world").to_s

          {
            world_id: world_id,
            page: locals.fetch(:menu_page, 0).to_i,
            artwork: world_id.downcase == "shiomori" ? "Coast" : "Valley",
            world_title: world[:title] || world_id,
            world_story: world[:story] || "",
            weather_index: weathers.index(world[:weather]) || 0,
            weathers: weathers,
            time_index: times.index(world[:time]) || 0,
            times: times,
            action_buttons: action_buttons(in_level)
          }
        end

        private

        def action_buttons(in_level)
          return "" unless in_level

          [
            %(      %Button{ label: "RESUME", action: "menu.resume", tone: "good" }),
            %(      %Button{ label: "FLIGHT DESK", action: "menu.settings" })
          ].join("\n")
        end
      end
    end
  end
end
