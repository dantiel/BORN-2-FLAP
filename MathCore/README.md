# Born2Flap Math Core

Der Haskell-Math-Core ist die kanonische Heimat für zeitkritische, reine Zustandsübergänge:

- Aerodynamik und Crossflow
- partieller und dynamischer Stall
- Flügel- und Servokinematik
- ONDAS, Mixer, PID und Stabilisierung
- Fahrzeugkräfte und Momente

Der Kern enthält inzwischen ein deterministisches bilaterales Fahrzeugmodell mit 16
Streifen pro Flügel, dynamischer lokaler Ablösung, Crossflow-Transport, Added Mass,
einem reduzierten LEV-Zustand, Rumpf- und Leitwerkslasten sowie Ratendämpfung. Es ist
ein kalibrierbares Reduced-Order-Modell und ausdrücklich kein Ersatz für validierende CFD.

```sh
cabal test all
```

Die `foreign-library`-Komponente erzeugt `born2flap_math.dll`,
`libborn2flap_math.dylib` oder `libborn2flap_math.so`. Diese Datei muss nach
`Unreal/Born2Flap/Binaries/ThirdParty/` kopiert werden. Die C-ABI ist in
[`born2flap_math.h`](../Native/include/born2flap_math.h) festgelegt und verarbeitet
das gesamte Fahrzeug mit genau einem Aufruf pro Physikschritt.

Für reproduzierbare Builds werden GHC 9.6 und Cabal 3.10 verwendet. Das sehr alte,
systemweit installierte GHC 8.0.2 auf dem aktuellen Entwicklungs-Mac ist dafür nicht geeignet.
