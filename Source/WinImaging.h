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
}
