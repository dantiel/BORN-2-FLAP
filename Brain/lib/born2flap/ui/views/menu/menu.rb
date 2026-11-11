# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
            # The OPEN WORLDS menu page, authored semantically in UMGHAML (was native
            # C++ ABorn2FlapMenu). Two stages: the all-maps PICKER (cards with a short
            # overview) and the selected world's OVERVIEW (tabs: location / weather &
            # time / launch point / scenarios + the FLY button). Telemetry supplies
            # the world catalog and the C++ menu state (world_index, world_stage,
            # menu_page); picking a scenario retitles the FLY button to the scenario.
            class Menu < ViewController
              view :panel, template: "menu.umghaml", theme: "menu.umgss"

              def panel_locals(locals = {})
                lang = locals[:lang] || I18n::DEFAULT
                apply_mode = locals[:apply_mode] ? true : false
                worlds = locals[:worlds] || []
                index = (locals[:world_index] || 0).to_i
                world = worlds[index] || worlds.first || {}
                weathers = world[:weathers] || ["clear"]
                times = world[:times] || ["day"]
                seasons = world[:seasons] || ["spring"]
                world_id = (world[:id] || "world").to_s
                wind_dir = (world[:wind_dir] || 0).to_f
                wind_speed = (world[:wind_speed] || 1).to_f

                # Scenario tab: the world's scenario list (empty = no tab) and the
                # current pick — 0 = free flight, 1+ = list index. Each scenario
                # row carries its own explainer text (title + story) so every
                # entry describes what to do. The FLY button takes the picked
                # scenario's title.
                scenarios = world[:scenarios] || []
                scenario_index = (world[:scenario_index] || 0).to_i
                picked = scenario_index.between?(1, scenarios.size) ? scenarios[scenario_index - 1] : nil
                # In-level the menu is the loaded world's parameter desk: the
                # bottom button becomes APPLY CHANGES and the travel-only tabs
                # (launch point / scenarios) are dropped.
                fly_label = if apply_mode
                              I18n.t('menu.apply_changes', lang: lang)
                            elsif picked
                              picked[:title]
                            else
                              "#{I18n.t('menu.play', lang: lang)} #{world[:title] || world_id}"
                            end

                menu_pages = [I18n.t('menu.location', lang: lang),
                              I18n.t('menu.weather_time', lang: lang)]
                unless apply_mode
                  menu_pages << I18n.t('menu.launch_point', lang: lang)
                  menu_pages << I18n.t('menu.scenarios', lang: lang) unless scenarios.empty?
                end

                # Picker cards: every world carries its artwork key and a one-line
                # brief for the all-maps overview stage.
                picker_worlds = worlds.map do |w|
                  wid = (w[:id] || '').to_s
                  w.merge(art: artwork_for(wid), brief: brief_for(w[:story]))
                end

                {
                  worlds: picker_worlds,
                  world_stage: locals.fetch(:world_stage, 0).to_i,
                  launch_points: world[:launch_points] || [],
                  launch_index: world[:launch_index] || 0,
                  world_id: world_id,
                  page: [[locals.fetch(:menu_page, 0).to_i, menu_pages.size - 1].min, 0].max,
                  artwork: artwork_for(world_id),
                  world_title: world[:title] || world_id,
                  world_story: world[:story] || "",
                  world_position: index + 1,
                  world_count: [worlds.size, 1].max,
                  weather_index: weathers.index(world[:weather]) || 0,
                  weathers: weathers,
                  time_index: times.index(world[:time]) || 0,
                  times: times,
                  season_index: seasons.index(world[:season]) || 0,
                  seasons: seasons,
                  # Wind config (per-world). The C++ host publishes the *effective*
                  # value (explicit override when >= 0, else the season's prevailing
                  # wind default) plus a flag that reveals the reset button once tuned.
                  wind_dir: wind_dir,
                  wind_speed: wind_speed,
                  wind_gust: (world[:wind_gust] || 1).to_f,
                  wind_random: (world[:wind_random] || 1).to_f,
                  wind_thermal: (world[:wind_thermal] || 1).to_f,
                  wind_custom: world[:wind_custom] ? true : false,
                  # Word hints shown next to the value readout of each wind slider.
                  wind_dir_hint: compass_hint(wind_dir, lang),
                  wind_speed_hint: wind_strength_hint(wind_speed, lang),
                  # Scenario tab + FLY-button title (follows the pick).
                  scenarios: scenarios,
                  scenario_index: scenario_index,
                  fly_label: fly_label,
                  menu_pages: menu_pages,
                  apply_mode: apply_mode
                }
              end

              private

              # Splash artwork per world (the picker cards and the overview header
              # share the same /Game/UI/Elements/<name> key).
              def artwork_for(world_id)
                case world_id.to_s.downcase
                when "shiomori" then "Coast"
                when "ravenstonefield" then "Valley"
                when "neonabyss" then "Abyss"
                when "yunsha" then "Desert"
                when "woshi" then "Mountains"
                else "Islands"
                end
              end

              # One-line overview for a picker card: the first sentence, trimmed to
              # ~170 characters at a word boundary.
              def brief_for(story)
                text = story.to_s.split(/\.\s+/).first.to_s
                text = text[0, 170]
                cut = text.rindex(' ')
                text = text[0...cut] if cut && cut > 90
                text.strip.empty? ? story.to_s : "#{text}."
              end

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