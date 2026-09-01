# Born2Flap Math Core

Der Haskell-Math-Core ist die kanonische Heimat für zeitkritische, reine Zustandsübergänge:

- Aerodynamik und Crossflow
- partieller und dynamischer Stall
- Flügel- und Servokinematik
- ONDAS, Mixer, PID und Stabilisierung
- Fahrzeugkräfte und Momente

Das aktuelle Paket ist ein erster, absichtlich kleiner Scaffold mit gerichteter Crossflow-Funktion, Ablöserelaxation, typisierten Einheiten und einem reinen PID-Achsenschritt.

```sh
cabal test all
```

Die nächste Ausbaustufe implementiert den vollständigen Fahrzeugzustand und exportiert ihn über [`born2flap_math.h`](../Native/include/born2flap_math.h). Die FFI verarbeitet das Fahrzeug in einem Aufruf und gibt ausschließlich vorab definierte C-kompatible Strukturen zurück.
