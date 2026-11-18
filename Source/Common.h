#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_cryptography/juce_cryptography.h>

namespace kp
{
using namespace juce;

// ---- Farby (tlmené oceľové tóny ako Korgus Sequencer) ----
namespace col
{
    const Colour bg        { 0xff161b22 };
    const Colour panel     { 0xff1d242e };
    const Colour panelHi   { 0xff252e3a };
    const Colour line      { 0xff2f3a48 };
    const Colour accent    { 0xff2d4362 };
    const Colour accentHi  { 0xff3d5a82 };
    const Colour text      { 0xffd3d9e1 };
    const Colour textDim   { 0xff8794a5 };
    const Colour gold      { 0xffc9a45c };
}

constexpr float radius = 3.0f;

enum class Kind { Folder, Image, Svg, Audio, Midi, Video, Font, Pdf, Model, Cad, Text, Table, Slides, App, Other };

inline Kind kindOf (const File& f)
{
    if (f.isDirectory()) return Kind::Folder;
    const auto e = f.getFileExtension().toLowerCase();
    if (e == ".jpg" || e == ".jpeg" || e == ".jpe" || e == ".png" || e == ".gif"
        // cez Windows (WIC): podľa nainštalovaných kodekov
        || e == ".bmp" || e == ".dib" || e == ".tif" || e == ".tiff" || e == ".webp" || e == ".ico" || e == ".jfif"
        || e == ".heic" || e == ".heif" || e == ".avif" || e == ".jxr" || e == ".wdp" || e == ".dds"
        || e == ".dng" || e == ".cr2" || e == ".cr3" || e == ".crw" || e == ".nef" || e == ".nrw" || e == ".arw"
        || e == ".sr2" || e == ".orf" || e == ".rw2" || e == ".raf" || e == ".pef" || e == ".srw" || e == ".raw"
        || e == ".rwl" || e == ".3fr" || e == ".erf" || e == ".kdc" || e == ".mrw" || e == ".mos"
        // vložené náhľady a metafily
        || e == ".eps" || e == ".epsf" || e == ".epsi" || e == ".cdr" || e == ".dwg" || e == ".blend"
        || e == ".emf" || e == ".wmf") return Kind::Image;
    if (e == ".svg") return Kind::Svg;
    if (e == ".wav" || e == ".aif" || e == ".aiff" || e == ".aifc" || e == ".flac" || e == ".ogg"
        || e == ".mp3" || e == ".wma" || e == ".bwf" || e == ".afc" || e == ".w64"
        || e == ".m4a" || e == ".aac" || e == ".m4b" || e == ".opus" || e == ".alac" || e == ".ac3" || e == ".amr"
        || e == ".rx2" || e == ".rex" || e == ".rcy") return Kind::Audio;
    if (e == ".mid" || e == ".midi" || e == ".kar" || e == ".rmi") return Kind::Midi;
    if (e == ".mp4" || e == ".mkv" || e == ".avi" || e == ".mov" || e == ".wmv" || e == ".webm"
        || e == ".m4v" || e == ".mpg" || e == ".mpeg" || e == ".ts" || e == ".m2ts" || e == ".mts"
        || e == ".3gp" || e == ".flv" || e == ".vob" || e == ".ogv") return Kind::Video;
    if (e == ".ttf" || e == ".otf" || e == ".ttc") return Kind::Font;
    if (e == ".pdf" || e == ".ai") return Kind::Pdf;
    if (e == ".txt" || e == ".md" || e == ".markdown" || e == ".log" || e == ".ini" || e == ".cfg" || e == ".conf"
        || e == ".json" || e == ".xml" || e == ".yaml" || e == ".yml" || e == ".toml" || e == ".sql"
        || e == ".html" || e == ".htm" || e == ".css" || e == ".scss" || e == ".js" || e == ".ts" || e == ".jsx" || e == ".tsx"
        || e == ".py" || e == ".php" || e == ".cpp" || e == ".cc" || e == ".c" || e == ".h" || e == ".hpp" || e == ".cs"
        || e == ".java" || e == ".kt" || e == ".rs" || e == ".go" || e == ".lua" || e == ".rb" || e == ".pl" || e == ".swift"
        || e == ".sh" || e == ".bat" || e == ".cmd" || e == ".ps1" || e == ".tex" || e == ".bib" || e == ".srt" || e == ".lrc"
        || e == ".nfo" || e == ".cue" || e == ".m3u" || e == ".reg" || e == ".rtf" || e == ".docx" || e == ".odt") return Kind::Text;
    if (e == ".csv" || e == ".tsv" || e == ".xlsx" || e == ".xlsm" || e == ".ods") return Kind::Table;
    if (e == ".pptx" || e == ".odp" || e == ".key" || e == ".pages" || e == ".numbers") return Kind::Slides;
    if (e == ".exe" || e == ".msi" || e == ".dll" || e == ".lnk") return Kind::App;
    if (e == ".stl" || e == ".obj" || e == ".3mf") return Kind::Model;
    if (e == ".dxf") return Kind::Cad;
    return Kind::Other;
}

// Typy so samostatným prehliadačom (DocViewer)
inline bool isDocKind (Kind k)
{
    return k == Kind::Font || k == Kind::Pdf || k == Kind::Model || k == Kind::Cad
        || k == Kind::Text || k == Kind::Table || k == Kind::Slides || k == Kind::App;
}

// Súbory, pre ktoré môže mať náhľad Windows (Shell)
inline bool hasShellThumbnail (const File& f)
{
    const auto e = f.getFileExtension().toLowerCase();
    return kindOf (f) == Kind::Video || e == ".psd" || e == ".doc" || e == ".docm" || e == ".dotx" || e == ".xls" || e == ".ppt" || e == ".fbx" || e == ".iso" || e == ".ai" || e == ".eps"
        || e == ".tga" || e == ".exr" || e == ".hdr" || e == ".xcf" || e == ".indd";
}

inline String sizeText (int64 bytes)
{
    if (bytes < 1024) return String (bytes) + " B";
    if (bytes < 1024 * 1024) return String (bytes / 1024.0, 1) + " kB";
    if (bytes < (int64) 1024 * 1024 * 1024) return String (bytes / (1024.0 * 1024.0), 1) + " MB";
    return String (bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
}

// Svetlá šachovnica pod priehľadné obrázky (čierne SVG/PNG by na tmavom pozadí nebolo vidieť)
inline void drawChecker (Graphics& g, Rectangle<float> r, float cell = 8.0f)
{
    Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (r.getSmallestIntegerContainer());
    g.setColour (Colour (0xffffffff));
    g.fillRect (r);
    g.setColour (Colour (0xffd9dde2));
    const int cols = (int) std::ceil (r.getWidth() / cell), rows = (int) std::ceil (r.getHeight() / cell);
    for (int y = 0; y < rows; ++y)
        for (int x = (y & 1); x < cols; x += 2)
            g.fillRect (r.getX() + (float) x * cell, r.getY() + (float) y * cell, cell, cell);
}

// Farba štítku podľa typu súboru
inline Colour kindColour (Kind k)
{
    switch (k)
    {
        case Kind::Folder: return Colour (0xff6f8db5);
        case Kind::Image:  return Colour (0xff5f9e86);
        case Kind::Svg:    return Colour (0xff8a7fb8);
        case Kind::Audio:  return Colour (0xffc9a45c);
        case Kind::Midi:   return Colour (0xff6fa7a0);
        case Kind::Font:   return Colour (0xffb58a6f);
        case Kind::Pdf:    return Colour (0xffb55a5a);
        case Kind::Model:  return Colour (0xff6f9bb5);
        case Kind::Cad:    return Colour (0xff7fb56f);
        case Kind::Text:   return Colour (0xff8a95a5);
        case Kind::Table:  return Colour (0xff4f9a6a);
        case Kind::Slides: return Colour (0xffc07a45);
        case Kind::App:    return Colour (0xff5f6f8f);
        case Kind::Video:  return Colour (0xffb5706f);
        case Kind::Other:
        default:           return Colour (0xff6b7686);
    }
}
} // namespace kp
