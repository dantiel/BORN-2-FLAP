# frozen_string_literal: true

require_relative "ui/node"
require_relative "ui/haml_parser"
require_relative "ui/reconciler"
require_relative "ui/root"
require_relative "ui/host_config"
require_relative "ui/effects"
require_relative "ui/effect_driver"
require_relative "ui/emitter"
require_relative "ui/wire"
require_relative "ui/transport"
require_relative "ui/template"
require_relative "ui/umgss"
require_relative "ui/view_controller"
require_relative "ui/views/flight_hud/flight_hud"
require_relative "ui/views/tuning_editor/tuning_editor"
require_relative "ui/views/menu/menu"
require_relative "ui/views/poi/poi"
require_relative "ui/views/radio/radio"
require_relative "ui/views/splash/splash"
require_relative "ui/views/rc/rc"
require_relative "ui/views/channels/channels"
require_relative "ui/dashboard"
require_relative "ui/flight_hud"
require_relative "ui/tuning_editor"

module Born2Flap
  # Polymorphes UI (UMGHAML): eine Grammatik, ein neutraler Baum, viele
  # Renderer. Wird später als eigenständiges Gem `umghaml` extrahiert.
  module UI
    # Namespace for the per-directory view controllers (one controller owns one
    # `views/<name>/` directory of `.umghaml` templates + `.umgss` themes).
    module Views; end

    # Absolute filesystem root of the `views/` tree.
    def self.view_root
      File.expand_path("ui/views", __dir__)
    end
  end
end
