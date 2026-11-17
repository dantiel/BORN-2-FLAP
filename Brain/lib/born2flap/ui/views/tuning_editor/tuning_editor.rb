# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # The in-game hangar / tuning editor — the *semantic* authoring source for
      # the configurator. Everything is SEMANTICS ONLY: a control says what it
      # *means* (value, min/max/step/unit, options, action key) — never how it
      # looks. The C++ host supplies data (servo catalog + degradation series
      # via BuildBrainTelemetry), widgets (Sparkline) and theme tokens only.
      #
      # Five workbench pages (CONTROLS/ASSIST moved to APP SETTINGS > CONTROL):
      #   0 CRAFT     — bird silhouette, flight profile, camera mode
      #   1 BIRD      — FPV tilt, mass, CG, coupled throttle, freelook invert
      #   2 SERVO     — preset picker + datasheet readout (operating range,
      #                 rated voltage, stall, speed) + the power-degradation
      #                 sparkline + the three servo knobs
      #   3 BATTERY   — pack knobs + live over-voltage warning against the
      #                 selected servo's rating
      #   4 FIRMWARE  — geometry / ferocity / waveform / authority groups
      #
      # SERVO and BATTERY got their own tabs so the firmware knobs never have
      # to be scrolled past (and vice versa).
      class TuningEditor < ViewController
        # A tuning knob's semantic descriptor (mirrors born2flap::tuning::FRow).
        # `action` doubles as the i18n label key (e.g. "tuning.servo_speed").
        Field = Struct.new(:action, :unit, :min, :max, :step, :value,
                           keyword_init: true)

        BIRD_MODELS = [
          "RAVENCROW  /  folded obsidian & comb pinions",
          "PROTOTYPE  /  elliptical feathers",
          "COMMON KESTREL  /  falcon"
        ].freeze

        FLIGHT_PROFILES = %w[CRUISE THERMAL AEROBATIC CALM].freeze

        BIRD_KEYS = %w[bird.ravencrow bird.prototype bird.kestrel bird.turtle bird.kestrel_light bird.pterosaur].freeze

        # Dossier for the selected craft, in BIRD_KEYS order. Mirrors the native
        # C++ flight-desk dossier (name, role, full wingspan, dry mass).
        CRAFT_INFO = [
          { name: "RAVENCROW",      role: "valley night hunter", span: "1.32 m", mass: "86 g" },
          { name: "PROTOTYPE",      role: "training workhorse",  span: "0.99 m", mass: "36 g" },
          { name: "COMMON KESTREL", role: "coast falcon",        span: "1.10 m", mass: "50 g" },
          { name: "NEON TURTLE",    role: "aether swimmer",      span: "1.10 m", mass: "50 g" },
          { name: "KESTREL LIGHT",  role: "light membrane",      span: "1.10 m", mass: "25 g" },
          { name: "PTEROSAUR",      role: "desert glider",       span: "1.10 m", mass: "50 g" }
        ].freeze

        def self.craft_info_text(index)
          c = CRAFT_INFO[index.to_i] || CRAFT_INFO[0]
          "#{c[:name]} · #{c[:role]} · #{c[:span]} span · #{c[:mass]}"
        end

        PROFILE_KEYS = {
          "CRUISE" => "profile.cruise",
          "THERMAL" => "profile.thermal",
          "AEROBATIC" => "profile.aerobatic",
          "CALM" => "profile.calm"
        }.freeze

        # The flight desk keeps the raw-simulation desk only: craft/bird/servo/
        # battery/firmware. CONTROLS and ASSIST moved to the APP SETTINGS
        # CONTROL tab (preferences.umghaml) — input shaping and helpers are
        # user preferences, not airframe tuning.
        TABS = %w[CRAFT BIRD SERVO BATTERY FIRMWARE].freeze
        TAB_KEYS = {
          "CRAFT" => "tab.craft", "BIRD" => "tab.bird", "SERVO" => "tab.servo",
          "BATTERY" => "tab.battery", "FIRMWARE" => "tab.firmware"
        }.freeze

        MOUSE_NAMES = ["ROLL  /  mouse X", "PITCH  /  mouse Y", "YAW  /  mouse X"].freeze

        # Knob groups per page (PascalCase keys shared with the C++ Rows key).
        SERVO_KEYS = %w[ServoSpeed StallTorque Backdrive].freeze
        BATTERY_KEYS = %w[BatteryVoltage BatteryResistance BatteryCapacity BatteryCRating BatteryBurstCRating].freeze
        # Mount geometry belongs to the bird's body (BIRD tab), not the firmware.
        BIRD_GROUPS = [
          ['tuning.group_geometry', %w[FlapBaseFreq TailElevatorAngle GlideAngle FlapCentre VerticalMountAngle]]
        ].freeze
        FIRMWARE_GROUPS = [
          ['tuning.group_ferocity',  %w[StrokeFerocity ReturnFerocity]],
          ['tuning.group_waveform',  %w[StrokeSkew ReturnSkew ShapeMix ThrottleThrustShapeMix ThrottleThrustExpo ThrottleSkewRateMix]],
          ['tuning.group_authority', %w[AileronScale ElevatorScale YawScale AileronSkewMix AileronSkewRateMix ElevatorFerocityMix]]
        ].freeze

        view :editor, template: "tuning_editor.umghaml", theme: "tuning_editor.umgss"

        # Fallback knobs for headless renders (no C++ telemetry). The live game
        # path reads `tuning_fields` from BuildBrainTelemetry instead.
        def self.fields(values)
          {
            servoSpeed:      Field.new(action: "tuning.servo_speed",        unit: "°/s",        min: 100,  max: 2400, step: 10,   value: values[:servoSpeed]      || 857),
            stallTorque:     Field.new(action: "tuning.stall_torque",       unit: "N·m",        min: 0.5,  max: 20,   step: 0.5, value: values[:stallTorque]     || 1.32),
            backdrive:       Field.new(action: "tuning.backdrive",          unit: "°/s per N·m", min: 0,    max: 100,  step: 1,   value: values[:backdrive]       || 20),
            batteryVoltage:  Field.new(action: "tuning.battery_voltage",    unit: "V",          min: 3.7,  max: 22.2, step: 0.1, value: values[:batteryVoltage]  || 8.4),
            batteryResist:   Field.new(action: "tuning.battery_resistance", unit: "Ω",          min: 0.01, max: 0.5,  step: 0.01, value: values[:batteryResist]   || 0.08),
            batteryCapacity: Field.new(action: "tuning.battery_capacity",   unit: "Ah",         min: 0.1,  max: 5,    step: 0.1, value: values[:batteryCapacity] || 1.3),
            flapBaseFreq:    Field.new(action: "tuning.flap_base_freq",     unit: "dHz",        min: 10,   max: 200,  step: 1,   value: values[:flapBaseFreq]    || 32),
            tailElevatorAngle: Field.new(action: "tuning.tail_elevator_angle", unit: "°",       min: -15,  max: 15,   step: 1,   value: values[:tailElevatorAngle] || -2.3),
            glideAngle:      Field.new(action: "tuning.glide_angle",        unit: "°",          min: -15,  max: 15,   step: 1,   value: values[:glideAngle]      || -4),
            strokeFerocity:  Field.new(action: "tuning.stroke_ferocity",    unit: "%",          min: 0,    max: 100,  step: 1,   value: values[:strokeFerocity]  || 50),
            aileronScale:    Field.new(action: "tuning.aileron_scale",      unit: "%",          min: 0,    max: 100,  step: 1,   value: values[:aileronScale]    || 40),
            elevatorScale:   Field.new(action: "tuning.elevator_scale",     unit: "%",          min: 0,    max: 100,  step: 1,   value: values[:elevatorScale]   || 55),
            shapeMix:             Field.new(action: "tuning.shape_mix",                 unit: "%", min: 0,    max: 100,  step: 1, value: values[:shapeMix]              || 0),
            throttleThrustShapeMix: Field.new(action: "tuning.throttle_thrust_shape_mix", unit: "%", min: 0,  max: 100,  step: 1, value: values[:throttleThrustShapeMix] || 0),
            throttleThrustExpo:  Field.new(action: "tuning.throttle_thrust_expo",     unit: "%", min: -100, max: 100,  step: 1, value: values[:throttleThrustExpo]     || 0),
            aileronSkewMix:      Field.new(action: "tuning.aileron_skew_mix",          unit: "%", min: 0,    max: 100,  step: 1, value: values[:aileronSkewMix]        || 0),
            throttleSkewRateMix: Field.new(action: "tuning.throttle_skew_rate_mix",    unit: "%", min: 0,    max: 100,  step: 1, value: values[:throttleSkewRateMix]   || 0),
            aileronSkewRateMix:  Field.new(action: "tuning.aileron_skew_rate_mix",     unit: "%", min: 0,    max: 100,  step: 1, value: values[:aileronSkewRateMix]    || 0),
            elevatorFerocityMix: Field.new(action: "tuning.elevator_ferocity_mix",     unit: "%", min: 0,    max: 100,  step: 1, value: values[:elevatorFerocityMix]   || 0)
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
          elevatorScale: :ElevatorScale,
          shapeMix: :ShapeMix, throttleThrustShapeMix: :ThrottleThrustShapeMix,
          throttleThrustExpo: :ThrottleThrustExpo, aileronSkewMix: :AileronSkewMix,
          throttleSkewRateMix: :ThrottleSkewRateMix, aileronSkewRateMix: :AileronSkewRateMix,
          elevatorFerocityMix: :ElevatorFerocityMix
        }.freeze

        def self.values_from_telemetry(locals)
          TUNING_TELEMETRY.each_with_object({}) do |(knob, telemetry_key), out|
            out[knob] = (locals[telemetry_key] || 0).to_f
          end
        end

        def editor_locals(locals = {})
          lang = locals.fetch(:lang, I18n::DEFAULT)
          rows = if locals[:tuning_fields]
                   locals[:tuning_fields].map do |r|
                     snake = r[:key].to_s.gsub(/([a-z\d])([A-Z])/, '\1_\2').downcase
                     r.merge(action: "tuning.display.#{r[:key]}", label: I18n.t("tuning.#{snake}", lang: lang))
                   end
                 else
                   self.class.fields(self.class.values_from_telemetry(locals)).map do |r|
                     r.to_h.merge(label: I18n.t(r.action, lang: lang), action: r.action.split('.').last.split('_').map(&:capitalize).join, decimals: 2, help: '')
                   end
                 end

          fetch_row = ->(key) { rows.find { |r| (r[:key] || r[:action]).to_s == key } }
          servo_rows = SERVO_KEYS.map { |k| fetch_row.call(k) }.compact
          battery_rows = BATTERY_KEYS.map { |k| fetch_row.call(k) }.compact
          groups = FIRMWARE_GROUPS.map do |group_key, field_keys|
            members = field_keys.map { |fk| fetch_row.call(fk) }.compact
            [I18n.t(group_key, lang: lang), members]
          end.reject { |_title, members| members.empty? }
          bird_groups = BIRD_GROUPS.map do |group_key, field_keys|
            members = field_keys.map { |fk| fetch_row.call(fk) }.compact
            [I18n.t(group_key, lang: lang), members]
          end.reject { |_title, members| members.empty? }

          # ---- Servo desk (SERVO tab) — data from the C++ backend ----
          servo_presets = locals[:servo_presets] || []
          servo_names = servo_presets.map { |p| (p[:name] || p["name"]).to_s }
          servo_preset = locals.fetch(:servo_preset, -1).to_i
          has_preset = servo_preset >= 0 && servo_preset < servo_names.size
          spec = has_preset ? (servo_presets[servo_preset] || {}) : {}
          pick = ->(key) { spec[key] || spec[key.to_s] || 0.0 }
          min_v = (locals[:servo_min_voltage] || pick.call(:min_voltage) || 4.8).to_f
          max_v = (locals[:servo_max_voltage] || pick.call(:max_voltage) || 8.4).to_f
          rated_v = (locals[:servo_rated_voltage] || pick.call(:voltage) || 8.4).to_f
          servo_name = has_preset ? servo_names[servo_preset] : ""
          servo_status = if has_preset
                           format(I18n.t('servo.status', lang: lang), servo_name)
                         else
                           I18n.t('servo.status_custom', lang: lang)
                         end
          servo_range = if has_preset
                          format(I18n.t('servo.operating_range', lang: lang),
                                 min_v, max_v, rated_v,
                                 pick.call(:torque).to_f, pick.call(:speed).to_f)
                        else
                          ""
                        end
          servo_curve = (locals[:servo_curve] || []).map(&:to_f)

          # ---- Battery desk (BATTERY tab): live over-voltage warning ----
          bv_row = fetch_row.call('BatteryVoltage')
          battery_voltage = bv_row ? bv_row[:value].to_f : (locals[:BatteryVoltage] || 8.4).to_f
          overvolt = battery_voltage > max_v + 0.01
          battery_note = if overvolt
                           if has_preset
                             format(I18n.t('battery.warn_preset', lang: lang), battery_voltage, servo_name, max_v)
                           else
                             format(I18n.t('battery.warn_custom', lang: lang), battery_voltage)
                           end
                         else
                           I18n.t('battery.note', lang: lang)
                         end

          # The C++ telemetry publishes the profile as a NUMBER (0..3);
          # string values (demos/tests) are matched by name.
          profile = locals[:profile]
          profile_index = if profile.is_a?(String)
                            FLIGHT_PROFILES.index(profile.upcase) || 0
                          else
                            profile.to_i.clamp(0, FLIGHT_PROFILES.size - 1)
                          end

          {
            page: locals.fetch(:settings_page, 0).to_i.clamp(0, 4),
            bird_groups: bird_groups,
            tabs: TABS.map { |k| I18n.t(TAB_KEYS[k], lang: lang) },
            birds: BIRD_KEYS.map { |k| I18n.t(k, lang: lang) },
            profiles: FLIGHT_PROFILES.map { |k| I18n.t(PROFILE_KEYS[k], lang: lang) },
            profile_index: profile_index,
            bird_model: locals.fetch(:bird_model, 0), craft_info: self.class.craft_info_text(locals.fetch(:bird_model, 0)),
            camera_fpv: locals.fetch(:camera_fpv, false),
            fpv_angle: locals.fetch(:fpv_angle, 0), body_mass_kg: locals.fetch(:body_mass_kg, 0.45),
            cg_offset_mm: locals.fetch(:cg_offset_mm, 0), coupled_throttle: locals.fetch(:coupled_throttle, true),
            freelook_invert: locals.fetch(:freelook_invert, false), mouse_gains: locals.fetch(:mouse_gains, [1, -1, 1]),
            control_expo: locals.fetch(:control_expo, 0.65) * 100, speed_modifier: locals.fetch(:speed_modifier, 0.5),
            flight_safety: locals.fetch(:flight_safety, 1), replay_spirits: locals.fetch(:replay_spirits, true),
            trick_toasts: locals.fetch(:trick_toasts, true),
            wingbeat_volume: locals.fetch(:wingbeat_volume, 1), radio_volume: locals.fetch(:radio_volume, 0.5),
            servo_rows: servo_rows,
            battery_rows: battery_rows,
            servo_names: servo_names,
            servo_preset: servo_preset,
            has_preset: has_preset,
            servo_status: servo_status,
            servo_range: servo_range,
            servo_curve: servo_curve,
            battery_voltage: battery_voltage,
            overvolt: overvolt,
            battery_note: battery_note,
            groups: groups, frontend: locals.fetch(:frontend, false)
          }
        end
      end
    end
  end
end