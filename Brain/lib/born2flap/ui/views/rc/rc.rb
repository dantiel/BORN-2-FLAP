# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # F3 control-settings panel — now a three-tab transmitter workbench:
      #
      #   0 STICKS   — two graphical gimbals (left = throttle/yaw, right =
      #                pitch/roll, Mode 2) + a live channel monitor
      #   1 DEVICE   — transmitter selection, enable/disable, calibration
      #                wizard, launch/reset-button learning
      #   2 MAPPING  — per-channel axis assignment, invert, axis learn
      #
      # This controller only formats telemetry into ready-to-render rows; the
      # template owns the loops/conditionals. The gimbals are driven by the
      # five *mapped* channels (rc_channels) — GAS is 0..1, ROLL/PITCH/YAW/
      # SPEED are centred -1..1 — so the stick mirrors what the bird actually
      # flies, not the raw 0..1 hardware axes.
      class Rc < ViewController
        view :panel, template: "rc.umghaml"

        TABS = %w[tab.sticks tab.device tab.mapping].freeze

        def panel_locals(locals = {})
          lang = locals[:lang] || I18n::DEFAULT

          channels = (locals[:rc_channels] || []).map { |c| c.is_a?(Hash) ? c : {} }
          by_name = channels.each_with_object({}) { |c, h| h[c[:name].to_s] = c[:value].to_f }

          gas   = by_name.fetch("GAS", 0.0)
          roll  = by_name.fetch("ROLL", 0.0)
          pitch = by_name.fetch("PITCH", 0.0)
          yaw   = by_name.fetch("YAW", 0.0)
          speed = by_name.fetch("SPEED", 0.0)

          {
            page: (locals[:rc_page] || 0).to_i.clamp(0, 2),
            tabs: TABS.map { |k| I18n.t(k, lang: lang) },
            left_stick:  { x: clamp1(yaw), y: clamp1(gas * 2.0 - 1.0), title: I18n.t("rc.left_stick", lang: lang),  vert: I18n.t("rc.throttle", lang: lang), horiz: I18n.t("rc.yaw", lang: lang) },
            right_stick: { x: clamp1(roll), y: clamp1(pitch),       title: I18n.t("rc.right_stick", lang: lang), vert: I18n.t("rc.pitch", lang: lang),   horiz: I18n.t("rc.roll", lang: lang) },
            channel_rows: channel_rows(gas, roll, pitch, yaw, speed, lang),
            rc_device: (locals[:rc_device] || "").to_s,
            rc_status: (locals[:rc_status] || "").to_s,
            rc_instruction: (locals[:rc_instruction] || "").to_s,
            rc_notice: (locals[:rc_notice] || "").to_s,
            wizard_active: (locals[:rc_stage] || -1).to_i >= 0,
            wizard_step: (locals[:rc_stage] || -1).to_i + 1,
            rc_enabled: locals[:rc_enabled] ? true : false,
            rc_buttons: (locals[:rc_buttons] || "").to_s,
            rc_channel_axes: locals[:rc_channel_axes] || [],
            rc_mappings: (locals[:rc_mappings] || []).map(&:to_s)
          }
        end

        private

        def clamp1(v)
          v.to_f.clamp(-1.0, 1.0)
        end

        # Normalise a centred -1..1 channel into a 0..1 progress-bar fraction.
        def centred(v)
          ((v.to_f.clamp(-1.0, 1.0) + 1.0) * 0.5).clamp(0.0, 1.0)
        end

        def signed_pct(v)
          pct = (v.to_f * 100.0).round
          pct.zero? ? "0%" : format("%+d%%", pct)
        end

        def channel_rows(gas, roll, pitch, yaw, speed, lang)
          [
            { label: I18n.t("rc.throttle", lang: lang), value: gas.clamp(0.0, 1.0), readout: "#{(gas.clamp(0.0, 1.0) * 100.0).round}%", tone: "accent" },
            { label: I18n.t("rc.roll", lang: lang),     value: centred(roll),      readout: signed_pct(roll),  tone: "info" },
            { label: I18n.t("rc.pitch", lang: lang),    value: centred(pitch),     readout: signed_pct(pitch), tone: "info" },
            { label: I18n.t("rc.yaw", lang: lang),      value: centred(yaw),       readout: signed_pct(yaw),   tone: "info" },
            { label: I18n.t("rc.speed", lang: lang),    value: centred(speed),     readout: signed_pct(speed), tone: "warn" }
          ]
        end
      end
    end
  end
end