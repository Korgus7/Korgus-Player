# Vygeneruje navod.html (slovensky + anglicky) v štýle stránky Korgus Player
import re
src=open('/home/claude/korgus-download/index.html',encoding='utf-8').read()
style=src[src.index('<style>')+7:src.index('</style>')]
extra='''
.doc{display:grid;grid-template-columns:240px 1fr;gap:40px;padding:40px 0 80px}
.toc{position:sticky;top:80px;align-self:start;font-size:14px;max-height:calc(100vh - 100px);overflow:auto}
.toc a{display:block;padding:5px 0;color:var(--sub);text-decoration:none}.toc a:hover{color:var(--txt)}
.doc h2{font-size:28px;margin:48px 0 10px;padding-top:10px;border-top:1px solid var(--line)}
.doc h2:first-child{margin-top:0;border-top:0}
.doc h3{font-size:18px;margin:26px 0 8px;color:var(--gold)}
.doc p,.doc li{color:var(--sub)}.doc b{color:var(--txt)}
.doc ul,.doc ol{padding-left:22px}.doc li{margin:4px 0}
kbd{display:inline-block;background:var(--panel);border:1px solid var(--line);border-bottom-width:2px;border-radius:3px;padding:0 6px;font:600 12.5px Consolas,monospace;color:var(--txt);white-space:nowrap}
table.keys{border-collapse:collapse;width:100%;margin:10px 0 20px;font-size:14.5px}
table.keys td{border-bottom:1px solid var(--line);padding:7px 8px;color:var(--sub);vertical-align:top}
table.keys td:first-child{width:220px;color:var(--txt)}
.tip{background:rgba(201,163,90,.08);border-left:3px solid var(--gold);padding:10px 14px;border-radius:3px;margin:14px 0;color:var(--sub)}
.doc .shot{margin:16px 0}
@media(max-width:900px){.doc{grid-template-columns:1fr}.toc{position:static;max-height:none}}
'''
def t(sk,en): return f'<span lang="sk">{sk}</span><span lang="en">{en}</span>'
def K(*keys): return ' + '.join(f'<kbd>{k}</kbd>' for k in keys)
def KS(*keys): return ' '.join(f'<kbd>{k}</kbd>' for k in keys)
sections=[]
def sec(id,sk,en,body): sections.append((id,sk,en,body))
def P(sk,en): return f'<p>{t(sk,en)}</p>'
def H(sk,en): return f'<h3>{t(sk,en)}</h3>'
def UL(items): return '<ul>'+''.join(f'<li>{t(a,b)}</li>' for a,b in items)+'</ul>'
def OL(items): return '<ol>'+''.join(f'<li>{t(a,b)}</li>' for a,b in items)+'</ol>'
def TIP(sk,en): return f'<div class="tip">{t(sk,en)}</div>'
def KEYS(rows): return '<table class="keys">'+''.join(f'<tr><td>{k}</td><td>{t(a,b)}</td></tr>' for k,a,b in rows)+'</table>'
def IMG(n,alt): return f'<div class="shot"><img lang="sk" src="img/{n}_sk.jpg" alt="{alt}"><img lang="en" src="img/{n}_en.jpg" alt="{alt}"></div>'

sec('instalacia','Inštalácia a aktualizácie','Installation and updates',
 H('Inštalácia','Installing')+
 OL([('Stiahni si <b>KorgusPlayer-Setup.exe</b> zo stránky cami.sk/korgusplayer.','Download <b>KorgusPlayer-Setup.exe</b> from cami.sk/korgusplayer.'),
     ('Spusti ho. Inštalácia nepotrebuje administrátora – program sa nainštaluje len pre teba do priečinka <i>AppData\\Local\\Programs\\Korgus Player</i>.','Run it. No admin rights are needed – the program installs just for you into <i>AppData\\Local\\Programs\\Korgus Player</i>.'),
     ('Ak chceš, nechaj vytvoriť ikonu na ploche. Program nájdeš aj v ponuke Štart.','Optionally create a desktop icon. The program is also in the Start menu.')])+
 P('Ak sa Windows SmartScreen spýta, či program spustiť, klikni na <b>Ďalšie informácie → Spustiť aj tak</b>. Program je nový a ešte nemá „reputáciu“.','If Windows SmartScreen asks whether to run it, click <b>More info → Run anyway</b>. The program is new and has no “reputation” yet.')+
 H('Bez inštalácie','Without installing')+
 P('Môžeš si stiahnuť aj samostatný súbor <b>Korgus Player.exe</b> a spustiť ho odkiaľkoľvek (aj z USB kľúča). Na prehrávanie videa vtedy program potrebuje knižnicu <i>libmpv-2.dll</i> – stiahneš ju jedným klikom cez <b>☰ → Stiahnuť prehrávač videa</b>.','You can also download the standalone <b>Korgus Player.exe</b> and run it from anywhere (even a USB stick). For video playback it then needs the <i>libmpv-2.dll</i> library – download it with one click via <b>☰ → Download video player</b>.')+
 H('Aktualizácie','Updates')+
 P('Pri spustení program potichu zistí, či je nová verzia. Ak áno, ponúkne <b>Aktualizovať</b>, <b>Neskôr</b> alebo <b>Preskočiť túto verziu</b>. Po kliknutí na Aktualizovať sa nová verzia stiahne, nahradí starú a program sa sám znova spustí. Ručne: <b>☰ → Skontrolovať aktualizácie</b>. Automatickú kontrolu vypneš v tom istom menu.','At start-up the program quietly checks for a new version and offers <b>Update</b>, <b>Later</b> or <b>Skip this version</b>. Update downloads the new version, replaces the old one and restarts. Manually: <b>☰ → Check for updates</b>. Turn automatic checks off in the same menu.')+
 H('Odinštalovanie','Uninstalling')+
 P('Cez <b>Nastavenia Windows → Aplikácie</b> ako každý iný program. Tvoje súbory sa nezmenia.','Via <b>Windows Settings → Apps</b> like any other program. Your files are not touched.'))

sec('okno','Okno programu','The main window',
 IMG('grid','')+
 UL([('<b>Horná lišta</b> – šípka ↑ o priečinok vyššie, cesta k priečinku, pole <b>Hľadať</b>, filter typu súborov (Všetko, Obrázky, Zvuk…), triedenie, <b>Prezentácia</b>, prepínač <b>Mriežka/Zoznam</b>, <b>◫</b> dva panely a <b>☰</b> nastavenia.','<b>Top bar</b> – ↑ up one folder, folder path, <b>Search</b>, file type filter (All, Pictures, Audio…), sorting, <b>Slideshow</b>, <b>Grid/List</b> switch, <b>◫</b> two panels and <b>☰</b> settings.'),
     ('<b>Strom priečinkov</b> vľavo – Plocha, Dokumenty, Obrázky, Hudba, Videá, Stiahnuté, disky a tvoje <b>obľúbené priečinky</b>.','<b>Folder tree</b> on the left – Desktop, Documents, Pictures, Music, Videos, Downloads, drives and your <b>favourite folders</b>.'),
     ('<b>Karty</b> nad súbormi – viac priečinkov otvorených naraz.','<b>Tabs</b> above the files – several folders open at once.'),
     ('<b>Stred</b> – súbory ako mriežka náhľadov alebo zoznam (pri zvukoch s vlnou, dĺžkou, BPM a tóninou).','<b>Centre</b> – files as a thumbnail grid or a list (for audio with waveform, length, BPM and key).'),
     ('<b>Náhľad</b> vpravo – obrázok, vlna zvuku, video, PDF, text, 3D model… Pod ním údaje o súbore, hodnotenie, farebný štítok a <b>čím sa dá súbor otvoriť</b>.','<b>Preview</b> on the right – image, waveform, video, PDF, text, 3D model… Below it file details, rating, colour label and <b>which programs open the file</b>.'),
     ('<b>Prehrávač</b> – ⏮ ▶ ⏹ ⏭, priebeh skladby, opakovanie, náhodné poradie, <b>♩ tempo</b> a hlasitosť.','<b>Player</b> – ⏮ ▶ ⏹ ⏭, progress, repeat, shuffle, <b>♩ tempo</b> and volume.'),
     ('<b>Spodná lišta</b> – správy programu, tlačidlo <b>?</b> a cieľové priečinky <b>1–9</b> na triedenie.','<b>Bottom bar</b> – program messages, the <b>?</b> button and target folders <b>1–9</b> for sorting.')])+
 P('Šírku stromu a náhľadu zmeníš ťahaním deliacich čiar.','Drag the divider lines to change the width of the tree and the preview.'))

sec('prechadzanie','Prechádzanie a hľadanie','Browsing and searching',
 KEYS([(KS('←','→','↑','↓'),'pohyb po súboroch','move between files'),
       (K('Enter'),'otvoriť priečinok, obrázok na celú obrazovku, video na celú obrazovku','open a folder, image full screen, video full screen'),
       (K('Backspace')+' / '+K('Alt','↑'),'o priečinok vyššie','up one folder'),
       (K('Home')+' / '+K('End'),'prvý / posledný súbor','first / last file'),
       (K('V'),'mriežka / zoznam','grid / list'),
       (K('Ctrl','A'),'označiť všetko','mark all'),
       ('Ctrl / Shift + '+t('klik','click'),'označiť viac súborov','mark several files'),
       (K('Esc'),'zrušiť označenie','clear marks')])+
 P('Priečinky so zvukmi sa samy otvoria ako zoznam, priečinky s fotkami ako mriežka.','Folders with audio open as a list automatically, folders with photos as a grid.')+
 H('Hľadanie','Search')+
 UL([('Píš do poľa <b>Hľadať</b> ('+K('Ctrl','F')+') – zoznam sa hneď filtruje v aktuálnom priečinku. Na diakritike ani veľkých písmenách nezáleží, viac slov = musia sedieť všetky.','Type into <b>Search</b> ('+K('Ctrl','F')+') – the current folder filters instantly. Accents and case don\'t matter; several words must all match.'),
     ('Stlač '+K('Enter')+' a program prehľadá <b>aj všetky podpriečinky</b> (napr. „kick 124“ v celej knižnici samplov). Výsledky môžeš prehrávať, triediť aj presúvať. '+K('Esc')+' alebo '+K('Backspace')+' ťa vráti do priečinka.','Press '+K('Enter')+' to search <b>all subfolders</b> too (e.g. “kick 124” across your whole sample library). Results can be played, sorted and moved. '+K('Esc')+' or '+K('Backspace')+' returns to the folder.'),
     ('Filter typu vedľa hľadania zobrazí len obrázky, zvuky, videá, dokumenty, 3D a CAD, priečinky, súbory s hodnotením alebo so štítkom.','The type filter next to search shows only images, audio, video, documents, 3D and CAD, folders, rated or labelled files.')])+
 H('Triedenie','Sorting')+
 P('Názov A–Z / Z–A, najnovšie, najstaršie, <b>dátum fotenia</b> (z fotoaparátu), najväčšie, typ súboru. V zozname klikni na nadpis stĺpca.','Name A–Z / Z–A, newest, oldest, <b>date taken</b> (from the camera), largest, file type. In the list, click a column header.')+
 H('Obľúbené priečinky a karty','Favourite folders and tabs')+
 UL([('Pravý klik na priečinok v strome → <b>Pridať do obľúbených ★</b>. Obľúbené sú hore v strome.','Right-click a folder in the tree → <b>Add to favourites ★</b>. Favourites are at the top of the tree.'),
     (K('Ctrl','T')+' alebo <b>+</b> otvorí novú kartu, '+K('Ctrl','W')+' alebo × ju zavrie, '+K('Ctrl','Tab')+' prepína. Stredné tlačidlo myši na karte ju tiež zavrie. Karty si program pamätá.',K('Ctrl','T')+' or <b>+</b> opens a new tab, '+K('Ctrl','W')+' or × closes it, '+K('Ctrl','Tab')+' switches. Middle-clicking a tab closes it too. Tabs are remembered.')]))

sec('obrazky','Obrázky a fotky','Images and photos',
 P('Podporované: JPG, PNG, GIF, WebP, BMP, TIFF, HEIC (s rozšírením z Microsoft Store), AVIF, RAW z fotoaparátov, PSD (náhľad), SVG, EPS/AI (vložený náhľad), ICO, EMF/WMF.','Supported: JPG, PNG, GIF, WebP, BMP, TIFF, HEIC (with the Microsoft Store extension), AVIF, camera RAW, PSD (preview), SVG, EPS/AI (embedded preview), ICO, EMF/WMF.')+
 H('Na celú obrazovku','Full screen')+
 KEYS([(K('Enter')+' / '+K('F11'),'otvoriť / zavrieť','open / close'),
       (KS('←','→'),'predošlá / ďalšia fotka','previous / next photo'),
       (t('koliesko myši','mouse wheel')+', '+KS('+','−'),'priblížiť / oddialiť (ťahaním posúvaš)','zoom in / out (drag to pan)'),
       (K('0'),'celý obrázok','fit to screen'),
       (K('F'),'veľkosť: celý / vyplniť obrazovku / pôvodná','size: fit / fill screen / original'),
       (K('1')+'…'+K('9'),'presunúť fotku do cieľového priečinka','move the photo to a target folder')])+
 H('Prezentácia','Slideshow')+
 P('Tlačidlo <b>Prezentácia</b> alebo '+K('F5')+' na celej obrazovke. Interval nastavíš šípkami ◀ ▶ (alebo '+KS('↑','↓')+', '+KS('+','−')+'), náhodné poradie a hudbu pod prezentáciu tlačidlom <b>Hudba</b> (vyberieš priečinok s hudbou).','The <b>Slideshow</b> button or '+K('F5')+' in full screen. Set the interval with ◀ ▶ (or '+KS('↑','↓')+', '+KS('+','−')+'), shuffle, and music with the <b>Music</b> button (pick a music folder).')+
 H('Úpravy','Editing')+
 UL([('<b>Otočiť</b> – '+K('R')+' doprava, '+K('L')+' doľava (aj tlačidlami nad náhľadom). JPG sa otáča <b>bez straty kvality</b>.','<b>Rotate</b> – '+K('R')+' right, '+K('L')+' left (also with the buttons above the preview). JPG rotates <b>losslessly</b>.'),
     ('<b>Zmeniť veľkosť</b> – '+K('Ctrl','R')+'. Označ viac fotiek a zmenši ich naraz: kópia vedľa originálu, do podpriečinka „zmensene“, do vybraného priečinka, alebo prepísať.','<b>Resize</b> – '+K('Ctrl','R')+'. Mark several photos and resize them at once: copy next to the original, into a “zmensene” subfolder, into a chosen folder, or overwrite.'),
     ('<b>Upraviť</b> – '+K('E')+': orezanie (voľné alebo 1:1, 4:3, 16:9…), jas, kontrast, sýtosť. Ulož ako kópiu alebo prepíš originál.','<b>Edit</b> – '+K('E')+': crop (free or 1:1, 4:3, 16:9…), brightness, contrast, saturation. Save as a copy or overwrite.')])+
 H('Porovnanie a podobné fotky','Compare and similar photos')+
 UL([(K('C')+' – dve fotky vedľa seba so spoločným priblížením. '+KS('←','→')+' mení pravú fotku, '+K('1')+' nechá ľavú, '+K('2')+' pravú (druhá sa označí na zmazanie alebo presun).',K('C')+' – two photos side by side with shared zoom. '+KS('←','→')+' changes the right photo, '+K('1')+' keeps the left, '+K('2')+' the right (the other gets marked for deleting or moving).'),
     ('Pravý klik → <b>Nájsť podobné fotky</b> – program zoskupí série skoro rovnakých záberov. V každej sérii nechá jednu a ostatné označí – stačí '+K('Del')+'.','Right-click → <b>Find similar photos</b> – groups series of near-identical shots, keeps one per series and marks the rest – just press '+K('Del')+'.'),
     ('Pravý klik → <b>Nájsť duplikáty v priečinku</b> – úplne rovnaké súbory (aj s iným menom).','Right-click → <b>Find duplicates in folder</b> – identical files (even with different names).')])+
 H('Údaje z fotoaparátu a mapa','Camera data and map')+
 P('Pod náhľadom fotky vidíš fotoaparát, objektív, expozíciu a dátum fotenia. Ak má fotka GPS, tlačidlo <b>Ukázať na mape</b> otvorí miesto na mape.','Below the photo preview you see the camera, lens, exposure and date taken. If the photo has GPS, <b>Show on map</b> opens the location.')+
 H('Import z karty a fotoaparátu','Importing from a card or camera')+
 P('Keď pripojíš kartu alebo fotoaparát (priečinok DCIM), program to oznámi. <b>☰ → Importovať fotky</b>: vyber odkiaľ, kam a ako pomenovať priečinky (napr. <i>2026\\2026-11-18 Dovolenka</i>). Fotky a videá sa skopírujú podľa dátumu fotenia, na karte ostanú a pri ďalšom importe sa už skopírované preskočia. Z telefónu: vyber jeho priečinok DCIM cez „Vybrať priečinok“.','When you connect a card or camera (a DCIM folder), the program tells you. <b>☰ → Import photos</b>: pick the source, the target and how folders are named (e.g. <i>2026\\2026-11-18 Holiday</i>). Photos and videos are copied by date taken, stay on the card, and files already imported are skipped next time. From a phone: choose its DCIM folder via “Choose a folder”.')+
 H('Video z fotiek','Video from photos')+
 P('Pravý klik → <b>Vytvoriť video z fotiek</b>. Vyber dĺžku jednej fotky a hudbu. Vznikne MP4 1920×1080 s pomalým približovaním a prelínaním – hotové na poslanie alebo na Facebook. Použijú sa označené fotky, inak všetky v priečinku.','Right-click → <b>Create a video from photos</b>. Choose the time per photo and music. You get a 1920×1080 MP4 with slow zoom and crossfades – ready to share. Marked photos are used, otherwise all photos in the folder.'))

sec('hudba','Hudba a samply','Music and samples',
 IMG('list','')+
 P('Podporované: MP3, WAV, W64, FLAC, AIFF/AIFC, OGG, Opus, M4A/AAC, WMA, REX (.rx2/.rex/.rcy) a MIDI.','Supported: MP3, WAV, W64, FLAC, AIFF/AIFC, OGG, Opus, M4A/AAC, WMA, REX (.rx2/.rex/.rcy) and MIDI.')+
 UL([('Klik na zvuk ho prehrá. Hudba hrá ďalej, aj keď prezeráš iné súbory. '+K('Medzerník')+' = prehrať / pauza, klik do vlny = skok.','Clicking a sound plays it. Music keeps playing while you browse other files. '+K('Space')+' = play / pause, click the waveform to jump.'),
     (K('Ctrl','←')+' / '+K('Ctrl','→')+' predošlá / ďalšia skladba, fungujú aj multimediálne klávesy.',K('Ctrl','←')+' / '+K('Ctrl','→')+' previous / next track, media keys work too.'),
     (K('Q')+' pridá skladbu do <b>fronty</b> – zahrá sa ako ďalšia.',K('Q')+' adds a track to the <b>queue</b> – it plays next.'),
     ('<b>Opakovanie</b>: vypnuté / celý zoznam / jedna skladba. <b>Náhodné poradie</b> vedľa neho.','<b>Repeat</b>: off / whole list / one track. <b>Shuffle</b> next to it.'),
     ('Pri zvukoch sa zobrazí <b>obal albumu</b>, interpret a názov skladby.','For music you see the <b>album cover</b>, artist and title.')])+
 H('BPM, tónina a tempo','BPM, key and tempo')+
 P('Pri samploch a loopoch program zistí <b>tempo (BPM)</b> a <b>tóninu</b> (napr. „124 BPM · a mol“). Tlačidlo <b>♩ Tempo</b> v prehrávači nastaví tempo tvojej skladby – krátke loopy so známym BPM sa potom prehrajú v ňom (mení sa aj výška, ako „re-pitch“ v DAW).','For samples and loops the program detects <b>tempo (BPM)</b> and <b>key</b> (e.g. “124 BPM · A minor”). The <b>♩ Tempo</b> button sets your song tempo – short loops with known BPM then play at it (pitch changes too, like “re-pitch” in a DAW).')+
 H('Výrez zo samplu','Cutting a part of a sample')+
 P('V náhľade <b>potiahni myšou po vlne</b> – vyberieš úsek, ktorý sa hneď hrá dokola (tlačidlo <b>⟳ Dokola</b> to vypne). <b>Uložiť výrez</b> ho uloží ako nový WAV vedľa pôvodného súboru (s jemným prelínaním na okrajoch, aby necvakal). Klik do vlny výber zruší.','In the preview <b>drag across the waveform</b> to select a part that loops right away (the <b>⟳ Loop</b> button turns it off). <b>Save selection</b> stores it as a new WAV next to the original (with tiny fades so it doesn\'t click). A click on the waveform clears the selection.')+
 H('Zvuková karta a ASIO','Sound card and ASIO')+
 P('<b>☰ → Zvukový výstup</b> – vyber zvukovú kartu, ovládač (aj <b>ASIO</b> pre nízke oneskorenie), výstupné kanály, vzorkovaciu frekvenciu a veľkosť bufferu. Výber si program pamätá.','<b>☰ → Audio output</b> – choose the sound card, driver (including <b>ASIO</b> for low latency), output channels, sample rate and buffer size. The choice is remembered.')+
 H('Podobné zvuky','Similar sounds')+
 P('Pravý klik na zvuk → <b>Nájsť podobné zvuky ako tento</b>. Program prehľadá priečinok aj podpriečinky a ukáže 40 najpodobnejších (podľa nástupu, dĺžky, farby zvuku a basov) – ideálne na hľadanie náhrady za kick či snare.','Right-click a sound → <b>Find sounds similar to this one</b>. The folder and subfolders are searched and the 40 most similar are listed (by attack, length, tone and bass) – ideal for finding an alternative kick or snare.')+
 H('Do DAW a do Korgus Sequencera','Into a DAW and Korgus Sequencer')+
 P('Sample alebo MIDI <b>pretiahni myšou</b> zo zoznamu priamo do Cubase, Abletonu, FL Studia alebo na stopu v <b>Korgus Sequenceri</b>. Pravý klik → <b>Otvoriť Korgus Sequencer</b> ho spustí.','<b>Drag</b> a sample or MIDI from the list straight into Cubase, Ableton, FL Studio or onto a track in <b>Korgus Sequencer</b>. Right-click → <b>Open Korgus Sequencer</b> launches it.')+
 H('Export zoznamu','Exporting a list')+
 P('Pravý klik → <b>Exportovať zoznam priečinka do tabuľky (CSV)</b> – názov, typ, veľkosť, dĺžka, BPM, tónina, formát, hodnotenie a dátum. Otvoríš v Exceli alebo LibreOffice.','Right-click → <b>Export folder list to a spreadsheet (CSV)</b> – name, type, size, length, BPM, key, format, rating and date. Opens in Excel or LibreOffice.')+
 H('MIDI','MIDI')+
 P('MIDI súbory sa prehrajú cez syntetizátor Windows (alebo tvoj MIDI výstup) a v náhľade uvidíš noty ako piano roll.','MIDI files play through the Windows synth (or your MIDI output) and the preview shows the notes as a piano roll.'))

sec('video','Video a filmy','Video and films',
 P('MP4, MKV, AVI, MOV, WMV, WebM, M2TS/MTS (kamery, Blu-ray), aj 4K a HEVC.','MP4, MKV, AVI, MOV, WMV, WebM, M2TS/MTS (camcorders, Blu-ray), 4K and HEVC included.')+
 KEYS([(K('Medzerník'),'pauza','pause'),(K('Enter'),'na celú obrazovku (pokračuje od toho istého miesta)','full screen (continues from the same point)'),
       (KS('←','→'),'posun o 5 s (so '+K('Shift')+' o 30 s)','seek 5 s (with '+K('Shift')+' 30 s)'),
       (KS('↑','↓'),'hlasitosť (na celej obrazovke)','volume (in full screen)'),(K('Esc'),'zavrieť celú obrazovku','close full screen')])+
 UL([('<b>Titulky / zvuk</b> – výber jazyka zvuku a titulkov. Titulky .srt s rovnakým menom ako film sa načítajú samy, iné pridáš cez „Načítať titulky zo súboru“.','<b>Subtitles / audio</b> – choose the audio language and subtitles. An .srt with the same name as the film loads automatically; add others via “Load subtitles from file”.'),
     ('Pri dlhších videách si program pamätá, <b>kde si skončil</b>, a nabudúce pokračuje odtiaľ. Klik na začiatok lišty = od začiatku.','For longer videos the program remembers <b>where you stopped</b> and continues from there next time. Click the start of the bar = from the beginning.'),
     ('Ikona <b>fotoaparátu</b> uloží aktuálny záber ako PNG vedľa videa.','The <b>camera</b> icon saves the current frame as a PNG next to the video.')]))

sec('dokumenty','Dokumenty, 3D, CAD a ďalšie','Documents, 3D, CAD and more',
 UL([('<b>PDF</b> – listovanie stránok '+KS('↑','↓')+', na celú obrazovku cez '+K('Enter')+'.','<b>PDF</b> – page through with '+KS('↑','↓')+', full screen with '+K('Enter')+'.'),
     ('<b>Texty a kód</b> (TXT, MD, PHP, CSS, JS, C++…), <b>tabuľky</b> (CSV, XLSX, ODS), <b>Word, PowerPoint</b> – rýchly náhľad bez otvárania programov.','<b>Text and code</b> (TXT, MD, PHP, CSS, JS, C++…), <b>spreadsheets</b> (CSV, XLSX, ODS), <b>Word, PowerPoint</b> – a quick look without opening other programs.'),
     ('<b>3D modely</b> STL, 3MF, OBJ – ťahaním otáčaš, pravým tlačidlom posúvaš, kolieskom približuješ.','<b>3D models</b> STL, 3MF, OBJ – drag to rotate, right button to pan, wheel to zoom.'),
     ('<b>Výkresy</b> DXF (celý výkres s vrstvami) a DWG (náhľad uložený vo výkrese).','<b>Drawings</b> DXF (full drawing with layers) and DWG (preview stored in the drawing).'),
     ('<b>Fonty</b> TTF/OTF – ukážka písma vo viacerých veľkostiach.','<b>Fonts</b> TTF/OTF – sample text in several sizes.'),
     ('<b>Programy</b> (EXE, MSI, DLL) – výrobca, verzia a popis.','<b>Programs</b> (EXE, MSI, DLL) – publisher, version and description.')])+
 H('Čím to otvoriť?','What opens it?')+
 P('Pod každým súborom vidíš, čo je to za formát a ktoré programy ho vedia otvoriť alebo upraviť – zvlášť bezplatné a zvlášť platené. Klik na názov programu ho skopíruje, pravý klik ho vyhľadá na webe. Tlačidlo <b>Otvoriť v…</b> ponúkne programy nainštalované vo Windows.','Under every file you see what the format is and which programs open or edit it – free and paid listed separately. Click a program name to copy it, right-click to search for it on the web. <b>Open with…</b> lists programs installed on Windows.'))

sec('organizovanie','Triedenie a upratovanie','Sorting and tidying up',
 H('Cieľové priečinky 1–9','Target folders 1–9')+
 OL([('Klikni dole na políčko (napr. <b>2</b>) a vyber priečinok – alebo otvor priečinok a stlač '+K('Ctrl','2')+'.','Click a slot at the bottom (e.g. <b>2</b>) and choose a folder – or open a folder and press '+K('Ctrl','2')+'.'),
     ('Pri prezeraní stlač '+K('2')+' – súbor (alebo všetky označené) sa hneď presunie do cieľa 2.','While browsing press '+K('2')+' – the file (or all marked files) moves to target 2 at once.'),
     ('Pomýlil si sa? '+K('Ctrl','Z')+' vráti posledný presun – aj viackrát.','Mistake? '+K('Ctrl','Z')+' undoes the last move – repeatedly too.'),
     ('Pravý klik na políčko cieľ zruší. Tlačidlo <b>?</b> vedľa ukáže návod.','Right-click a slot to clear it. The <b>?</b> button next to them shows help.')])+
 TIP('Príklad: fotky z dovolenky – 1 = Dobré, 2 = Na tlač, 3 = Zmazať. Listuješ šípkami (aj na celej obrazovke) a len stláčaš 1, 2, 3.','Example: holiday photos – 1 = Good, 2 = To print, 3 = Delete. Browse with the arrows (even full screen) and just press 1, 2, 3.')+
 H('Dva panely','Two panels')+
 P('Tlačidlo <b>◫</b> alebo '+K('F9')+'. Namiesto náhľadu sa zobrazí druhý priečinok. '+K('Tab')+' prepína panel (aktívny má zlatú čiaru), '+K('F5')+' <b>kopíruje</b> a '+K('F6')+' <b>presúva</b> označené súbory do druhého panela. Veľké súbory sa kopírujú na pozadí s percentami, '+K('Esc')+' kopírovanie zruší.','The <b>◫</b> button or '+K('F9')+'. A second folder replaces the preview. '+K('Tab')+' switches panels (the active one has a gold line), '+K('F5')+' <b>copies</b> and '+K('F6')+' <b>moves</b> marked files to the other panel. Large files copy in the background with a percentage, '+K('Esc')+' cancels.')+
 H('Premenovanie a mazanie','Renaming and deleting')+
 UL([(K('F2')+' premenovať, '+K('Del')+' do Koša (obnovíš ho z Koša vo Windows).',K('F2')+' rename, '+K('Del')+' to the Recycle Bin (restore from the Windows Recycle Bin).'),
     (K('Ctrl','F2')+' <b>hromadné premenovanie</b> označených súborov podľa vzoru, napr. <code>Dovolenka_{datum}_{cislo}</code>. Značky: {datum}, {cas} (dátum a čas fotenia), {cislo}, {nazov} (pôvodné meno), {priecinok}, {bpm} a {tonina} (pri zvukoch, napr. <code>{nazov}_{bpm}bpm_{tonina}</code> → loop_124bpm_Am.wav). Pred potvrdením vidíš ukážku, '+K('Ctrl','Z')+' vráti.',K('Ctrl','F2')+' <b>batch rename</b> of marked files by a pattern, e.g. <code>Holiday_{datum}_{cislo}</code>. Tokens: {datum} date, {cas} time (taken), {cislo} number, {nazov} original name, {priecinok} folder, {bpm} and {tonina} key (for audio, e.g. <code>{nazov}_{bpm}bpm_{tonina}</code> → loop_124bpm_Am.wav). You see a preview first, '+K('Ctrl','Z')+' undoes.')])+
 H('Hodnotenie a farebné štítky','Ratings and colour labels')+
 P(K('Alt','1')+'…'+K('Alt','5')+' hviezdičky, '+K('Alt','0')+' zruší. Farebný štítok cez pravý klik alebo kliknutím na bodky pod náhľadom. Filter <b>S hodnotením ★</b> / <b>So štítkom</b> ich potom ukáže. Hodnotenia sa ukladajú bokom, súbory sa nemenia.',K('Alt','1')+'…'+K('Alt','5')+' stars, '+K('Alt','0')+' clears. Colour labels via right-click or the dots under the preview. The <b>Rated ★</b> / <b>With label</b> filter then shows them. Ratings are stored separately, files are not changed.'))

sec('tlac','Tlač a prevod formátov','Printing and conversion',
 H('Tlač','Printing')+
 UL([(K('Ctrl','P')+' alebo pravý klik → <b>Tlačiť</b>. Pri fotkách sa otvorí výber tlačiarne a každá fotka ide na celú stranu (otočí sa podľa papiera). Viac označených fotiek = viac strán.',K('Ctrl','P')+' or right-click → <b>Print</b>. For photos you choose the printer and each photo fills a page (rotated to fit the paper). Several marked photos = several pages.'),
     ('Texty (TXT, kód, CSV) vytlačí Korgus Player sám – písmo Consolas, okraje 15 mm, čísla strán. SVG ako obrázok.','Text files (TXT, code, CSV) are printed by Korgus Player itself – Consolas font, 15 mm margins, page numbers. SVG as an image.'),
     ('PDF a ostatné súbory sa tlačia cez program, ktorý ich vo Windows otvára.','PDF and other files print through the program that opens them in Windows.')])+
 H('Prevod do iného formátu','Converting to another format')+
 P('Označ súbory → pravý klik → <b>Previesť do iného formátu</b>. Fotky do JPG alebo PNG (napr. HEIC z iPhonu → JPG), zvuky do WAV 16/24 bit, FLAC, OGG, MP3 320 kbps alebo M4A. Nové súbory sa uložia vedľa originálov.','Mark files → right-click → <b>Convert to another format</b>. Photos to JPG or PNG (e.g. iPhone HEIC → JPG), audio to WAV 16/24-bit, FLAC, OGG, MP3 320 kbps or M4A. New files are saved next to the originals.'))

sec('nastavenia','Nastavenia (☰)','Settings (☰)',
 UL([('<b>Tmavá / svetlá téma</b>.','<b>Dark / light theme</b>.'),
     ('<b>Jazyk</b> – slovenčina alebo angličtina.','<b>Language</b> – Slovak or English.'),
     ('<b>„Otvoriť v Korgus Player“ v Prieskumníkovi</b> – po zapnutí stačí pravý klik na priečinok alebo súbor v Prieskumníkovi.','<b>“Open in Korgus Player” in Explorer</b> – once enabled, just right-click a folder or file in Explorer.'),
     ('<b>Importovať fotky z karty / fotoaparátu</b>.','<b>Import photos from a card / camera</b>.'),
     ('<b>Zvukový výstup</b> – zvuková karta a ASIO.','<b>Audio output</b> – sound card and ASIO.'),
     ('<b>Stiahnuť prehrávač videa</b> – len ak chýba (verzia bez inštalácie).','<b>Download video player</b> – only if it is missing (portable version).'),
     ('<b>Aktualizácie</b> – skontrolovať teraz a zapnúť / vypnúť kontrolu pri spustení.','<b>Updates</b> – check now and turn the start-up check on / off.')]))

sec('skratky','Všetky klávesové skratky','All keyboard shortcuts',
 KEYS([(K('Enter'),'otvoriť / celá obrazovka','open / full screen'),(K('Backspace'),'o priečinok vyššie','up one folder'),(K('V'),'mriežka / zoznam','grid / list'),
       (K('Ctrl','F'),'hľadať (Enter = aj podpriečinky)','search (Enter = include subfolders)'),(K('Ctrl','T')+' '+K('Ctrl','W')+' '+K('Ctrl','Tab'),'karty','tabs'),
       (K('Medzerník'),'prehrať / pauza','play / pause'),(K('Ctrl','←')+' '+K('Ctrl','→'),'predošlá / ďalšia skladba','previous / next track'),(K('Q'),'do fronty','queue'),
       (K('1')+'…'+K('9'),'presunúť do cieľa','move to target'),(K('Ctrl','1')+'…'+K('Ctrl','9'),'nastaviť cieľ','set target'),(K('Ctrl','Z'),'späť','undo'),
       (K('F2')+' / '+K('Ctrl','F2'),'premenovať / hromadne','rename / batch'),(K('Del'),'do Koša','to Recycle Bin'),
       (K('R')+' / '+K('L'),'otočiť doprava / doľava','rotate right / left'),(K('Ctrl','R'),'zmeniť veľkosť','resize'),(K('E'),'upraviť fotku','edit photo'),(K('C'),'porovnať fotky','compare photos'),
       (K('Ctrl','P'),'tlačiť','print'),(K('Alt','1')+'…'+K('Alt','5')+', '+K('Alt','0'),'hodnotenie','rating'),
       (K('F5'),'prezentácia (v dvoch paneloch kopírovať)','slideshow (copy in two-panel mode)'),(K('F6'),'presunúť do druhého panela','move to the other panel'),(K('F9'),'dva panely','two panels'),
       (K('Tab'),'prepnúť panel','switch panel'),(K('F11'),'celá obrazovka','full screen'),(K('Esc'),'zrušiť / späť','cancel / back')]))

sec('problemy','Keď niečo nejde','Troubleshooting',
 UL([('<b>Video sa neprehrá</b> – chýba prehrávač videa: ☰ → Stiahnuť prehrávač videa. Iný program na video netreba.','<b>Video won\'t play</b> – the video player is missing: ☰ → Download video player. No other program is needed.'),
     ('<b>HEIC fotky z iPhonu sa neukážu</b> – nainštaluj z Microsoft Store bezplatné rozšírenie <i>HEIF Image Extensions</i>.','<b>iPhone HEIC photos don\'t show</b> – install the free <i>HEIF Image Extensions</i> from the Microsoft Store.'),
     ('<b>Aktualizácia sa nepodarí</b> – program je asi v priečinku, kam sa nedá zapisovať (napr. Program Files). Nainštaluj ho inštalátorom alebo ho presuň napr. na plochu.','<b>Update fails</b> – the program is probably in a folder that can\'t be written to (e.g. Program Files). Use the installer or move it e.g. to the desktop.'),
     ('<b>Súbor sa nedá presunúť / premenovať</b> – je otvorený v inom programe. Zavri ho a skús znova.','<b>A file can\'t be moved / renamed</b> – it is open in another program. Close it and try again.'),
     ('<b>Kde sú nastavenia</b> – <i>%APPDATA%\\Korgus Player</i>. Náhľady sú v <i>%LOCALAPPDATA%\\Korgus Player\\Nahlady</i> a dajú sa kedykoľvek zmazať.','<b>Where are the settings</b> – <i>%APPDATA%\\Korgus Player</i>. Thumbnails are in <i>%LOCALAPPDATA%\\Korgus Player\\Nahlady</i> and can be deleted anytime.')])+
 P('Nápad alebo chyba? Napíš cez stránku cami.sk.','An idea or a bug? Get in touch via cami.sk.'))

toc=''.join(f'<a href="#{i}">{t(a,b)}</a>' for i,a,b,_ in sections)
body=''.join(f'<h2 id="{i}">{t(a,b)}</h2>{c}' for i,a,b,c in sections)
DL="https://github.com/Korgus7/Korgus-Download/raw/main/player/KorgusPlayer-Setup.exe"
html=f'''<!DOCTYPE html>
<html lang="sk" data-lang="sk">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Návod – Korgus Player</title>
<meta name="description" content="Kompletný návod ku Korgus Player – prehliadač obrázkov, hudby, samplov, videí a dokumentov pre Windows. / Complete user guide.">
<link rel="icon" href="img/icon.png">
<style>{style}{extra}</style>
</head>
<body>
<header><div class="wrap">
  <a class="logo" href="index.html"><img src="img/icon.png" alt=""><span class="lt">KORGUS PLAYER</span></a>
  <nav><a href="index.html">{t('Úvod','Home')}</a><a href="#skratky">{t('Skratky','Shortcuts')}</a><a href="#problemy">{t('Keď niečo nejde','Troubleshooting')}</a></nav>
  <span class="sp"></span>
  <div class="lang"><button data-l="sk">SK</button><button data-l="en">EN</button></div>
  <a class="btn sm dl" href="{DL}">{t('Stiahnuť zadarmo','Free download')}</a>
</div></header>
<div class="wrap">
  <div style="padding:44px 0 0"><h1 style="font-size:40px;margin:0 0 8px">{t('Návod na Korgus Player','Korgus Player user guide')}</h1>
  <p style="color:var(--sub);margin:0">{t('Všetko, čo program vie – od prvého spustenia po klávesové skratky.','Everything the program can do – from the first start to keyboard shortcuts.')}</p></div>
  <div class="doc"><aside class="toc">{toc}</aside><main>{body}</main></div>
</div>
<footer><div class="wrap"><img src="img/icon.png" alt="" width="20" height="20" style="border-radius:3px"><span>© 2026 Korgus</span><span class="sp"></span>{t('Vyrobené na Slovensku','Made in Slovakia')}</div></footer>
<script>
(function(){{
  var h=document.documentElement;
  function set(l){{ h.setAttribute('data-lang',l); h.lang=l; document.title=l==='en'?'User guide – Korgus Player':'Návod – Korgus Player';
    document.querySelectorAll('.lang button').forEach(function(b){{ b.classList.toggle('on',b.dataset.l===l); }}); try{{ localStorage.setItem('korgusWebLang',l); }}catch(e){{}} }}
  var l=null; try{{ l=localStorage.getItem('korgusWebLang'); }}catch(e){{}}
  if(!l){{ var n=(navigator.language||'').toLowerCase(); l=(n.indexOf('sk')===0||n.indexOf('cs')===0)?'sk':'en'; }}
  if(/[?&]lang=(sk|en)/.test(location.search)) l=RegExp.$1;
  set(l);
  document.querySelectorAll('.lang button').forEach(function(b){{ b.onclick=function(){{ set(b.dataset.l); }}; }});
}})();
</script>
</body>
</html>
'''
open('/home/claude/korgus-download/player/navod.html','w',encoding='utf-8').write(html)
print(len(html))
