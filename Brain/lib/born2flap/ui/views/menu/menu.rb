# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # The post-splash main menu / level selector, authored semantically in
      # UMGHAML (was native C++ ABorn2FlapMenu). Telemetry supplies the world
      # catalog; the view renders the selected world's card + weather/time
      # selects + FLY/PREV/NEXT and (when opened in-level) RESUME/FLIGHT DESK.
      class Menu < ViewController
        view :panel, template: "menu.umghaml", theme: "menu.umgss"

        def panel_locals(locals = {})
          worlds = locals[:worlds] || []
          index = (locals[:world_index] || 0).to_i
          world = worlds[index] || worlds.first || {}
          weathers = world[:weathers] || ["clear"]
          times = world[:times] || ["day"]
          world_id = (world[:id] || "world").to_s

          {
            world_id: world_id,
            page: locals.fetch(:menu_page, 0).to_i,
            artwork: world_id.downcase == "shiomori" ? "Coast" : "Valley",
            world_title: world[:title] || world_id,
            world_story: world[:story] || "",
            world_position: index + 1,
            world_count: [worlds.size, 1].max,
            weather_index: weathers.index(world[:weather]) || 0,
            weathers: weathers,
            time_index: times.index(world[:time]) || 0,
            times: times,
            # Wind config (per-world). The C++ host publishes the *effective*
            # value (explicit override when >= 0, else the weather/time profile
            # default) plus a flag that reveals the reset button only once tuned.
            wind_dir: (world[:wind_dir] || 0).to_f,
            wind_speed: (world[:wind_speed] || 1).to_f,
            wind_gust: (world[:wind_gust] || 1).to_f,
            wind_random: (world[:wind_random] || 1).to_f,
            wind_thermal: (world[:wind_thermal] || 1).to_f,
            wind_custom: world[:wind_custom] ? true : false,
            menu_inlevel: locals[:menu_inlevel] ? true : false
          }
        end
      end
    end
  end
end