# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # The in-game hangar / tuning editor — the *semantic* authoring source for
      # the configurator. Same screen the native C++ editor will eventually mount
      # (see Born2FlapFlightSettings.cpp, which today is Slate).
      #
      # Everything is SEMANTICS ONLY: a control says what it *means* (value,
      # min/max/step/unit, options, action key) — never how it looks. Labels are
      # translation keys resolved through Born2Flap::I18n, so the host
      # (Born2FlapUiTheme + Born2FlapUIRenderer) resolves the glass look and the
      # locale resolves the words.
      #
      # The 12 knobs are data, not markup: the controller expands them into
      # UMGHAML row fragments (`slider_rows` / `slider_number_rows`) and injects
      # them into `tuning_editor.umghaml` via `#{...}`. One grammar, many faces
      # (UMG / HTML / React Native), many languages.
      class TuningEditor < ViewController
        # A tuning knob's semantic descriptor (mirrors FTuningRow in C++).
        # `action` doubles as the i18n label key (e.g. "tuning.servo_speed").
        Field = Struct.new(:action, :unit, :min, :max, :step, :value,
                           keyword_init: true)

        # Canonical English strings retained for backward-compat facades; the
        # translated options are derived from the key lists below.
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

        view :editor, template: "tuning_editor.umghaml", theme: "tuning_editor.umgss"

        # The 12 live firmware knobs (one-to-one with ETuningField in C++).
        def self.fields(values)
          {
            servoSpeed:      Field.new(action: "tuning.servo_speed",      unit: "°/s",        min: 100,  max: 2400, step: 10,   value: values[:servoSpeed]      || 1200),
            stallTorque:     Field.new(action: "tuning.stall_torque",     unit: "N·m",        min: 0.5,  max: 20,   step: 0.5, value: values[:stallTorque]     || 8),
            backdrive:       Field.new(action: "tuning.backdrive",        unit: "°/s per N·m", min: 0,    max: 100,  step: 1,   value: values[:backdrive]       || 40),
            batteryVoltage:  Field.new(action: "tuning.battery_voltage",  unit: "V",          min: 3.7,  max: 22.2, step: 0.1, value: values[:batteryVoltage]  || 11.1),
            batteryResist:   Field.new(action: "tuning.battery_resistance", unit: "Ω",        min: 0.01, max: 0.5,  step: 0.01, value: values[:batteryResist]   || 0.08),
            batteryCapacity: Field.new(action: "tuning.battery_capacity", unit: "Ah",         min: 0.1,  max: 5,    step: 0.1, value: values[:batteryCapacity] || 2.2),
            flapBaseFreq:    Field.new(action: "tuning.flap_base_freq",   unit: "dHz",        min: 10,   max: 200,  step: 1,   value: values[:flapBaseFreq]    || 60),
            tailElevatorAngle:  Field.new(action: "tuning.tail_elevator_angle", unit: "°",          min: -15,  max: 15,   step: 1,   value: values[:tailElevatorAngle]  || 0),
            glideAngle:      Field.new(action: "tuning.glide_angle",      unit: "°",          min: -15,  max: 15,   step: 1,   value: values[:glideAngle]      || 0),
            strokeFerocity:  Field.new(action: "tuning.stroke_ferocity",  unit: "%",          min: 0,    max: 100,  step: 1,   value: values[:strokeFerocity]  || 50),
            aileronScale:    Field.new(action: "tuning.aileron_scale",    unit: "%",          min: 0,    max: 100,  step: 1,   value: values[:aileronScale]    || 100),
            elevatorScale:   Field.new(action: "tuning.elevator_scale",   unit: "%",          min: 0,    max: 100,  step: 1,   value: values[:elevatorScale]   || 100)
          }.values
        end

        # Ruby sugar: resolve locals for the :editor endpoint.
        def editor_locals(locals = {})
          values = locals[:values] || {}
          profile = locals[:profile] || "CRUISE"
          lang = locals[:lang] || I18n::DEFAULT
          f = self.class.fields(values)

          {
            lang: lang,
            bird_model: locals[:bird_model] || 0,
            profile_index: FLIGHT_PROFILES.index(profile) || 0,
            bird_options: BIRD_KEYS.map { |k| I18n.t(k, lang: lang) },
            profile_options: FLIGHT_PROFILES.map { |p| I18n.t(PROFILE_KEYS[p], lang: lang) },
            servo_rows: slider_number_rows(f, %w[servo_speed stall_torque backdrive], lang),
            battery_rows: slider_rows(f, %w[battery_voltage battery_resistance battery_capacity], lang),
            geometry_rows: slider_rows(f, %w[tail_elevator_angle glide_angle flap_base_freq], lang),
            authority_rows: slider_rows(f, %w[stroke_ferocity aileron_scale elevator_scale], lang)
          }
        end

        # A row with both a slider and a precise number box.
        def slider_number_rows(fields, names, lang)
          select(fields, names).map do |row|
            [
              %(  %Field{ label: "#{I18n.t(row.action, lang: lang)}" }),
              %(    %Slider{ value: #{row.value}, min: #{row.min}, max: #{row.max}, step: #{row.step}, unit: "#{row.unit}", action: "#{row.action}" }),
              %(    %Number{ value: #{row.value}, min: #{row.min}, max: #{row.max}, step: #{row.step}, unit: "#{row.unit}", action: "#{row.action}" })
            ]
          end.flatten.join("\n")
        end

        # A slider-only row.
        def slider_rows(fields, names, lang)
          select(fields, names).map do |row|
            [
              %(  %Field{ label: "#{I18n.t(row.action, lang: lang)}" }),
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