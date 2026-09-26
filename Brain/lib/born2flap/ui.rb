# frozen_string_literal: true

require_relative "ui/node"
require_relative "ui/haml_parser"
require_relative "ui/reconciler"
require_relative "ui/root"
require_relative "ui/host_config"
require_relative "ui/effects"
require_relative "ui/effect_driver"
require_relative "ui/emitter"

module Born2Flap
  # Polymorphes UI (UMGHAML): eine Grammatik, ein neutraler Baum, viele
  # Renderer. Wird später als eigenständiges Gem `umghaml` extrahiert.
  module UI
  end
end