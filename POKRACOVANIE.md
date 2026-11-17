# Korgus Player – pokračovanie práce (stav 17. 11. 2026, verzia 1.0.2)

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

## Plán ďalších krokov (neurobené)
1. Video cez libmpv
2. MIDI (SoundFont)
3. Cache náhľadov na disk, rýchlejší JPEG (WIC / libjpeg-turbo)
4. EPS (Ghostscript), PSD, RAW, PDF; REX až po overení licencie SDK
5. Aktualizácie cez `latest-player.json` v Korgus-Download (zatiaľ nerozhodnuté)
