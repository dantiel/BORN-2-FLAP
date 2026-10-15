# frozen_string_literal: true
module Born2Flap
  module UI
    module Views
      class Preferences < ViewController
        view :panel, template: 'preferences.umghaml', theme: 'preferences.umgss'
        def panel_locals(locals = {})
          { page: 0, graphics_quality: 0, lang: 'en', camera_fpv: false, fpv_angle: 0,
            freelook_invert: false, wingbeat_volume: 1, radio_volume: 0.5, keybindings: []
          }.merge(locals).merge(page: locals.fetch(:preferences_page,0))
        end
      end
    end
  end
end
