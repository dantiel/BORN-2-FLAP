# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # The OPEN WORLDS menu page / level selector, authored semantically in
      # UMGHAML (was native C++ ABorn2FlapMenu). Telemetry supplies the world
      # catalog; the view renders the selected world's card + weather/time
      # selects + FLY/PREV/NEXT. The sidebar is owned by Dashboard so the nav
      # rail stays identical across the other menu pages.
      class Menu < ViewController
        view :panel, template: "menu.umghaml", theme: "menu.umgss"

        def panel_locals(locals = {})
          lang = locals[:lang] || I18n::DEFAULT
          worlds = locals[:worlds] || []
          index = (locals[:world_index] || 0).to_i
          world = worlds[index] || worlds.first || {}
          weathers = world[:weathers] || ["clear"]
          times = world[:times] || ["day"]
          world_id = (world[:id] || "world").to_s
          wind_dir = (world[:wind_dir] || 0).to_f
          wind_speed = (world[:wind_speed] || 1).to_f

          {
            launch_points: world[:launch_points] || [],
            launch_index: world[:launch_index] || 0,
            world_id: world_id,
            page: locals.fetch(:menu_page, 0).to_i,
            artwork: case world_id.downcase
                     when "shiomori" then "Coast"
                     when "ravenstonefield" then "Valley"
                     when "neonabyss" then "Abyss"
                     else "Islands"
                     end,
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
            wind_dir: wind_dir,
            wind_speed: wind_speed,
            wind_gust: (world[:wind_gust] || 1).to_f,
            wind_random: (world[:wind_random] || 1).to_f,
            wind_thermal: (world[:wind_thermal] || 1).to_f,
            wind_custom: world[:wind_custom] ? true : false,
            # Word hints shown next to the value readout of each wind slider.
            wind_dir_hint: compass_hint(wind_dir, lang),
            wind_speed_hint: wind_strength_hint(wind_speed, lang)
          }
        end

        private

        # Map a compass heading (degrees, any sign) to an 8-sector name.
        def compass_hint(deg, lang)
          names = %w[dir.north dir.north_east dir.east dir.south_east
                     dir.south dir.south_west dir.west dir.north_west]
          sector = (((deg.to_f % 360.0) + 360.0) % 360.0 / 45.0).round % 8
          I18n.t(names[sector], lang: lang)
        end

        # Map the wind strength multiplier (0..5×) to a plain-language level.
        def wind_strength_hint(mult, lang)
          m = mult.to_f
          key = if m <= 0.1 then 'wind.still'
                elsif m <= 0.5 then 'wind.calm'
                elsif m <= 1.0 then 'wind.breeze'
                elsif m <= 1.5 then 'wind.fresh_breeze'
                elsif m <= 2.0 then 'wind.moderate_wind'
                elsif m <= 2.5 then 'wind.strong'
                elsif m <= 3.0 then 'wind.high_wind'
                elsif m <= 4.0 then 'wind.very_strong'
                else 'wind.gale'
                end
          I18n.t(key, lang: lang)
        end
      end
    end
  end
end
