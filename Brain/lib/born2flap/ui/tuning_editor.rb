# frozen_string_literal: true

module Born2Flap
  module UI
    # Backward-compatible facade for the hangar editor view controller.
    #
    # `TuningEditor.tree` / `.fields` and the constants delegate to
    # `Views::TuningEditor` (the real controller lives in views/tuning_editor/).
    # Prefer `Views::TuningEditor.render(:editor, ...)` in new code.
    module TuningEditor
      Field = Views::TuningEditor::Field
      BIRD_MODELS = Views::TuningEditor::BIRD_MODELS
      FLIGHT_PROFILES = Views::TuningEditor::FLIGHT_PROFILES

      def self.fields(values)
        Views::TuningEditor.fields(values)
      end

      def self.tree(values: {}, bird_model: 0, profile: "CRUISE")
        Views::TuningEditor.render(:editor,
                                   values: values, bird_model: bird_model, profile: profile)
      end
    end
  end
end
