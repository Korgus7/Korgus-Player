# Korgus Player

Rýchly multimediálny prehliadač pre Windows (JUCE 8, C++): strom priečinkov, mriežka náhľadov,
ukážka obrázkov (JPG, PNG, GIF, SVG a cez Windows BMP, TIFF, WebP, ICO, HEIC, RAW…), náhľady videí, zvuku (WAV, W64, AIFF/AIFC, FLAC, OGG, MP3, WMA, REX: .rx2/.rex/.rcy)
a MIDI (cez syntetizátor Windows) a triedenie súborov klávesmi.

## Ovládanie
| Kláves | Akcia |
|---|---|
| Šípky, PageUp/PageDown, Home/End | listovanie |
| Enter, dvojklik | otvorí priečinok, obrázok na celú obrazovku, iný súbor v predvolenom programe |
| V | mriežka / zoznam |
| F11 | obrázok na celú obrazovku (Esc späť) |
| Backspace, Alt+↑ | o priečinok vyššie |
| 1–9 | presun označeného súboru do cieľového priečinka |
| Ctrl+1–9 | aktuálny priečinok nastaví ako cieľ |
| Ctrl+Z | vráti posledný presun |
| Medzerník | prehrať / zastaviť zvuk |
| Ctrl+klik, Shift+klik, Shift+šípky, Ctrl+A | označenie viacerých súborov (Esc zruší) |
| Ťahanie myšou | skopíruje súbory do iného programu (DAW, Prieskumník) |
| Del | presun do koša |
| F2 | premenovanie |
| Na celej obrazovke: koliesko, +/−, 0, dvojklik, ťahanie | priblíženie, celý obrázok, 100 %, posun |
| F5 | obnoviť |

Ciele 1–9 sa dajú nastaviť aj klikom na spodnej lište (pravý klik = zrušiť).

## Zostavenie
Push do `main` spustí `build-windows.yml`. Hotový `Korgus Player.exe` a `version.txt` sú vo vetve `build`.
Lokálne: `cmake -B build -DJUCE_DIR=<cesta k JUCE>` a `cmake --build build --config Release`.

## Knižnice tretích strán
- [VelociLoops](https://github.com/kunitoki/VelociLoops) – čítanie REX2/REX/RCY (public domain), v `ThirdParty/VelociLoops`.
