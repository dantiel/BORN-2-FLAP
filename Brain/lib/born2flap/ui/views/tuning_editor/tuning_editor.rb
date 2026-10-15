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

        BIRD_KEYS = %w[bird.ravencrow bird.prototype bird.peregrine bird.turtle].freeze

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

        def editor_locals(locals = {})
          lang=locals.fetch(:lang,I18n::DEFAULT)
          rows=if locals[:tuning_fields]
            locals[:tuning_fields].map do |r|
              snake = r[:key].to_s.gsub(/([a-z\d])([A-Z])/, '\1_\2').downcase
              r.merge(action: "tuning.display.#{r[:key]}", label: I18n.t("tuning.#{snake}", lang: lang))
            end
          else
            self.class.fields(self.class.values_from_telemetry(locals)).map do |r|
              r.to_h.merge(label:I18n.t(r.action,lang:lang),action:r.action.split('.').last.split('_').map(&:capitalize).join,decimals:2,help:'')
            end
          end
          groups=locals[:tuning_fields] ? [[I18n.t('tuning.live_firmware',lang:lang),rows]] :
            %w[hangar.servo hangar.battery hangar.geometry hangar.authority].each_with_index.map { |key,i| [I18n.t(key,lang:lang),rows.slice(i*3,3)] }
          { page:locals.fetch(:settings_page,0).to_i.clamp(0,4),tabs:TABS.map { |k| I18n.t("tab.#{k.downcase}", lang: lang) },
            birds:BIRD_KEYS.map { |k| I18n.t(k,lang:lang) },
            profiles:FLIGHT_PROFILES.map { |k| I18n.t(PROFILE_KEYS[k],lang:lang) },
            profile_index:FLIGHT_PROFILES.index(locals[:profile]) || 0,
            bird_model:locals.fetch(:bird_model,0), camera_fpv:locals.fetch(:camera_fpv,false),
            fpv_angle:locals.fetch(:fpv_angle,0),body_mass_kg:locals.fetch(:body_mass_kg,0.45),
            cg_offset_mm:locals.fetch(:cg_offset_mm,0),coupled_throttle:locals.fetch(:coupled_throttle,true),
            freelook_invert:locals.fetch(:freelook_invert,false),mouse_gains:locals.fetch(:mouse_gains,[1,-1,1]),
            control_expo:locals.fetch(:control_expo,0.65)*100,speed_modifier:locals.fetch(:speed_modifier,0.5),
            flight_safety:locals.fetch(:flight_safety,1),replay_spirits:locals.fetch(:replay_spirits,true),
            wingbeat_volume:locals.fetch(:wingbeat_volume,1),radio_volume:locals.fetch(:radio_volume,0.5),
            groups:groups,frontend:locals.fetch(:frontend,false) }
        end
      end
    end
  end
end