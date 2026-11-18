#include "FormatInfo.h"
#include <map>

namespace kp
{
namespace
{
    struct Entry { const char* exts; const char* name; const char* freeApps; const char* paidApps; const char* note; };

    // Prípony oddelené medzerou. Texty v UTF-8.
    const Entry entries[] = {
        // ---- obrázky
        { "jpg jpeg jpe jfif", "Fotografia JPEG", "Fotky vo Windows, GIMP, Paint.NET, Krita", "Adobe Photoshop", "" },
        { "png", "Obrázok PNG (môže byť priehľadný)", "Fotky vo Windows, GIMP, Paint.NET, Krita", "Adobe Photoshop", "" },
        { "gif", "Obrázok / animácia GIF", "Fotky vo Windows, GIMP", "Adobe Photoshop", "" },
        { "webp", "Obrázok WebP (z webu)", "Fotky vo Windows, GIMP, Paint.NET", "Adobe Photoshop", "" },
        { "bmp dib", "Bitmapový obrázok", "Maľovanie, Fotky vo Windows, GIMP", "", "" },
        { "tif tiff", "Obrázok TIFF (tlač, skenery)", "Fotky vo Windows, GIMP, Krita", "Adobe Photoshop", "" },
        { "heic heif", "Fotka z iPhonu (HEIF)", "Fotky vo Windows s rozšírením HEIF z Microsoft Store, GIMP", "", "Ak sa nezobrazí, nainštaluj z Microsoft Store rozšírenie HEIF Image Extensions." },
        { "avif", "Obrázok AVIF", "Fotky vo Windows (rozšírenie AV1), GIMP", "", "" },
        { "ico", "Ikona", "GIMP, Paint.NET", "", "" },
        { "dng cr2 cr3 crw nef nrw arw sr2 orf rw2 raf pef srw raw rwl 3fr erf kdc mrw mos", "RAW fotka z fotoaparátu",
          "darktable, RawTherapee, Fotky vo Windows (rozšírenie Raw Image Extension)", "Adobe Lightroom, Photoshop, Capture One", "Úprava RAW nemení originál – výsledok sa exportuje do JPG." },
        { "psd", "Projekt Adobe Photoshop", "GIMP, Krita, Photopea (online)", "Adobe Photoshop", "" },
        { "svg", "Vektorová grafika SVG", "Inkscape, webový prehliadač", "Adobe Illustrator, CorelDRAW", "" },
        { "eps epsf epsi", "Vektorová grafika EPS (PostScript)", "Inkscape (s Ghostscriptom), Scribus", "Adobe Illustrator, CorelDRAW", "Korgus Player ukazuje len náhľad vložený v súbore." },
        { "ai", "Adobe Illustrator", "Inkscape, prehliadač PDF (novšie súbory .ai sú vnútri PDF)", "Adobe Illustrator", "" },
        { "cdr", "CorelDRAW", "Inkscape (dobre staršie verzie 7–X4), LibreOffice Draw", "CorelDRAW", "Najspoľahlivejšie je poprosiť odosielateľa o PDF alebo SVG." },
        { "emf wmf", "Vektorový obrázok Windows (metafile)", "LibreOffice Draw, Inkscape", "", "" },
        // ---- CAD a 3D
        { "dwg", "Výkres AutoCAD", "Autodesk DWG TrueView (prezeranie, tlač, prevod), LibreCAD a FreeCAD (čiastočne)", "AutoCAD, BricsCAD, ZWCAD", "Korgus Player ukazuje len náhľad uložený vo výkrese." },
        { "dxf", "Výkres CAD (výmenný formát DXF)", "LibreCAD, QCAD, FreeCAD, Inkscape (import)", "AutoCAD", "" },
        { "stl", "3D model (3D tlač)", "PrusaSlicer, UltiMaker Cura, Bambu Studio, Blender", "", "Na tlač ho otvor v sliceri tvojej tlačiarne." },
        { "3mf", "3D model / projekt pre 3D tlač", "PrusaSlicer, UltiMaker Cura, Bambu Studio", "", "" },
        { "obj", "3D model OBJ", "Blender, PrusaSlicer, UltiMaker Cura", "", "" },
        { "fbx", "3D model / animácia FBX", "Blender", "Autodesk Maya, 3ds Max", "" },
        { "blend", "Projekt Blender", "Blender", "", "" },
        // ---- zvuk
        { "mp3", "Hudba MP3", "VLC, Windows Media Player; úprava: Audacity", "", "" },
        { "wav w64 bwf", "Zvuk WAV (bez kompresie)", "VLC; úprava: Audacity, Reaper", "Cubase, Ableton Live, FL Studio", "" },
        { "flac", "Zvuk FLAC (bezstratový)", "VLC; úprava: Audacity", "", "" },
        { "aif aiff aifc afc", "Zvuk AIFF (z Macu)", "VLC; úprava: Audacity", "Logic Pro, Cubase", "" },
        { "ogg opus", "Zvuk Ogg / Opus", "VLC; úprava: Audacity", "", "" },
        { "aac m4a m4b alac", "Zvuk AAC / M4A (Apple, YouTube)", "VLC, Windows Media Player; úprava: Audacity", "", "" },
        { "wma", "Zvuk Windows Media", "VLC, Windows Media Player", "", "" },
        { "rx2 rex rcy", "REX loop (ReCycle)", "Korgus Player (prehrávanie)", "Reason, Cubase, FL Studio, Ableton Live, Logic", "REX loop sa v DAW natiahne podľa tempa skladby." },
        { "mid midi kar rmi", "MIDI (noty, nie zvuk)", "MuseScore (noty), LMMS; prehranie: Korgus Player", "Cubase, FL Studio, Ableton Live", "MIDI obsahuje len noty – zvuk vytvorí syntetizátor." },
        // ---- video
        { "mp4 m4v", "Video MP4", "VLC, Filmy a TV; úprava: Shotcut, DaVinci Resolve, Clipchamp", "Adobe Premiere Pro", "" },
        { "mov", "Video QuickTime (iPhone, Mac)", "VLC; úprava: Shotcut, DaVinci Resolve", "Adobe Premiere Pro, Final Cut Pro", "" },
        { "mkv", "Video Matroska", "VLC, MPC-HC; úprava: Shotcut", "", "" },
        { "avi wmv flv mpg mpeg vob 3gp ogv webm", "Video", "VLC; úprava: Shotcut, DaVinci Resolve", "", "" },
        { "m2ts mts ts", "Video z kamery (AVCHD) alebo Blu-ray", "VLC; úprava: Shotcut, DaVinci Resolve", "", "" },
        // ---- text a dokumenty
        { "txt log nfo", "Textový súbor", "Poznámkový blok, Notepad++", "", "" },
        { "md markdown", "Text Markdown", "Poznámkový blok, Visual Studio Code, Obsidian", "", "" },
        { "rtf", "Formátovaný text RTF", "WordPad, LibreOffice Writer", "Microsoft Word", "" },
        { "doc docx docm dotx", "Dokument Word", "LibreOffice Writer, Word na webe (zadarmo s kontom Microsoft), Google Dokumenty", "Microsoft Word", "" },
        { "odt", "Dokument OpenDocument", "LibreOffice Writer, Google Dokumenty", "Microsoft Word", "" },
        { "pages", "Dokument Apple Pages", "Pages na iCloud.com (zadarmo s Apple ID), LibreOffice (import)", "Pages na Macu", "Ak nejde otvoriť, popros odosielateľa o export do PDF alebo DOCX." },
        { "tex bib", "Dokument LaTeX", "TeXstudio + MiKTeX, Overleaf (online)", "", "" },
        { "pdf", "Dokument PDF", "Microsoft Edge, SumatraPDF; úpravy: PDF24, LibreOffice Draw", "Adobe Acrobat", "" },
        { "srt lrc", "Titulky / text piesne", "Poznámkový blok, Subtitle Edit", "", "" },
        // ---- tabuľky a dáta
        { "xls xlsx xlsm", "Tabuľka Excel", "LibreOffice Calc, Excel na webe (zadarmo s kontom Microsoft), Google Tabuľky", "Microsoft Excel", "" },
        { "ods", "Tabuľka OpenDocument", "LibreOffice Calc, Google Tabuľky", "Microsoft Excel", "" },
        { "numbers", "Tabuľka Apple Numbers", "Numbers na iCloud.com, LibreOffice (import)", "Numbers na Macu", "" },
        { "csv tsv", "Tabuľka ako text (CSV)", "LibreOffice Calc, Google Tabuľky, Poznámkový blok", "Microsoft Excel", "Ak sa v Exceli zobrazí všetko v jednom stĺpci, použi Údaje → Z textu/CSV." },
        { "json", "Dáta JSON", "Visual Studio Code, Notepad++, webový prehliadač", "", "" },
        { "xml", "Dáta XML", "Visual Studio Code, Notepad++, webový prehliadač", "", "" },
        { "yaml yml toml ini cfg conf reg", "Nastavenia / konfigurácia", "Poznámkový blok, Visual Studio Code, Notepad++", "", "Pri úpravách nastavení si urob zálohu." },
        { "sql", "Databázový skript SQL", "Visual Studio Code, DBeaver, HeidiSQL", "", "" },
        // ---- prezentácie
        { "ppt pptx", "Prezentácia PowerPoint", "LibreOffice Impress, PowerPoint na webe (zadarmo s kontom Microsoft), Google Prezentácie", "Microsoft PowerPoint", "" },
        { "odp", "Prezentácia OpenDocument", "LibreOffice Impress, Google Prezentácie", "Microsoft PowerPoint", "" },
        { "key", "Prezentácia Apple Keynote", "Keynote na iCloud.com (zadarmo s Apple ID), LibreOffice Impress (import)", "Keynote na Macu", "" },
        // ---- fonty
        { "ttf otf ttc", "Písmo (font)", "Windows: pravý klik → Inštalovať", "", "Po inštalácii je písmo dostupné vo všetkých programoch." },
        // ---- programy a obrazy diskov
        { "exe", "Program pre Windows", "", "", "Spúšťaj len programy z dôveryhodných zdrojov. Prílohy .exe v e-maile bývajú vírus." },
        { "msi", "Inštalátor pre Windows", "", "", "Spúšťaj len inštalátory z dôveryhodných zdrojov." },
        { "dll", "Knižnica programu", "", "", "Samostatne sa neotvára – patrí k inému programu." },
        { "lnk", "Odkaz (zástupca)", "", "", "" },
        { "iso", "Obraz disku (CD/DVD)", "Windows: dvojklik pripojí ako disk; 7-Zip", "", "" },
        { "dmg", "Obraz disku pre macOS", "7-Zip (len rozbalenie)", "", "Programy z .dmg vo Windows nefungujú." },
        { "apk", "Aplikácia pre Android", "", "", "Inštaluje sa na telefón s Androidom, nie vo Windows." },
        { "zip", "Archív ZIP", "Windows (dvojklik), 7-Zip", "WinRAR", "" },
        { "rar 7z tar gz tgz bz2 xz", "Komprimovaný archív", "7-Zip, Windows 11", "WinRAR", "" },
        // ---- web a programovanie
        { "html htm", "Webová stránka", "Webový prehliadač; úprava: Visual Studio Code, Notepad++", "", "" },
        { "css scss", "Štýly webovej stránky", "Visual Studio Code, Notepad++", "", "" },
        { "js jsx ts tsx", "Kód JavaScript / TypeScript", "Visual Studio Code, Notepad++", "", "" },
        { "py", "Program v Pythone", "Visual Studio Code; spustenie: Python", "", "" },
        { "php", "Kód PHP (web)", "Visual Studio Code, Notepad++; spustenie: webový server s PHP", "", "" },
        { "cpp cc c h hpp cs java kt rs go lua rb pl swift", "Zdrojový kód programu", "Visual Studio Code, Notepad++", "", "" },
        { "sh bat cmd ps1", "Skript", "Poznámkový blok, Visual Studio Code", "", "Spúšťaj len skripty, ktorým rozumieš alebo dôveruješ." },
        { "cue m3u", "Zoznam skladieb", "VLC, foobar2000", "", "" },
    };

    const std::map<String, FormatInfo>& table()
    {
        static const std::map<String, FormatInfo> t = []
        {
            std::map<String, FormatInfo> m;
            for (auto& e : entries)
            {
                FormatInfo fi { String::fromUTF8 (e.name), String::fromUTF8 (e.freeApps), String::fromUTF8 (e.paidApps), String::fromUTF8 (e.note) };
                for (auto& x : StringArray::fromTokens (e.exts, " ", ""))
                    if (x.isNotEmpty()) m["." + x] = fi;
            }
            return m;
        }();
        return t;
    }
}

const FormatInfo* formatInfo (const File& f)
{
    const auto& t = table();
    auto it = t.find (f.getFileExtension().toLowerCase());
    return it != t.end() ? &it->second : nullptr;
}
}
