# Korgus Player – pokračovanie práce (stav 18. 11. 2026, verzia 1.0.3)

## Čo to je
- Rýchly multimediálny prehliadač pre Windows (JUCE 8.0.15, C++, natívne rozhranie bez WebView2).
- Repozitár (súkromný): **Korgus7/Korgus-Player**
  `git clone https://github.com/Korgus7/Korgus-Player.git /home/claude/korgus-player`
- Push do `main` spustí **build-windows.yml** (asi 6–8 min). Výsledok je vo vetve `build` (`Korgus Player.exe`, `version.txt` = 1.0.<číslo behu>).
- Lokálna kontrola: JUCE do `/home/claude/JUCE` (tag 8.0.15), `cmake -G Ninja -B /home/claude/kpbuild -DJUCE_DIR=/home/claude/JUCE`, `ninja`. Test pod `xvfb-run`; klávesy cez xdotool fungujú až po kliknutí do mriežky.
- Hotový .exe zapisovať do `D:\_Claude\korgusplayer`.

## Súbory
- `Main.cpp` – aplikácia, okno, LookAndFeel (farby v `Common.h`, zaoblenie 3 px, #2d4362). `--selftest` hneď skončí.
- `MainComponent` – rozloženie (strom | mriežka | ukážka), klávesy, presun 1–9, Ctrl+Z, ciele v nastaveniach (`%APPDATA%\Korgus Player`).
- `FolderTree` – obľúbené priečinky + disky, podpriečinky sa načítajú pri otvorení.
- `FileGrid` – mriežka, kreslí len viditeľné dlaždice.
- `ThumbCache` – náhľady na pozadí (viac vlákien), max 1500 v pamäti.
- `ImageLoader` – JPEG s EXIF náhľadom a orientáciou, PNG, GIF, SVG (JUCE Drawable).
- `PreviewPane` – obrázok (prednačíta ±2 susedov, 7 v pamäti), zvuk (AudioTransportSource + AudioThumbnail, zariadenie sa otvorí až pri prvom zvuku).
- `StatusBar` – správa + ciele 1–9 (klik = vybrať, pravý klik = zrušiť).
- `ImageCache` – veľké obrázky pre ukážku aj celú obrazovku (spoločné, 7 v pamäti, zobrazený sa nevyhadzuje).
- `AudioInfoCache` – dĺžka, formát a malá vlna (96 stĺpcov) pre zoznam; WMA sa preskakuje (COM na pozadí).
- `FullscreenView` – celá obrazovka (kiosk). Šípky/koliesko/PageUp/Down listujú len obrázky, Home/End, Esc/Enter/dvojklik zavrie, 1–9 a Ctrl+Z idú do MainComponent::handleSortKey.

## Vydané v 1.0.2
- Zoznam so stĺpcami Názov · Vlna · Dĺžka · Formát · Veľkosť · Zmenené (stĺpce sa skrývajú pri úzkom okne), triedenie klikom na hlavičku.
- Prepínanie mriežka/zoznam: tlačidlo vpravo hore alebo V. Automaticky: viac ako polovica súborov zvuky = zoznam, obrázky/videá = mriežka, inak ostane posledný režim.
- Enter/dvojklik na obrázok = celá obrazovka (F11 tiež). Ostatné súbory Enter otvorí v predvolenom programe.

- `ExtraFormats` – REX (.rx2/.rex/.rcy cez VelociLoops, loop sa skladá zo slice-ov; tempo sa vyberá to, pri ktorom dĺžka sedí na nahraté audio – PPQ je 15360 na dobu, nie na takt ako píše hlavička knižnice), AIFC (ima4, ulaw, alaw, fl64, in24, in32, .aifc – len ak JUCE AIFF zlyhá), Wave64 (.w64, streamovane). `registerAllFormats()` používa ukážka aj zoznam.
- `MidiPlayer` – MIDI cez predvolený MIDI výstup (Microsoft GS Wavetable Synth), noty pre klavírny pás, hustota nôt v zozname.

## Vydané v 1.0.3
- REX, AIFC varianty, W64, MIDI.
- Viac označených (Ctrl/Shift/Ctrl+A, Esc zruší), 1–9 presúva všetky, Ctrl+Z vráti celú dávku.
- Ťahanie súborov von (kopírovanie), Del = kôš, F2 = premenovanie (okno vnútri programu).
- Celá obrazovka: koliesko/+/−/0 priblíženie, ťahanie posun, dvojklik celý ↔ 2×/100 %, pri priblížení sa dočíta plné rozlíšenie.
- Testovacie súbory od používateľa: `D:\_Claude\testsuborov` (1010.w64, Bass Analog Classic.aif, LPD-Fill_128Bpm-Big_007.Rx2) – všetky hrajú.

## Plán ďalších krokov (neurobené)
1. Video cez libmpv
3. Cache náhľadov na disk, rýchlejší JPEG (WIC / libjpeg-turbo)
4. EPS (Ghostscript), PSD, RAW, PDF; REX až po overení licencie SDK
5. Aktualizácie cez `latest-player.json` v Korgus-Download (zatiaľ nerozhodnuté)
