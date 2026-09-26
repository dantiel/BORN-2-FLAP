# frozen_string_literal: true

module Born2Flap
  module Audio
    # Drives the aero-audio voices from physics telemetry — the continuous-audio
    # half of react-native-umg, mirroring EffectDriver exactly: the Brain owns
    # state, the host renders. Each `drive(telemetry)` computes per-voice
    # spectral params and publishes them as `set_audio_params` host ops on the
    # `:audio_effect` channel, which Transport frames into the NDJSON stream.
    #
    #   AeroAudio::Telemetry ──► AudioDriver.drive ──► :audio_effect ──► C++ synth
    class AudioDriver
      def initialize(bus: nil)
        @bus = bus
      end

      # Compute spectral params for every voice and publish them. Returns the
      # host-op stream (nil-safe for headless tests).
      def drive(telemetry)
        ops = AeroAudio::VOICES.map do |voice|
          [:set_audio_params, voice.to_s, AeroAudio.compute(voice, telemetry)]
        end
        @bus.publish(:audio_effect, ops: ops) if @bus
        ops
      end
    end
  end
end
