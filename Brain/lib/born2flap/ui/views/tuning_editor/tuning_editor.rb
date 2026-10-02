# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # The in-game hangar / tuning editor — the *semantic* authoring source for
      # the configurator. Same screen the native C++ editor will eventually mount
      # (see Born2FlapFlightSettings.cpp, which today is Slate).
      #
      # Everything is SEMANTICS ONLY: a control says what it *means* (label,
      # value, min/max/step/unit, options, action key) — never how it looks. The
      # host (Born2FlapUiTheme + Born2FlapUIRenderer) resolves the glass look.
      #
      # The 12 knobs are data, not markup: the controller expands them into
      # UMGHAML row fragments (`slider_rows` / `slider_number_rows`) and injects
      # them into `tuning_editor.umghaml` via `#{...}`. One grammar, many faces
      # (UMG / HTML / React Native).
      class TuningEditor < ViewController
        # A tuning knob's semantic descriptor (mirrors FTuningRow in C++).
        Field = Struct.new(:action, :label, :unit, :min, :max, :step, :value,
                           keyword_init: true)

        BIRD_MODELS = [
          "RAVENCROW  /  folded obsidian & comb pinions",
          "PROTOTYPE  /  elliptical feathers",
          "PEREGRINE  /  falcon"
        ].freeze

        FLIGHT_PROFILES = %w[CRUISE THERMAL AEROBATIC CALM].freeze

        view :editor, template: "tuning_editor.umghaml", theme: "tuning_editor.umgss"

        # The 12 live firmware knobs (one-to-one with ETuningField in C++).
        def self.fields(values)
          {
            servoSpeed:     Field.new(action: "tuning.servo_speed",     label: "SERVO SPEED",       unit: "°/s",        min: 100,  max: 2400, step: 10,   value: values[:servoSpeed]     || 1200),
            stallTorque:    Field.new(action: "tuning.stall_torque",    label: "STALL TORQUE",      unit: "N·m",        min: 0.5,  max: 20,   step: 0.5, value: values[:stallTorque]    || 8),
            backdrive:      Field.new(action: "tuning.backdrive",       label: "BACKDRIVE",         unit: "°/s per N·m", min: 0,    max: 100,  step: 1,   value: values[:backdrive]      || 40),
            batteryVoltage: Field.new(action: "tuning.battery_voltage", label: "BATTERY VOLTAGE",   unit: "V",          min: 3.7,  max: 22.2, step: 0.1, value: values[:batteryVoltage] || 11.1),
            batteryResist:  Field.new(action: "tuning.battery_resist",  label: "BATTERY RESISTANCE", unit: "Ω",         min: 0.01, max: 0.5,  step: 0.01, value: values[:batteryResist]  || 0.08),
            batteryCapacity: Field.new(action: "tuning.battery_capacity", label: "BATTERY CAPACITY", unit: "Ah",       min: 0.1,  max: 5,    step: 0.1, value: values[:batteryCapacity] || 2.2),
            flapBaseFreq:   Field.new(action: "tuning.flap_base_freq",  label: "FLAP FREQ CEILING", unit: "dHz",       min: 10,   max: 200,  step: 1,   value: values[:flapBaseFreq]   || 60),
            mountAngle:     Field.new(action: "tuning.mount_angle",     label: "MOUNT ANGLE",       unit: "°",          min: -15,  max: 15,   step: 1,   value: values[:mountAngle]     || 0),
            glideAngle:     Field.new(action: "tuning.glide_angle",     label: "GLIDE ANGLE",       unit: "°",          min: -15,  max: 15,   step: 1,   value: values[:glideAngle]     || 0),
            strokeFerocity: Field.new(action: "tuning.stroke_ferocity", label: "STROKE FEROCITY",   unit: "%",          min: 0,    max: 100,  step: 1,   value: values[:strokeFerocity] || 50),
            aileronScale:   Field.new(action: "tuning.aileron_scale",   label: "AILERON SCALE",     unit: "%",          min: 0,    max: 100,  step: 1,   value: values[:aileronScale]   || 100),
            elevatorScale:  Field.new(action: "tuning.elevator_scale",  label: "ELEVATOR SCALE",    unit: "%",          min: 0,    max: 100,  step: 1,   value: values[:elevatorScale]  || 100)
          }.values
        end

        # Ruby sugar: resolve locals for the :editor endpoint.
        def editor_locals(locals = {})
          values = locals[:values] || {}
          profile = locals[:profile] || "CRUISE"
          f = self.class.fields(values)

          {
            bird_model: locals[:bird_model] || 0,
            profile_index: FLIGHT_PROFILES.index(profile) || 0,
            bird_options: BIRD_MODELS,
            profile_options: FLIGHT_PROFILES,
            servo_rows: slider_number_rows(f, %w[servo_speed stall_torque backdrive]),
            battery_rows: slider_rows(f, %w[battery_voltage battery_resist battery_capacity]),
            geometry_rows: slider_rows(f, %w[mount_angle glide_angle flap_base_freq]),
            authority_rows: slider_rows(f, %w[stroke_ferocity aileron_scale elevator_scale])
          }
        end

        # A row with both a slider and a precise number box.
        def slider_number_rows(fields, names)
          select(fields, names).map do |row|
            [
              %(  %Field{ label: "#{row.label}" }),
              %(    %Slider{ value: #{row.value}, min: #{row.min}, max: #{row.max}, step: #{row.step}, unit: "#{row.unit}", action: "#{row.action}" }),
              %(    %Number{ value: #{row.value}, min: #{row.min}, max: #{row.max}, step: #{row.step}, unit: "#{row.unit}", action: "#{row.action}" })
            ]
          end.flatten.join("\n")
        end

        # A slider-only row.
        def slider_rows(fields, names)
          select(fields, names).map do |row|
            [
              %(  %Field{ label: "#{row.label}" }),
              %(    %Slider{ value: #{row.value}, min: #{row.min}, max: #{row.max}, step: #{row.step}, unit: "#{row.unit}", action: "#{row.action}" })
            ]
          end.flatten.join("\n")
        end

        private

        def select(fields, names)
          fields.select { |row| names.any? { |n| row.action.end_with?(n) } }
        end
      end
    end
  end
end
