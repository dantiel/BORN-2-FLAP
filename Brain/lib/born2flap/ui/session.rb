# frozen_string_literal: true

module Born2Flap
  module UI
    # UI navigation and presets belong to Ruby. Native commands apply simulation
    # changes; acknowledged input events survive slow frames without replaying.
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
          if telemetry[:frontend]
            case action
            when 'menu.settings' then @state[:settings] = true; @state[:rc] = false
            when 'menu.controls' then @state[:settings] = false; @state[:rc] = true
            when 'editor.close', 'settings.close', 'rc.close' then @state[:settings] = @state[:rc] = false
            end
          end
          case action
          when 'menu.page' then @state[:menu_page] = value.to_i.clamp(0, 1)
          when 'settings.page' then @state[:settings_page] = value.to_i.clamp(0, 4)
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
          else
            apply(action, value, event[:text].to_s)
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
