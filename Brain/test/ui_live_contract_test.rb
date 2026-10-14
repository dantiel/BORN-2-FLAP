require 'minitest/autorun'
require_relative '../lib/born2flap'
require_relative '../lib/born2flap/ui/session'

class UiLiveContractTest < Minitest::Test
  def test_route_tracks_travel_and_modal_lifecycle
    b=Born2Flap::Brain.new
    b.tick(frontend: true, menu: true)
    assert_equal :main_menu,b.router.current
    5.times { b.tick(frontend: false, settings: true) }
    assert_equal :free_flight,b.router.current
    assert_equal [],b.router.history
    tree=Born2Flap::UI::Dashboard.build({settings:true,hud:false},screen:b.router.current)
    assert tree.each.any? { |n| n[:action]=='settings.page' }
    refute tree.each.any? { |n| n[:action]=='menu.nextWorld' }
  end

  def test_input_is_processed_once_and_navigation_is_ruby_owned
    b=Born2Flap::Brain.new
    s=Born2Flap::UI::Session.new(b)
    commands=[]
    b.bus.subscribe(:command) { |c| commands << c }
    t={ui_events:[{id:1,action:'menu.page',value:1},{id:2,action:'bird.select',value:2}]}
    3.times { assert_equal 1,s.update(t)[:menu_page]; b.bus.drain }
    assert_equal 1,commands.count { |c| c[:name]=='ui.action' }
    assert_equal 2,commands.count { |c| c[:name]=='ui.ack' }
  end

  def test_real_templates_have_no_unknown_host_widgets
    ui=Born2Flap::UI
    trees=[ui::Views::Menu.render(:panel),ui::Views::Rc.render(:panel),ui::Views::TuningEditor.render(:editor),ui::Views::FlightHud.render(:cockpit)]
    trees.each { |t| t.each { |n| refute_equal 'CustomWidget',ui::Emitter.umg_type(n.type),n.type.to_s } }
  end

  def test_tuning_uses_native_metadata_and_exact_keys
    t=Born2Flap::UI::Views::TuningEditor.render(:editor,tuning_fields:[{key:'MountAngle',label:'Mount',unit:'°',min:-15,max:15,step:1,value:3,help:'Trim'}])
    slider=t.each.find { |n| n[:action]=='tuning.display.MountAngle' }
    assert_equal 3,slider[:value]
    assert_equal 'Trim',slider[:tooltip]
  end
end
