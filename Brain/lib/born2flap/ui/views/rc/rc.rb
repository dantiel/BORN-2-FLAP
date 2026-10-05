# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # F3 RC-sender panel (was native C++ FBorn2FlapRcController + AHUD
      # DrawRcPanel): device, status, instruction, per-channel readout, buttons.
      class Rc < ViewController
        view :panel, template: "rc.umghaml"

        def panel_locals(locals = {})
          {
            rc_device: (locals[:rc_device] || "Kein Sender ausgewaehlt").to_s,
            rc_status: (locals[:rc_status] || "").to_s,
            rc_instruction: (locals[:rc_instruction] || "").to_s,
            rc_buttons: (locals[:rc_buttons] || "").to_s,
            channel_rows: channel_rows(locals[:rc_channels] || [])
          }
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
