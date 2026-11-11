require 'minitest/autorun'
require_relative '../lib/born2flap'
require_relative '../lib/born2flap/ui/session'

class HarvestWatchMenuTest < Minitest::Test
  SHIOMORI = {
    id: 'Shiomori', title: 'SHIOMORI BAY', scenario_index: 1,
    scenarios: [{ title: 'HARVEST WATCH', story: 'Guard the sweet-potato field.' }]
  }.freeze

  def attrs(worlds, **locals)
    Born2Flap::UI::Views::Menu.render(:panel, worlds: worlds, world_stage: 1, **locals)
      .each.map { |n| n.to_h[:attrs] }
  end

  def test_scenario_tab_lists_every_scenario_with_its_own_explainer
    nodes = attrs([SHIOMORI], menu_page: 3)
    scenario_buttons = nodes.select { |a| a[:action] == 'menu.scenario' }
    # FREE FLIGHT row + one row per scenario.
    assert_equal 2, scenario_buttons.size
    assert_equal 1, scenario_buttons.count { |a| a[:value] == 0 }
    assert_equal 1, scenario_buttons.count { |a| a[:value] == 1 }
    # Every row carries a description of what to do.
    texts = nodes.select { |a| a[:text] }.map { |a| a[:text] }
    assert_includes texts, 'Guard the sweet-potato field.'
    assert texts.any? { |t| t.include?('Free flight') }
  end

  def test_fly_button_follows_the_picked_scenario
    fly = attrs([SHIOMORI], menu_page: 3).find { |a| a[:action] == 'menu.shiomori' }
    assert_equal 'HARVEST WATCH', fly[:label]
  end

  def test_worlds_without_scenarios_have_no_scenario_tab
    %w[Training Ravenstonefield NeonAbyss].each do |world|
      refute attrs([{ id: world }]).any? { |a| a[:action] == 'menu.scenario' }
    end
  end

  def test_scenario_pick_reaches_native_action_handler_once
    brain = Born2Flap::Brain.new
    session = Born2Flap::UI::Session.new(brain)
    commands = []
    brain.bus.subscribe(:command) { |command| commands << command }
    event = { worlds: [{ id: 'Shiomori' }], ui_events: [{ id: 1, action: 'menu.scenario', value: 1 }] }
    3.times { session.update(event); brain.bus.drain }
    assert_equal 1, commands.count { |command| command[:name] == 'ui.action' }
  end

  def test_session_admits_the_scenarios_tab_and_resets_on_world_switch
    brain = Born2Flap::Brain.new
    session = Born2Flap::UI::Session.new(brain)
    tab = { ui_events: [{ id: 1, action: 'menu.page', value: 3 }] }
    assert_equal 3, session.update(tab)[:menu_page]
    pick = { ui_events: [{ id: 2, action: 'menu.pick.shiomori', value: 0 }] }
    assert_equal 0, session.update(pick)[:menu_page]
  end
end
