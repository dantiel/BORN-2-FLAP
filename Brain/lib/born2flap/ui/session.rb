# frozen_string_literal: true

module Born2Flap
  module UI
    # UI navigation and presets belong to Ruby. Native commands apply simulation
    # changes; acknowledged input events survive slow frames without replaying.
    #
    # The menu's top-level page (OPEN WORLDS / FLIGHT DESK / APP SETTINGS / RC)
    # is owned by C++ (MenuNavPage → telemetry `nav_page`), so it survives the
    # menu closing. This session owns only transient sub-tab state and routes
    # actions back to the game core.
    class Session
      attr_reader :state

      def initialize(brain)
        @brain = brain
        @last_event = 0
        @state = { menu_page: 0, settings_page: 0, profile: 'CUSTOM' }
      end

      def update(telemetry)
        Array(telemetry[:ui_events]).each do |event|
          id = event.fetch(:id).to_i
          next if id <= @last_event
          action = event.fetch(:action).to_s
          value = event.fetch(:value, 0).to_f

          case action
          when 'ui.back'
            apply('rc.close', 0) if telemetry[:rc]
            apply('editor.close', 0) if telemetry[:settings]
            apply('menu.resume', 0) if telemetry[:menu] && !telemetry[:frontend]
          when 'editor.close', 'settings.close', 'rc.close', 'preferences.close'
            # Inside the menu these buttons return to the OPEN WORLDS page;
            # outside it they close their standalone panel (F8 flight desk / RC).
            if telemetry[:menu] || telemetry[:frontend]
              apply('menu.open_worlds', 0)
            else
              apply(action, 0)
            end
          when 'preferences.page'
            # 0 GENERAL / 1 AUDIO / 2 CONTROL / 3 KEY BINDINGS. Forwarded to C++
            # (PrefsPage) so the page survives panel close; the session state
            # mirrors it for the immediate visual update.
            @state[:preferences_page] = value.to_i.clamp(0, 3)
            apply('preferences.page', value)
          # Tab 3 is the SCENARIOS tab — it exists only when the current world
          # has scenarios (C++ clamps per world), so the session must at least
          # admit it or the tab snaps back to LAUNCH POINT.
          when 'menu.page' then @state[:menu_page] = value.to_i.clamp(0, 3)
          when 'settings.page'
            # 0 CRAFT / 1 BIRD / 2 SERVO / 3 BATTERY / 4 FIRMWARE. Forwarded to
            # C++ (SettingsPage) so the desk restores the same tab next open;
            # the session state mirrors it for the immediate visual update.
            # (This used to clamp at 4 and swallow the event — SERVO/BATTERY/
            # FIRMWARE snaps were broken: clicking those tabs rendered SERVO.)
            @state[:settings_page] = value.to_i.clamp(0, 4)
            apply('settings.page', value)
          when 'servo.preset'
            presets = [[1500,0.17,6.0],[667,0.56,8.4],[750,0.74,7.4],[1000,0.41,7.4],[750,0.35,7.4],[750,0.54,7.4],[545,3.43,7.4]]
            speed, torque, voltage = presets.fetch(value.to_i.clamp(0,presets.size-1))
            {'ServoSpeed'=>speed,'StallTorque'=>torque,'Backdrive'=>20,'BatteryVoltage'=>voltage}.each { |key,v| apply(key,v) }
          when 'profile.select'
            presets = [
              { 'control.expo' => 65, 'flight.safety' => 2, 'StrokeFerocity' => 50 },
              { 'control.expo' => 75, 'flight.safety' => 2, 'StrokeFerocity' => 30 },
              { 'control.expo' => 35, 'flight.safety' => 1, 'StrokeFerocity' => 85 },
              { 'control.expo' => 85, 'flight.safety' => 2, 'StrokeFerocity' => 20 }
            ]
            index = value.to_i.clamp(0, presets.size - 1)
            presets[index].each { |key, v| apply(key, v) }
            @state[:profile] = %w[CRUISE THERMAL AEROBATIC CALM][index]
          when 'menu.allWorlds'
            # World switches reset the overview to the LOCATION tab (C++ also
            # resets MenuPage; the session state must follow or the stale tab
            # would override the fresh telemetry).
            @state[:menu_page] = 0
            apply(action, value, event[:text].to_s)
          when /\Amenu\.pick\./
            @state[:menu_page] = 0
            apply(action, value, event[:text].to_s)
          else
            world = Array(telemetry[:worlds]).find { |item| action == "menu.#{item[:id].to_s.downcase}" }
            if world
              @brain.command('world.load', level: world[:id])
            else
              apply(action, value, event[:text].to_s)
            end
          end
          @last_event = id
          @brain.command('ui.ack', id: id)
        end
        telemetry.merge(@state)
      end

      private

      def apply(action, value, text = '')
        @brain.command('ui.action', action: action, value: value, text: text)
      end
    end
  end
end