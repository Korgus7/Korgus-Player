# Korgus Player – pokračovanie práce (stav 18. 11. 2026, verzia 1.0.8)

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

- `WinImaging` (len Windows) – WIC dekódovanie (BMP, TIFF, WebP, ICO, HEIC/AVIF/RAW podľa kodekov, JPEG zmenšený priamo v dekodéri cez scaler Fant), orientácia cez System.Photo.Orientation, náhľady zo Shellu (SIIGBF_THUMBNAILONLY) pre videá, PDF, PSD… COM sa zapína v každom vlákne.
- `ThumbCache` – náhľady aj na disku: `%LOCALAPPDATA%\Korgus Player\Nahlady` (kľúč = cesta+veľkosť+čas zmeny), PNG pri priehľadnosti, inak JPEG; pri štarte sa upratuje nad 600 MB.

## Vydané v 1.0.4
- Obrázky cez Windows (veľa formátov, rýchlejšie veľké JPEG), náhľady videí a PDF/PSD z Windows aj vo veľkej ukážke, náhľady uložené na disku.
- WIC/Shell kód sa na Linuxe nekompiluje (prázdne funkcie) – overuje sa len zostavením na GitHube a u používateľa.

- `VideoView` (len Windows) – libmpv načítané dynamicky (LoadLibrary `libmpv-2.dll` vedľa exe), vložené do vlastného STATIC okna cez HWNDComponent, ovládanie cez mpv_command, poloha/dĺžka/pauza cez mpv_get_property v časovači. Ak dll chýba, video sa správa ako predtým (náhľad + Enter = predvolený program). Klik do videa môže vziať klávesnicu – časovač ju vracia mriežke.
- CI sťahuje libmpv z `shinchiro/mpv-winbuild-cmake` (najnovší release, nie v3) a dáva ho ako samostatný artifact `libmpv` (do vetvy build sa nedáva – veľkosť). Stiahnuť: `gh run download <run> -R Korgus7/Korgus-Player -n libmpv`.

## Vydané v 1.0.5
- Prehrávanie videa priamo v ukážke (Medzerník pauza, klik na lištu posun).

## Opravy 1.0.6–1.0.8 (podľa skúšania)
- MIDI začínalo o ~9 s neskôr: MidiOutput::sendBlockOfMessages počíta s Time::getMillisecondCounter (timeGetTime), nie s HiRes (QPC) – vo Windows majú iný základ.
- Zlá veľká vlna pri REX/W64: AudioThumbnail čítal z prúdu a MP3 dekodér JUCE mylne prijal tieto súbory. Teraz `wave.setReader (formats.createReaderFor (file))`. Test: `"Korgus Player.exe" --decode "<súbor>" "<výstup.f32>"` vypíše formát podľa prípony aj obsahu.
- Svetlá šachovnica pod priehľadné obrázky (mriežka, ukážka, celá obrazovka).
- libmpv: CI ho dáva zbalený do vetvy `libmpv` (libmpv-2.zip + libmpv-verzia.txt), len keď vyjde nová verzia. DLL má ~120 MB.

## Vydané v 1.0.9+ (fonty, PDF, 3D, DXF)
- `DocViewers.h` – spoločný základ `DocViewer` (load, infoText, handleKey, hintText) + `createViewer(Kind)`; ten istý prehliadač v ukážke aj na celej obrazovke (`FullscreenDoc`, Enter/F11, ← → ďalší súbor rovnakého typu).
- `FontView` – TTF/OTF/TTC cez Typeface::createSystemTypefaceFor; náhľad „Aa“ sa kreslí na hlavnom vlákne.
- `PdfView` + `WinPdf.cpp` – Windows.Data.Pdf cez C++/WinRT (súbor bez JUCE hlavičiek, linkuje windowsapp, /bigobj), strany ↑↓/PageUp/PageDown/koliesko, klik na ◀ ▶. Na Linuxe nefunguje.
- `ModelView` – STL (bin/text), OBJ, 3MF (zip + XML, bez transformácií build itemov); softvérové vykresľovanie so z-bufferom, Z hore, ťahanie otáča, pravé/Shift posúva, koliesko zoom, rozmery v mm.
- `CadView` – vlastný DXF parser: LINE, CIRCLE, ARC, LWPOLYLINE (bulge), POLYLINE/VERTEX, ELLIPSE, SPLINE (de Boor), SOLID, 3DFACE, TEXT/MTEXT/ATTRIB, INSERT (aj polia), DIMENSION (blok), farby ACI/vrstvy/true color, vypnuté vrstvy. Chýba HATCH a binárny DXF.

## Ďalšie formáty a info o súboroch
- `TextDoc.cpp` – TextView (txt, md, kód s číslami riadkov, prozaické typy sa zalamujú; RTF vlastný prevod, docx/odt text z XML, kódovanie: BOM/UTF-8/inak Windows ANSI cez `win::decodeAnsi`), TableView (csv/tsv s autodetekciou oddeľovača, xlsx prvý hárok + sharedStrings, ods), SlidesView (pptx/odp/key/pages/numbers – vložený náhľad zo zipu, počet snímok).
- `EmbeddedPreview.cpp` – vložené náhľady: EPS (DOS EPS TIFF/WMF, EPSI hex), CDR (zip thumbnail / RIFF DISP), DWG (sentinel + BMP/PNG/WMF), Blend (TEST blok, aj gzip a nový formát hlavičky), EMF/WMF cez GDI. Tieto typy sú Kind::Image.
- `MfAudio.cpp` – AAC/M4A/Opus/ALAC/AC3 cez Media Foundation IMFSourceReader (float, posun cez SetCurrentPosition a zahodenie vzoriek podľa časovej značky). Len Windows.
- `AppView.cpp` – exe/msi/dll/lnk: ikona zo Shellu + GetFileVersionInfo. Enter program spustí.
- `FormatInfo.cpp` – zabudovaná tabuľka: názov typu, programy zadarmo, pôvodný program, tip. Zobrazuje sa v ukážke; tlačidlo „Otvoriť v…“ (SHAssocEnumHandlers + IAssocHandler::Invoke, predvolený program, ukázať v priečinku).
- Archívy používateľ nechce.

## Úpravy obrázkov a ďalšie (1.0.13+)
- `ImageEdit` – R/L otočenie (JPG bez straty cez EXIF orientáciu, vloží EXIF ak chýba; PNG nanovo), Ctrl+R zmena veľkosti (dlhšia strana / %, kópia `_1920` alebo prepísanie JPG/PNG), hromadne na označené, na pozadí (editPool).
- Pravé tlačidlo na súbore: otočiť, zmeniť veľkosť, premenovať, kôš, otvoriť / otvoriť v, ukázať v priečinku, kopírovať cestu.
- Programy pod ukážkou sú čipy: klik = kopírovať názov, pravý klik = aj hľadať na webe.
- VideoView skrýva natívne okno mpv pri skrytí (inak zakrývalo ukážku).

## Prehrávač hudby a prezentácia (1.0.15+)
- `AudioPlayer` (vlastník MainComponent) – jediný prehrávač zvuku v programe; zoznam = zvuky v priečinku pri výbere, opakovanie Off/All/One, náhodne (história pre „predošlú“), hlasitosť; hrá ďalej pri prezeraní iných súborov. Výber zvuku ho spustí (ak to nie je práve hrajúca skladba). Nastavenia v settings: repeat, shuffle, volume.
- `PlayerBar` – lišta nad stavovou lištou: ⏮ ▶/❚❚ ⏹ ⏭, názov (klik = ukázať súbor), posuvník, čas, opakovanie, náhodne, hlasitosť. Klávesy: medzerník, Ctrl+←/→, mediálne klávesy.
- `Icons.h` – ikony ako cesty + `IconButton`.
- Prezentácia: tlačidlo „Prezentácia“ hore, na celej obrazovke F5/tlačidlo, prelínanie (snímka obrazovky + priehľadnosť 650 ms), interval 2–30 s (+/−, tlačidlo), náhodne, na konci od začiatku.
- Viditeľné tlačidlá ↺ ↻ a „Veľkosť“ v ukážke obrázka aj na celej obrazovke.
- Oprava: ImageCache/ThumbCache majú verziu na cestu – po úprave súboru sa staršie výsledky zahodia (predtým po rýchlom 2× L ostal starý/malý obrázok). Zápis otočeného súboru sa skúša opakovane (súbor môže práve čítať náhľad).

## Dávka 1 (1.0.16)
- Zmena veľkosti: kópia vedľa / do podpriečinka „zmensene“ / do vybraného priečinka (pamätá si `resizeFolder`) / prepísať.
- Vyhľadávanie v hornej lište (Ctrl+F, bez diakritiky, viac slov), filter typu (Všetko, Obrázky, Zvuk, Video, Dokumenty, 3D a CAD, Priečinky). FileGrid drží `allEntries` a filtrované `entries`.
- `AudioTags` – ID3v2.2–2.4 (TIT2, TPE1, TALB, TYER/TDRC, APIC), FLAC (VORBIS_COMMENT, PICTURE), MP4/M4A (©nam, ©ART, aART, ©alb, ©day, covr), obal aj z cover/folder/front.jpg. Lišta prehrávača ukazuje obal a „Interpret – Názov“, ukážka obal nad vlnou.
- Fronta: Q alebo pravé tlačidlo → „Pridať do fronty“; next() berie najprv z fronty.

## Dávka 2 (1.0.17+)
- `PhotoInfo` – EXIF z JPG: fotoaparát, objektív, clona, čas, ISO, ohnisko, dátum fotenia, GPS → tlačidlo „Ukázať na mape“ (OpenStreetMap). V ukážke nahrádza blok „čím otvoriť“ pri fotkách.
- Triedenie v hornej lište (Názov A–Z/Z–A, najnovšie, najstaršie, dátum fotenia, najväčšie, typ); dátumy fotenia sa čítajú na pozadí. Klik na hlavičku v zozname = „Vlastné“.
- Prezentácia: šípky ◀ ▶ pri intervale (aj ↑↓ počas prezentácie), tlačidlo veľkosti Celý / Vyplniť / Pôvodná (F).
- `MetaStore` – hviezdičky 0–5 a farebné štítky (6 farieb) v `hodnotenia.xml` v priečinku nastavení; Alt+1–5 / Alt+0, pravé tlačidlo, klik v ukážke; filter „S hodnotením“ a „So štítkom“; presúvajú sa so súborom.
- Hromadné premenovanie (Ctrl+F2 / pravé tlačidlo): vzor {datum} {cas} {cislo} {nazov} {priecinok}, ukážka, dva kroky cez dočasné mená, Ctrl+Z.
- Obľúbené priečinky: pravé tlačidlo v strome → Pridať do obľúbených ★ (hore v strome, `favorites` v nastaveniach).

## Plán (zostáva)
- Dávka 3: duplikáty, orezanie a jas/kontrast, video na celú obrazovku a snímka z videa, BPM a tónina, hudba pod prezentáciou.
- Dávka 4: dva panely, svetlý vzhľad, angličtina, „Otvoriť v Korgus Player“ z Prieskumníka, automatické aktualizácie (opýtať sa kam – návrh Korgus-Download).

## Dôležité pri nahrávaní na disk používateľa
- `device_commit_files` so `stagedPath` raz zapísal STARÚ verziu exe (rovnaká cesta → cache). Exe preto vždy nahrávať cez `SendUserFile` → `fileUuid` a po zápise skontrolovať veľkosť cez `device_list_dir`.
- Keď je program spustený, súbor je zamknutý – uložiť pod iným názvom (napr. `Korgus Player 1.0.x.exe`) a povedať používateľovi.

## Plán ďalších krokov (neurobené)
4. EPS (Ghostscript), PSD, RAW, PDF; REX až po overení licencie SDK
5. Aktualizácie cez `latest-player.json` v Korgus-Download (zatiaľ nerozhodnuté)

## Dávka 3 (hotovo)
- Hľadanie duplikátov (kontextové menu → veľkosť + SHA256, označí duplikáty).
- Hudba pod prezentáciou (tlačidlo „Hudba“ vo fullscreen).
- BPM a tónina pri samploch (AudioInfoCache: BPM z názvu alebo autokorelácia, tónina Goertzel chroma + Krumhansl).
- Editor obrázkov (ImageEditor, kláves E / tlačidlo Upraviť / menu): orezanie s pomermi, jas, kontrast, sýtosť, uložiť ako _upravene / prepísať.
- Video: tlačidlo fotoaparát = snímka do PNG (meno_snimka_mm-ss.png, mpv screenshot-to-file), tlačidlo/Enter = celá obrazovka (VideoFullscreen, pokračuje od aktuálneho času, ←/→ ±5 s, Shift ±30 s).

## Dávka 4
- Svetlá téma (col:: farby sú premenné, col::applyTheme; zmena = restartUI znovu vytvorí okno).
- Angličtina: všetky texty idú cez tr(); slovník tools/en_ui.py → tools/gen_translations.py → Source/Translations.cpp. Nový text = pridať do en_ui.py a pregenerovať. Doterajší používatelia ostávajú v SK, noví podľa jazyka Windows.
- ☰ nastavenia: téma, jazyk, „Otvoriť v Korgus Player“ v Prieskumníkovi (HKCU\Software\Classes\{Directory,Directory\Background,*}\shell\KorgusPlayer, cesta k exe sa obnovuje pri štarte), stiahnutie libmpv, aktualizácie.
- Aktualizácie: Updater.cpp číta https://raw.githubusercontent.com/Korgus7/Korgus-Download/main/latest-player.json (version, url, libmpv, notes). Exe sa premenuje na .old, nové sa nasťahuje a spustí. Zverejnenie: tools/release.sh "poznámky" (klon Korgus-Download vedľa, push z tejto relácie; CI do neho nemá prístup).
- Dva panely (◫ / F9): druhý panel namiesto náhľadu, F5 kopírovať (na pozadí), F6 presunúť (Ctrl+Z vráti), Tab prepína, strom otvára v aktívnom paneli.
- POZOR verzia: PowerShell v CI delil `-DKP_VERSION=1.0.23` na bodke → v programe bolo "1". Opravené úvodzovkami vo workflow (od 1.0.26). Verzia ide cez vygenerovaný Version.h.

## Zoznam nápadov / čo ďalej (stav 18. 11. 2026, verzia 1.0.30)
Zatiaľ nič z toho nie je rozhodnuté – čaká sa, čo si používateľ vyberie.

### Filmy (video už hrá cez libmpv)
- [x] Výber titulkov a zvukovej stopy (MKV s viacerými jazykmi), aj načítanie .srt vedľa filmu
- [x] Pamätať si, kde sa film prestal pozerať (pokračovať od toho miesta)
- [x] Na celej obrazovke: hlasitosť a posúvanie myšou (lišta sa ukáže pri pohybe myši)
- [ ] DVD/Blu-ray disky (súbory z nich už idú)

### Tlač
- [x] Rýchla tlač: pravý klik → Tlačiť / Ctrl+P = ShellExecute "print" (fotky cez sprievodcu tlačou Windows, PDF cez predvolený prehliadač, text cez Poznámkový blok), viac označených naraz – navrhnuté ako prvé
- [ ] Vlastná tlač s náhľadom: tlačiareň, papier, orientácia, prispôsobiť na stranu, viac obrázkov na stranu

### Ďalšie návrhy
- [x] Kopírovanie/presun s ukazovateľom priebehu a tlačidlom Zrušiť (veľké videá)
- [x] Porovnanie dvoch fotiek vedľa seba (výber najlepšej z podobných)
- [x] Prehrávanie samplov synchronizovane v tempe (BPM), voliteľne cez vybraný zvukový/MIDI výstup
- [x] Export zoznamu skladieb/samplov (názov, dĺžka, BPM, tónina) do tabuľky
- [x] Hromadný prevod formátov (WAV → MP3/FLAC, HEIC → JPG, PNG → JPG)
- [x] Inštalátor (ako Sequencer) – ikona na ploche a v Štart menu – odporúčané
- [x] Stránka na cami.sk s popisom a tlačidlom Stiahnuť (súbory už sú v Korgus-Download/player) – odporúčané

### Otázky používateľa, ktoré už sú zodpovedané
- Políčka 1–9 dole = cieľové priečinky na triedenie (v programe je teraz tlačidlo „?“ s návodom).
- Korgus-Download je verejný, Korgus-Player ostáva súkromný.

### Ako je to spravené (dávka 5)
- Filmy: VideoView – tlačidlo „Titulky / zvuk“ (mpv track-list, sid/aid, sub-add), hlasitosť (posuvník, ↑↓ na celej obrazovke), pozície v „Korgus Player/pozicie-videa.xml“ (len videá ≥ 5 min, od 30 s do konca−60 s). sub-auto=fuzzy, slang/alang sk,cs,en.
- Tlač: win::printFiles – IContextMenu „print“ pre súbory z jedného priečinka (fotky = sprievodca tlačou Windows), Ctrl+P / menu.
- Transfer.cpp: kopírovanie F5 a presun na iný disk na pozadí po 1 MB, priebeh v stavovej lište, Esc zruší.
- CompareView (C): dve fotky, spoločné priblíženie, ←/→ mení pravú, 1/2 = ktorú nechať (druhá sa označí).
- Export CSV (menu): UTF-8 s BOM, oddeľovač ;, BPM/tónina/dĺžka cez AudioInfoCache::computeNow.
- Convert.cpp: JPG/PNG, WAV16/24, FLAC, OGG (JUCE), MP3 320k a M4A AAC cez Media Foundation (len 44,1/48 kHz).
- Tempo (♩ v lište): AudioPlayer::setSyncTempo – loopy ≤ 60 s so známym BPM sa hrajú zrýchlene (re-pitch) cez sampleRate×pomer.
- Ikona Resources/icon.png (+ .ico). Inštalátor installer/KorgusPlayer.iss (Inno Setup, PrivilegesRequired=lowest, {localappdata}\Programs\Korgus Player, s libmpv), CI ho dáva do vetvy build, release.sh ho kopíruje do Korgus-Download/player.
- Web: Korgus-Download/player/index.html + img/ (snímky z Linuxu bez stromu priečinkov) – nahrať na cami.sk/korgusplayer. Lepšie by boli snímky z Windows.
- Ostáva: vlastná tlač s náhľadom, DVD/Blu-ray disky.

## Dávka 6 (18. 11. popoludní)
- Hľadanie: Enter v poli Hľadať = všetky podpriečinky (FileGrid::showList – „zoznamový“ režim, Esc/Backspace späť).
- Podobné fotky: Similar.cpp – dHash 64 bit (≤ 8) + farby 4×4 (priem. rozdiel ≤ 22), union-find, nechá najväčší súbor, ostatné označí.
- Podobné zvuky: 7 vlastností (dĺžka, nástup, doznievanie, hlasitosť, ZCR, jas, basy), z-skóre, 40 najbližších.
- Karty: TabStrip nad mriežkou, Ctrl+T/W/Tab, uložené v nastaveniach „tabs“.
- Sequencer: menu „Otvoriť Korgus Sequencer“ (%LOCALAPPDATA%\Programs\Korgus Sequencer), sample sa doň ťahá myšou (Sequencer berie drop audio/MIDI na stopu).
- Video z fotiek: VideoExport.cpp – MF SinkWriter H.264 1080p30 12 Mbps + AAC 48 kHz, Ken Burns, prelínanie, stlmenie na konci.
- Tlač: WinPrint.cpp – fotky a SVG cez GDI (PrintDlg), texty Consolas 10 pt s okrajmi 15 mm, ostatné ShellExecuteEx "print" NOASYNC. Pozor: „small“ je makro vo Windows.
- Vzhľad: horný panel 48 px, lupa v hľadaní, strom odsadený.
- Web: Korgus-Download/player/index.html + navod.html (generuje tools/gen_navod.py) + img/. ☰ → Návod otvára cami.sk/korgusplayer/navod.html.
- Propagácia: Claude Docs „Korgus Player – texty na propagáciu“.
