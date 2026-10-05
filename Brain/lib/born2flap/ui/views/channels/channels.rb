# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # F2 channel readout overlay (was native C++ AHUD DrawChannels): the five
      # live control channels as a compact stat list.
      class Channels < ViewController
        view :panel, template: "channels.umghaml"

        def panel_locals(locals = {})
          { channel_rows: channel_rows(locals[:rc_channels] || []) }
        end

        private

        def channel_rows(channels)
          channels.map do |c|
            name = (c[:name] || "?").to_s
            value = (c[:value] || 0.0).to_f.round(2)
            %(      %Stat{ label: "#{name}", value: #{value}, tone: "info" })
          end.join("\n")
        end
      end
    end
  end
end
