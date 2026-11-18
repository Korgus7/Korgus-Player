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

enum class Kind { Folder, Image, Svg, Audio, Midi, Video, Other };

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
        || e == ".rwl" || e == ".3fr" || e == ".erf" || e == ".kdc" || e == ".mrw" || e == ".mos") return Kind::Image;
    if (e == ".svg") return Kind::Svg;
    if (e == ".wav" || e == ".aif" || e == ".aiff" || e == ".aifc" || e == ".flac" || e == ".ogg"
        || e == ".mp3" || e == ".wma" || e == ".bwf" || e == ".afc" || e == ".w64"
        || e == ".rx2" || e == ".rex" || e == ".rcy") return Kind::Audio;
    if (e == ".mid" || e == ".midi" || e == ".kar" || e == ".rmi") return Kind::Midi;
    if (e == ".mp4" || e == ".mkv" || e == ".avi" || e == ".mov" || e == ".wmv" || e == ".webm"
        || e == ".m4v" || e == ".mpg" || e == ".mpeg" || e == ".ts" || e == ".m2ts" || e == ".mts"
        || e == ".3gp" || e == ".flv" || e == ".vob" || e == ".ogv") return Kind::Video;
    return Kind::Other;
}

// Súbory, pre ktoré môže mať náhľad Windows (Shell)
inline bool hasShellThumbnail (const File& f)
{
    const auto e = f.getFileExtension().toLowerCase();
    return kindOf (f) == Kind::Video || e == ".pdf" || e == ".psd" || e == ".ai" || e == ".eps"
        || e == ".tga" || e == ".exr" || e == ".hdr" || e == ".xcf" || e == ".indd";
}

inline String sizeText (int64 bytes)
{
    if (bytes < 1024) return String (bytes) + " B";
    if (bytes < 1024 * 1024) return String (bytes / 1024.0, 1) + " kB";
    if (bytes < (int64) 1024 * 1024 * 1024) return String (bytes / (1024.0 * 1024.0), 1) + " MB";
    return String (bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
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
        case Kind::Video:  return Colour (0xffb5706f);
        case Kind::Other:
        default:           return Colour (0xff6b7686);
    }
}
} // namespace kp
