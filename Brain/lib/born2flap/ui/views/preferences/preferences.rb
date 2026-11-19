# frozen_string_literal: true
module Born2Flap
  module UI
    module Views
      class Preferences < ViewController
        view :panel, template: 'preferences.umghaml', theme: 'preferences.umgss'
        def panel_locals(locals = {})
          base = { page: 0, graphics_quality: 0, lang: 'en', camera_fpv: false, fpv_angle: 0,
            freelook_invert: false, wingbeat_volume: 1, radio_volume: 0.5, keybindings: [],
            display_mode: 1, display_follow_system: true, display_resolution: 0,
            avatar_enabled: true, avatar_person: 0, trick_toasts: true,
            # CONTROL tab (input shaping + flight helpers, moved from the desk).
            mouse_gains: [1, -1, 1], control_expo: 65, speed_modifier: 0.5,
            flight_safety: 1, replay_spirits: true, frontend: false,
            # AUDIO tab: on-foot pilot footsteps (0..1).
            footstep_volume: 1
          }.merge(locals).merge(page: locals.fetch(:preferences_page,0))
          # Telemetry publishes expo as 0..1; the slider speaks 0..100 %.
          base[:control_expo] = base[:control_expo].to_f * 100
          # Group the flat binding list by its C++ category so the KEY BINDINGS
          # tab can fold each group independently (Section widgets).
          base[:keybind_groups] = keybind_groups(base[:keybindings])
          base
        end

        private

        # Collapse the flat binding list into ordered groups keyed by C++ category
        # (FLIGHT / CAMERA / UI / DEBUG / AUDIO), preserving catalogue display order.
        def keybind_groups(bindings)
          groups = {}
          Array(bindings).each do |binding|
            category = binding[:category].to_s
            category = 'BINDINGS' if category.empty?
            (groups[category] ||= []) << binding
          end
          groups.map { |title, rows| { title: title, bindings: rows } }
        end
      end
    end
  end
end