<!-- GENERIERT aus docs/release-readiness.json -- nicht von Hand editieren. -->

# BORN-2-FLAP Release-Readiness-Katalog

Produkt: **BORN-2-FLAP** · Version: **Alpha 0.1.0**

## Legende

- 🟢 = erfüllt
- 🟡 = teilweise
- 🔴 = offen
- ⛔ = Blocker
- ➖ = n/a

## Gates

- **G1 — Rechtliche Freigabe**: 8🟢 / 4🟡 / 0🔴 / 0⛔ — Lizenzen, Drittanbieter-Attribution und Unreal-EULA-Compliance
- **G2 — Technische Stabilität**: 2🟢 / 0🟡 / 2🔴 / 2⛔ — Flugphysik, Absturzfreiheit und bekannte Fehler
- **G3 — Build & Distribution**: 5🟢 / 1🟡 / 0🔴 / 0⛔ — Reproduzierbare Win64/macOS-Builds und GitHub-Release-Artefakte
- **G4 — Store-Infrastruktur**: 0🟢 / 0🟡 / 6🔴 / 0⛔ — Steamworks/Epic-Services: Achievements, Cloud-Saves, Overlay
- **G5 — Store-Präsentation**: 0🟢 / 1🟡 / 3🔴 / 0⛔ — Store-Assets, Alterseinstufung und Steam-Deck-Tauglichkeit

## Katalog

### G1 — Rechtliche Freigabe

| ID | Anforderung | Kategorie | Scope | Status | Beleg |
|----|-------------|-----------|-------|--------|-------|
| LIC-01 | Eigencode unter MIT-Lizenz | Lizenz | Alle | 🟢 | LICENSE |
| LIC-02 | Unreal-Engine-EULA eingehalten | Recht | Alle | 🟢 | Unreal/Born2Flap/Born2Flap.uproject |
| LIC-03 | Drittanbieter-Assets CC0 (Poly Haven) | Lizenz | Alle | 🟢 | docs/ravenstonefield.md, docs/natural-valley-and-rc.md, docs/shiomori-bay.md |
| LIC-04 | Kein CC-NC-Material gebündelt | Lizenz | Steam, Epic | 🟡 |  |
| LIC-05 | Kein GPL-Code verlinkt | Lizenz | Alle | 🟢 | MathCore/born2flap-math.cabal |
| LIC-06 | ChakraPetch-OFL gebündelt | Lizenz | Alle | 🟢 | Unreal/Born2Flap/Tools/fonts/OFL.txt, Tools/package-win.ps1 |
| LIC-07 | Olympia-Script-Lizenz geklärt | Lizenz | GitHub | 🟢 | web/assets/fonts/OlympiaScript-LICENSE.txt |
| LIC-08 | THIRD-PARTY-NOTICES im Paket | Lizenz | Alle | 🟢 | THIRD-PARTY-NOTICES.md, Tools/package-win.ps1 |
| LIC-09 | 7-Zip-SFX-Lizenz im Installer | Lizenz | Alle | 🟡 |  |
| LIC-10 | Haskell-cabal-Lizenzzeile | Lizenz | Alle | 🟡 |  |
| REV-01 | Unreal-Royalty-Schwelle dokumentiert | Recht | Steam, Epic | 🟢 | docs/release-packaging.md |
| REV-02 | Preis-/Vertriebsmodell festgelegt | Recht | Steam, Epic | 🟡 |  |

### G2 — Technische Stabilität

| ID | Anforderung | Kategorie | Scope | Status | Beleg |
|----|-------------|-----------|-------|--------|-------|
| STA-01 | Bank-Recovery stabil | Stabilität | Alle | ⛔ |  |
| STA-02 | Dauerflug (sustained flight) stabil | Stabilität | Alle | ⛔ |  |
| STA-03 | 30-min-Dauerlauf ohne Crash | Stabilität | Steam, Epic | 🔴 |  |
| STA-04 | Haskell-Regressionstests grün | Stabilität | Alle | 🟢 | .github/workflows/ci.yml |
| STA-05 | Packaged-Smoke-Tests grün | Stabilität | Alle | 🟢 | Tools/test-package-win.ps1 |
| STA-06 | Flugkoeffizienten kalibriert | Stabilität | Alle | 🔴 |  |

### G3 — Build & Distribution

| ID | Anforderung | Kategorie | Scope | Status | Beleg |
|----|-------------|-----------|-------|--------|-------|
| WIN-01 | Win64-Shipping-Build reproduzierbar | Build/Distribution | Alle | 🟢 | Tools/package-win.ps1 |
| WIN-02 | macOS-Build reproduzierbar | Build/Distribution | GitHub | 🟡 | Tools/package-mac.sh |
| WIN-03 | 64-Bit-only Artefakte | Build/Distribution | Steam, Epic | 🟢 | Tools/package-win.ps1 |
| WIN-04 | GitHub-Release-Automatik | Build/Distribution | Alle | 🟢 | .github/workflows/release.yml |
| WIN-05 | Debug-Symbole ausgeschlossen | Build/Distribution | Alle | 🟢 | Tools/package-win.ps1 |
| WIN-06 | Checksums (.sha256) | Build/Distribution | Alle | 🟢 | Tools/package-win.ps1 |

### G4 — Store-Infrastruktur

| ID | Anforderung | Kategorie | Scope | Status | Beleg |
|----|-------------|-----------|-------|--------|-------|
| EGS-01 | Epic-Services-Integration | Store-Infra | Epic | 🔴 |  |
| EGS-02 | Epic-Achievements | Store-Infra | Epic | 🔴 |  |
| EGS-03 | Epic-Cloud-Saves | Store-Infra | Epic | 🔴 |  |
| STM-01 | Steamworks-AppID | Store-Infra | Steam | 🔴 |  |
| STM-02 | Steam-Achievements | Store-Infra | Steam | 🔴 |  |
| STM-03 | Steam-Cloud-Saves | Store-Infra | Steam | 🔴 |  |

### G5 — Store-Präsentation

| ID | Anforderung | Kategorie | Scope | Status | Beleg |
|----|-------------|-----------|-------|--------|-------|
| DEC-01 | Steam-Deck-Tauglichkeit | Plattform | Steam | 🔴 |  |
| DEC-02 | Store-Assets (Capsule/Screenshots) | Store-Präsentation | Steam, Epic | 🔴 |  |
| DEC-03 | Alterseinstufung (IARC) | Store-Präsentation | Steam, Epic | 🔴 |  |
| DEC-04 | Controller-/Deck-Input | Plattform | Steam | 🟡 |  |

