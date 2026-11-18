#pragma once
#include "Common.h"

// Obrázky cez Windows: WIC (BMP, TIFF, WebP, ICO, HEIC, RAW… podľa nainštalovaných kodekov,
// rýchle zmenšené dekódovanie JPEG) a náhľady zo Shellu (videá, PDF, PSD…).
// Na iných systémoch vracajú prázdny obrázok.
namespace kp::win
{
    // Celý obrázok zmenšený na maxSide (0 = plné rozlíšenie), otočený podľa EXIF.
    Image loadImage (const File& f, int maxSide, int& origW, int& origH);

    // Náhľad z Windows (rovnaký ako v Prieskumníkovi), len ak existuje skutočný náhľad
    Image shellThumbnail (const File& f, int side);

    // Ikona súboru z Windows (pre programy)
    Image shellIcon (const File& f, int side);

    // Dekódovanie obrázka z pamäte cez WIC (BMP, TIFF…)
    Image decodeMemory (const void* data, size_t size, int maxSide);

    // EMF / WMF (aj z pamäte) vykreslené cez GDI na biele pozadie
    Image renderMetafile (const void* data, size_t size, int maxSide, int& origW, int& origH);

    // Text v kódovaní Windows (napr. 1250) → Unicode; na iných systémoch Latin-1
    String decodeAnsi (const char* data, size_t size);

    // Programy, ktorými Windows vie súbor otvoriť (Otvoriť v…), a spustenie jedného z nich
    StringArray openWithApps (const File& f);
    bool openWithApp (const File& f, int index);

    // Informácie o programe (.exe/.dll/.msi): názov, popis, výrobca, verzia
    StringPairArray versionInfo (const File& f);
}
