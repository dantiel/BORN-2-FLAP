# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # F3 control-settings panel (was native C++ FBorn2FlapRcController +
      # AHUD DrawRcPanel): device selection, calibration wizard, live axes,
      # channel mapping and transmitter-button learning — authored semantically
      # in UMGHAML. The template owns the loops/conditionals; this controller
      # only formats the telemetry into ready-to-render rows.
      class Rc < ViewController
        view :panel, template: "rc.umghaml"

        def panel_locals(locals = {})
          {
            rc_device: (locals[:rc_device] || "").to_s,
            rc_status: (locals[:rc_status] || "").to_s,
            rc_instruction: (locals[:rc_instruction] || "").to_s,
            rc_notice: (locals[:rc_notice] || "").to_s,
            rc_enabled: locals[:rc_enabled] ? true : false,
            rc_buttons: (locals[:rc_buttons] || "").to_s,
            rc_axes: axis_rows(locals[:rc_axes] || []),
            rc_mappings: (locals[:rc_mappings] || []).map(&:to_s)
          }
        end

        private

        def axis_rows(axes)
          axes.each_with_index.map do |a, i|
            value = (a.is_a?(Hash) ? (a[:value] || 0.0) : 0.0).to_f
            available = a.is_a?(Hash) && a[:available]
            pct = (value * 100.0).round
            {
              label: available ? "#{i + 1}: #{pct}%" : "#{i + 1}: --",
              value: value.round(2),
              tone: available ? "info" : "dim"
            }
          end
        end
      end
    end
  end
end
