# frozen_string_literal: true

module Born2Flap
  module UI
    # The glass cockpit — the semantic *authoring* source of the flight HUD.
    #
    # This is the same screen ABorn2FlapFlightHUD builds natively in C++ (see
    # Unreal/Born2Flap/Source/Born2Flap/UI/Born2FlapFlightHUD.cpp). The Brain
    # writes it once here in UMGHAML; the neutral tree is emitted to UMG (JSON
    # wire), HTML and React Native by the polymorphic emitters. One grammar,
    # many faces.
    #
    # The tree uses semantic components only (Banner / Panel / Stat / Gauge) —
    # no pixel styling, no colors, no fonts. The host resolves the look via
    # Born2FlapUiTheme.
    module FlightHud
      HAML = <<~HAML.freeze
        %Overlay{ align: "left", valign: "top" }
          %VBox{ spacing: 6 }
            %Banner{ text: "BORN 2 FLAP", tone: "accent" }
            %Panel{ title: "INSTRUMENTE", spacing: 2 }
              %Stat{ label: "HÖHE",    value: 0, unit: " m",   tone: "good" }
              %Stat{ label: "STEIGEN", value: 0, unit: " m/s" }
              %Stat{ label: "GESCHW.", value: 0, unit: " m/s", tone: "info" }
            %Panel{ title: "ENERGIE", spacing: 2 }
              %Gauge{ label: "BATTERIE", value: 100, min: 0, max: 100, unit: "%", tone: "good" }
              %Gauge{ label: "SCHUB",   value: 0,   min: 0, max: 1,             tone: "accent" }
            %Banner{ text: "BEREIT", tone: "normal" }
      HAML

      # Parses the authoring source into the neutral tree (top widget).
      def self.tree
        HamlParser.parse(HAML).children.first
      end

      # A simple live builder: returns the tree with the readout leaves patched
      # to the given telemetry. The reconciler turns tree→tree into the minimal
      # op stream (only changed values propagate).
      def self.build(telemetry)
        t = telemetry
        HamlParser.parse(<<~HAML).children.first
          %Overlay{ align: "left", valign: "top" }
            %VBox{ spacing: 6 }
              %Banner{ text: "BORN 2 FLAP", tone: "accent" }
              %Panel{ title: "INSTRUMENTE", spacing: 2 }
                %Stat{ label: "HÖHE",    value: #{t[:altitude]}, unit: " m",   tone: "good" }
                %Stat{ label: "STEIGEN", value: #{t[:climb]},    unit: " m/s" }
                %Stat{ label: "GESCHW.", value: #{t[:speed]},    unit: " m/s", tone: "info" }
              %Panel{ title: "ENERGIE", spacing: 2 }
                %Gauge{ label: "BATTERIE", value: #{t[:battery]}, min: 0, max: 100, unit: "%", tone: "good" }
                %Gauge{ label: "SCHUB",   value: #{t[:throttle]}, min: 0, max: 1,             tone: "accent" }
              %Banner{ text: "#{t[:status]}", tone: "normal" }
        HAML
      end
    end
  end
end
