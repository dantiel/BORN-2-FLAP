# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # The in-game hangar / tuning editor — the *semantic* authoring source for
      # the configurator. Same screen the native C++ editor mirrors (see
      # Born2FlapFlightSettings.cpp). Everything is SEMANTICS ONLY: a control
      # says what it *means* (value, min/max/step/unit, options, action key) —
      # never how it looks.
      #
      # The panel is now split into five workbench pages:
      #   0 CRAFT     — bird silhouette, flight profile, camera mode
      #   1 BIRD      — further bird tuning (FPV tilt, wing twist, coupled
      #                 throttle, mouse speed)
      #   2 CONTROLS  — mouse response (roll/pitch/yaw gains), control expo
      #   3 ASSIST    — flight-safety reset + replay shadow-doppelgängers
      #   4 TUNING    — the twelve live firmware knobs (servo/battery/geometry/
      #                 authority)
      #
      # The whole tree is generated in `build_source` (a properly indented
      # UMGHAML string) and interpolated through the one-line template, so the
      # indentation nesting is exact and never drifts.
      class TuningEditor < ViewController
        # A tuning knob's semantic descriptor (mirrors born2flap::tuning::FRow).
        # `action` doubles as the i18n label key (e.g. "tuning.servo_speed").
        Field = Struct.new(:action, :unit, :min, :max, :step, :value,
                           keyword_init: true)

        BIRD_MODELS = [
          "RAVENCROW  /  folded obsidian & comb pinions",
          "PROTOTYPE  /  elliptical feathers",
          "PEREGRINE  /  falcon"
        ].freeze

        FLIGHT_PROFILES = %w[CRUISE THERMAL AEROBATIC CALM].freeze

        BIRD_KEYS = %w[bird.ravencrow bird.prototype bird.peregrine].freeze
        PROFILE_KEYS = {
          "CRUISE" => "profile.cruise",
          "THERMAL" => "profile.thermal",
          "AEROBATIC" => "profile.aerobatic",
          "CALM" => "profile.calm"
        }.freeze

        TABS = ["CRAFT", "BIRD", "CONTROLS", "ASSIST", "TUNING"].freeze

        MOUSE_NAMES = ["ROLL  /  mouse X", "PITCH  /  mouse Y", "YAW  /  mouse X"].freeze

        view :editor, template: "tuning_editor.umghaml", theme: "tuning_editor.umgss"

        # The 12 live firmware knobs (one-to-one with ETuningField in C++).
        def self.fields(values)
          {
            servoSpeed:      Field.new(action: "tuning.servo_speed",        unit: "°/s",        min: 100,  max: 2400, step: 10,   value: values[:servoSpeed]      || 1200),
            stallTorque:     Field.new(action: "tuning.stall_torque",       unit: "N·m",        min: 0.5,  max: 20,   step: 0.5, value: values[:stallTorque]     || 8),
            backdrive:       Field.new(action: "tuning.backdrive",          unit: "°/s per N·m", min: 0,    max: 100,  step: 1,   value: values[:backdrive]       || 40),
            batteryVoltage:  Field.new(action: "tuning.battery_voltage",    unit: "V",          min: 3.7,  max: 22.2, step: 0.1, value: values[:batteryVoltage]  || 11.1),
            batteryResist:   Field.new(action: "tuning.battery_resistance", unit: "Ω",          min: 0.01, max: 0.5,  step: 0.01, value: values[:batteryResist]   || 0.08),
            batteryCapacity: Field.new(action: "tuning.battery_capacity",   unit: "Ah",         min: 0.1,  max: 5,    step: 0.1, value: values[:batteryCapacity] || 2.2),
            flapBaseFreq:    Field.new(action: "tuning.flap_base_freq",     unit: "dHz",        min: 10,   max: 200,  step: 1,   value: values[:flapBaseFreq]    || 60),
            tailElevatorAngle: Field.new(action: "tuning.tail_elevator_angle", unit: "°",       min: -15,  max: 15,   step: 1,   value: values[:tailElevatorAngle] || 0),
            glideAngle:      Field.new(action: "tuning.glide_angle",        unit: "°",          min: -15,  max: 15,   step: 1,   value: values[:glideAngle]      || 0),
            strokeFerocity:  Field.new(action: "tuning.stroke_ferocity",    unit: "%",          min: 0,    max: 100,  step: 1,   value: values[:strokeFerocity]  || 50),
            aileronScale:    Field.new(action: "tuning.aileron_scale",      unit: "%",          min: 0,    max: 100,  step: 1,   value: values[:aileronScale]    || 100),
            elevatorScale:   Field.new(action: "tuning.elevator_scale",     unit: "%",          min: 0,    max: 100,  step: 1,   value: values[:elevatorScale]   || 100)
          }.values
        end

        # C++ telemetry emits tuning values under born2flap::tuning::Key
        # (PascalCase). Map the camelCase knob keys onto those.
        TUNING_TELEMETRY = {
          servoSpeed: :ServoSpeed, stallTorque: :StallTorque, backdrive: :Backdrive,
          batteryVoltage: :BatteryVoltage, batteryResist: :BatteryResistance,
          batteryCapacity: :BatteryCapacity, flapBaseFreq: :FlapBaseFreq,
          tailElevatorAngle: :TailElevatorAngle, glideAngle: :GlideAngle,
          strokeFerocity: :StrokeFerocity, aileronScale: :AileronScale,
          elevatorScale: :ElevatorScale
        }.freeze

        def self.values_from_telemetry(locals)
          TUNING_TELEMETRY.each_with_object({}) do |(knob, telemetry_key), out|
            out[knob] = (locals[telemetry_key] || 0).to_f
          end
        end

        # Ruby sugar: resolve the single interpolated local the one-line
        # template consumes.
        def editor_locals(locals = {})
          page = (locals[:settings_page] || 0).to_i
          page = [[page, 0].max, TABS.length - 1].min
          { tree_source: build_source(locals, page) }
        end

        private

        def build_source(locals, page)
          lang = (locals[:lang] || I18n::DEFAULT).to_s
          fields = self.class.fields(self.class.values_from_telemetry(locals))
          out = []
          out << %(%Overlay{ align: "center", valign: "center" })
          out << %(  %Panel{ title: "FLIGHT DESK", padding: 24 })
          out << %(    %Segment{ options: #{TABS.inspect}, value: #{page}, action: "settings.page", tooltip: "Choose a workbench page." })
          out << %(    %Banner{ text: "#{I18n.t('hangar.title', lang: lang)}", tone: "accent" })

          # 0 — CRAFT
          out << section(page, 0, I18n.t('hangar.silhouette', lang: lang))
          out << field(I18n.t('hangar.bird', lang: lang))
          out << %(        %Select{ value: #{intv(locals[:bird_model])}, options: #{bird_options(lang)}, action: "bird.select" })
          out << field(I18n.t('hangar.flight_profile', lang: lang))
          out << %(        %Segment{ value: #{profile_index(locals)}, options: #{profile_options(lang)}, action: "profile.select" })
          out << toggle("AIRBORNE CAMERA", locals[:camera_fpv], "camera.fpv", "FPV / click for chase", "CHASE / click for FPV")

          # 1 — BIRD (further bird tuning)
          out << section(page, 1, "BIRD TUNING")
          out << slider("FPV CAMERA ANGLE", locals[:fpv_angle], -45, 45, 1, "°", "camera.angle")
          out << toggle("WING TWIST AILERONS", locals[:roll_twist], "bird.rolltwist", "ON / click to disable", "OFF / click to enable")
          out << toggle("COUPLED THROTTLE", locals[:coupled_throttle], "bird.coupled", "ON / click to decouple", "OFF / click to couple")

          # 2 — CONTROLS
          out << section(page, 2, "CONTROLS")
          3.times { |axis| out << slider(MOUSE_NAMES[axis], gain(locals, axis), -2, 2, 0.05, "×", "mouse.gain.#{axis}") }
          out << button("Reset mouse response", "mouse.reset")
          out << slider("CONTROL EXPO", ((locals[:control_expo] || 0.65).to_f * 100.0), 0, 100, 1, "%", "control.expo")
          out << slider("MOUSE SPEED", locals[:speed_modifier], 0, 1, 0.05, "×", "mouse.speed")

          # 3 — ASSIST
          out << section(page, 3, "ASSIST")
          out << slider("FLIGHT SAFETY RESET", locals[:flight_safety], 0, 2, 1, "", "flight.safety")
          out << toggle("REPLAY SHADOWS", locals[:replay_spirits], "replay.spirits", "ON / click to hide", "OFF / click to show")
          out << button("Delete all replay shadows", "replay.delete", "danger")

          # 4 — TUNING (12 firmware knobs in four subsections)
          out << section(page, 4, I18n.t('hangar.servo', lang: lang))
          each_row(fields, %w[servo_speed stall_torque backdrive]) { |r| out << tuning_slider(r, lang) }
          out << section(page, 4, I18n.t('hangar.battery', lang: lang))
          each_row(fields, %w[battery_voltage battery_resistance battery_capacity]) { |r| out << tuning_slider(r, lang) }
          out << section(page, 4, I18n.t('hangar.geometry', lang: lang))
          each_row(fields, %w[tail_elevator_angle glide_angle flap_base_freq]) { |r| out << tuning_slider(r, lang) }
          out << section(page, 4, I18n.t('hangar.authority', lang: lang))
          each_row(fields, %w[stroke_ferocity aileron_scale elevator_scale]) { |r| out << tuning_slider(r, lang) }

          out << %(    %Button{ label: "#{I18n.t('hangar.save', lang: lang)}", action: "editor.close", tone: "good" })
          out.join("\n")
        end

        def section(page, idx, title)
          %(    %Section{ title: "#{title}", open: true, visible: #{page == idx} })
        end

        def field(label)
          %(      %Field{ label: "#{label}" })
        end

        def slider(label, value, min, max, step, unit, action)
          [field(label),
           %(        %Slider{ value: #{num(value)}, min: #{num(min)}, max: #{num(max)}, step: #{num(step)}, unit: "#{unit}", action: "#{action}" })].join("\n")
        end

        def toggle(label, value, action, on_label, off_label)
          [field(label),
           %(        %Toggle{ value: #{value ? "true" : "false"}, action: "#{action}", on: "#{on_label}", off: "#{off_label}" })].join("\n")
        end

        def button(label, action, tone = nil)
          tone_attr = tone ? %(, tone: "#{tone}") : ""
          %(      %Button{ label: "#{label}", action: "#{action}"#{tone_attr} })
        end

        # A tuning slider emits the PascalCase C++ action key (matching
        # born2flap::tuning::Key) while its label is the i18n key.
        def tuning_slider(row, lang)
          slider(I18n.t(row.action, lang: lang), row.value, row.min, row.max, row.step, row.unit, ckey(row.action))
        end

        def ckey(action)
          action.split(".").last.split("_").map(&:capitalize).join
        end

        def each_row(fields, names)
          fields.select { |r| names.any? { |n| r.action.end_with?(n) } }.each { |r| yield r }
        end

        def gain(locals, axis)
          g = locals[:mouse_gains]
          return (axis == 1 ? -1.0 : 1.0) unless g.is_a?(Array) && g[axis]
          g[axis].to_f
        end

        def bird_options(lang)
          BIRD_KEYS.map { |k| I18n.t(k, lang: lang) }.inspect
        end

        def profile_index(locals)
          FLIGHT_PROFILES.index(locals[:profile]) || 0
        end

        def profile_options(lang)
          FLIGHT_PROFILES.map { |p| I18n.t(PROFILE_KEYS[p], lang: lang) }.inspect
        end

        def intv(v)
          (v || 0).to_i
        end

        def num(v)
          v.nil? ? 0 : v
        end
      end
    end
  end
end