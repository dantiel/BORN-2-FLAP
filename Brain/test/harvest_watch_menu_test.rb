require 'minitest/autorun'
require_relative '../lib/born2flap'
require_relative '../lib/born2flap/ui/session'

class HarvestWatchMenuTest < Minitest::Test
  def actions(world)
    Born2Flap::UI::Views::Menu.render(:panel, worlds: [{ id: world }]).each.map { |n| n[:action] }.compact
  end

  def test_shiomori_offers_both_free_flight_and_harvest_watch
    assert_includes actions('Shiomori'), 'menu.shiomori'
    assert_includes actions('Shiomori'), 'menu.harvestwatch'
  end

  def test_other_worlds_do_not_offer_harvest_watch
    %w[Training Ravenstonefield NeonAbyss].each { |world| refute_includes actions(world), 'menu.harvestwatch' }
  end

  def test_activity_button_reaches_native_action_handler_once
    brain = Born2Flap::Brain.new
    session = Born2Flap::UI::Session.new(brain)
    commands = []
    brain.bus.subscribe(:command) { |command| commands << command }
    event = { worlds: [{ id: 'Shiomori' }], ui_events: [{ id: 1, action: 'menu.harvestwatch', value: 0 }] }
    3.times { session.update(event); brain.bus.drain }
    assert_equal 1, commands.count { |command| command[:name] == 'ui.action' }
  end
end
