# frozen_string_literal: true

require_relative "audio/aero_audio"
require_relative "audio/audio_driver"

module Born2Flap
  # Aero-audio-physics engine — the auditory half of the ornithopter's voice.
  # Pure functions (Telemetry → spectral params) + a driver that publishes
  # `set_audio_params` ops on the `:audio_effect` channel, rendered host-side.
  module Audio
  end
end
