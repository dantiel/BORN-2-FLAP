# frozen_string_literal: true

require "minitest/autorun"
$LOAD_PATH.unshift File.expand_path("../lib", __dir__)
require "born2flap"

class BrainTest < Minitest::Test
  def test_native_ready_opens_main_menu
    brain = Born2Flap::Brain.new
    brain.command(:native_ready, math_backend: :haskell)
    brain.tick

    assert_equal :main_menu, brain.router.current
    assert_equal :haskell, brain.view_model[:backend]
  end

  def test_navigation_and_back
    brain = Born2Flap::Brain.new
    brain.command(:native_ready, math_backend: :haskell)
    brain.command(:navigate, to: :race)
    brain.tick
    assert_equal :race, brain.router.current

    brain.command(:back)
    brain.tick
    assert_equal :main_menu, brain.router.current
  end

  def test_unknown_routes_are_rejected
    brain = Born2Flap::Brain.new
    brain.command(:navigate, to: :secret)
    assert_raises(Born2Flap::Router::RouteError) { brain.tick }
  end

  def test_plugins_require_capabilities
    registry = Born2Flap::PluginRegistry.new
    assert_raises(Born2Flap::PluginRegistry::PermissionError) do
      registry.register(id: "unsafe", plugin: Object.new, capabilities: [:process_spawn])
    end
  end
end
