# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # F2 channel readout overlay (was native C++ AHUD DrawChannels): the five
      # live control channels as a compact stat list. The template owns the
      # loop; this controller only supplies the channel data.
      class Channels < ViewController
        view :panel, template: "channels.umghaml"

        def panel_locals(locals = {})
          { rc_channels: locals[:rc_channels] || [] }
        end
      end
    end
  end
end
