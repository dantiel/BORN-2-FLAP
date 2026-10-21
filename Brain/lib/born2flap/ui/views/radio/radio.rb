# frozen_string_literal: true

module Born2Flap
  module UI
    module Views
      # The in-game radio HUD (was native C++ ABorn2FlapRadioHUD): station,
      # track and a volume gauge, driven by radio_* telemetry.
      class Radio < ViewController
        view :panel, template: "radio.umghaml"

        def panel_locals(locals = {})
          playing = locals[:radio_playing] != false
          muted = locals[:radio_muted] == true
          cover = (locals[:radio_cover] || "").to_s
          cover = "/Game/UI/Elements/RadioOn.RadioOn" if cover.empty?
          {
            radio_station: (locals[:radio_station] || "FIELD RADIO").to_s,
            radio_track: (locals[:radio_track] || "").to_s,
            radio_cover: cover,
            radio_volume: (locals[:radio_volume] || 0.5).to_f,
            play_icon: muted ? "MUTED" : (playing ? "PLAYING" : "PAUSED"),
            play_action: muted ? "radio.mute" : "radio.play"
          }
        end
      end
    end
  end
end