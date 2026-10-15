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
  def test_f7_collapses_all_panels_and_removes_siblings_in_reverse_order
    ui=Born2Flap::UI
    visible=ui::Dashboard.build({hud:true,radio_visible:true,settings:true,rc:true,poi:true,menu:true})
    hidden=ui::Dashboard.build({ui_hidden:true,settings:true,rc:true,menu:true})
    assert_empty hidden.children
    assert_equal false,hidden[:visible]
    patch=ui::Reconciler.new.diff(visible,hidden)
    removals=patch.select { |op| op[:op]==:remove_child }.map { |op| op[:index] }
    assert_equal (0...visible.children.length).to_a.reverse,removals
    assert_empty ui::Reconciler.new.diff(hidden,ui::Dashboard.build({ui_hidden:true,speed:999}))
  end

  def test_transport_rotation_does_not_duplicate_subscribers
    require 'stringio'
    brain=Born2Flap::Brain.new
    first=StringIO.new
    transport=Born2Flap::UI::Transport.new(bus:brain.bus,io:first)
    second=StringIO.new
    transport.replace_io(second)
    first.close
    brain.command('ui.ack',id:4)
    transport.flush
    assert_equal 1,JSON.parse(second.string).length
  end

  def test_sub_display_flight_noise_does_not_invalidate_widgets
    ui=Born2Flap::UI
    a=ui::Views::FlightHud.render(:cockpit,altitude:1.200001,speed:2.300001,climb:0.000001,battery:99.99999,throttle:0.0000001)
    b=ui::Views::FlightHud.render(:cockpit,altitude:1.200002,speed:2.300002,climb:0.000002,battery:99.99998,throttle:0.0000002)
    assert_empty ui::Reconciler.new.diff(a,b)
  end

  def test_standalone_brain_emits_nothing_extra_for_unchanged_hidden_snapshot
    require 'tmpdir'
    require 'open3'
    require 'rbconfig'
    Dir.mktmpdir('b2f-ui-idle') do |dir|
      input=File.join(dir,'telemetry.json')
      File.write(input,JSON.generate(frontend:false,ui_hidden:true,hud:false))
      results=[1,12].map do |ticks|
        output=File.join(dir,"frames-#{ticks}.ndjson")
        _,error,status=Open3.capture3(RbConfig.ruby,File.expand_path('../bin/umghaml_brain',__dir__),input,output,'--ticks',ticks.to_s,'--interval','0.001')
        assert status.success?,error
        File.read(output)
      end
      assert_equal results[0],results[1]
    end
  end

end
