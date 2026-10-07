# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # F3 control-settings panel (was native C++ FBorn2FlapRcController +
      # AHUD DrawRcPanel): device selection, calibration wizard, live axes,
      # channel mapping and transmitter-button learning — authored semantically
      # in UMGHAML. Every control emits an action routed through
      # HandleBrainAction -> FBorn2FlapRcController, so game logic stays in C++.
      class Rc < ViewController
        view :panel, template: "rc.umghaml"

        def panel_locals(locals = {})
          { tree_source: build_source(locals) }
        end

        private

        def build_source(locals)
          out = []
          out << %(%Overlay{ align: "center", valign: "center" })
          out << %(  %SizeBox{ width: 880 })
          out << %(    %Panel{ title: "CONTROL SETTINGS", padding: 24 })
          out << %(      %VBox{ spacing: 8 })
          out << %(        %Banner{ text: "RC TRANSMITTER", tone: "accent" })
          out << text(locals[:rc_device], "info", "m")
          out << text(locals[:rc_status], "dim", "s")
          out << text(locals[:rc_instruction], "accent", "s")
          notice = (locals[:rc_notice] || "").to_s
          out << text(notice, "accent", "s") unless notice.empty?
          out << %(        %Field{ label: "DEVICE" })
          out << %(        %HBox{ spacing: 8 })
          out << %(          %Button{ label: "NEXT DEVICE", action: "rc.device.next" })
          out << %(          %Button{ label: "#{enable_label(locals)}", action: "rc.enable.toggle", tone: "dim" })
          out << %(        %Field{ label: "CALIBRATION" })
          out << %(        %HBox{ spacing: 8 })
          out << %(          %Button{ label: "START", action: "rc.calibrate.start", tone: "good" })
          out << %(          %Button{ label: "NEXT STEP", action: "rc.calibrate.advance" })
          out << %(          %Button{ label: "CANCEL", action: "rc.calibrate.cancel", tone: "danger" })
          out << %(        %HBox{ spacing: 8 })
          out << %(          %Button{ label: "LEARN START", action: "rc.learn.launch", tone: "dim" })
          out << %(          %Button{ label: "LEARN RESET", action: "rc.learn.reset", tone: "dim" })
          out << %(        %Field{ label: "LIVE AXES" })
          (locals[:rc_axes] || []).each_with_index { |a, i| out << axis_row(a, i) }
          out << %(        %Field{ label: "CHANNEL MAPPING" })
          (locals[:rc_mappings] || []).each { |m| out << text(m.to_s, "dim", "s") }
          out << text(locals[:rc_buttons], "dim", "xs")
          out << %(        %Button{ label: "CLOSE", action: "rc.close", tone: "good" })
          out.join("\n")
        end

        def text(s, tone, size)
          %(        %TextBlock{ text: "#{(s || "").to_s}", tone: "#{tone}", size: "#{size}" })
        end

        def enable_label(locals)
          locals[:rc_enabled] ? "DISABLE RC" : "ENABLE RC"
        end

        def axis_row(a, i)
          value = (a.is_a?(Hash) ? (a[:value] || 0.0) : 0.0).to_f
          available = a.is_a?(Hash) && a[:available]
          pct = (value * 100.0).round
          label = available ? "#{i + 1}: #{pct}%" : "#{i + 1}: --"
          tone = available ? "info" : "dim"
          %(        %Stat{ label: "#{label}", value: #{value.round(2)}, tone: "#{tone}" })
        end
      end
    end
  end
end
