#pragma once
#include "Common.h"

// Úpravy obrázkov: otočenie a zmena veľkosti
namespace kp::edit
{
    // Otočenie o 90°. JPG bez straty (zmena EXIF orientácie), PNG sa uloží nanovo.
    bool canRotate (const File& f);
    bool rotate (const File& f, bool clockwise, String& error);

    // Zmenšenie tak, aby dlhšia strana mala longSide px (alebo percent, ak percent > 0).
    // Výsledok JPG (kvalita 92 %) alebo PNG podľa pôvodného súboru.
    bool canResize (const File& f);
    bool resize (const File& src, const File& dst, int longSide, int percent, String& error);
    File resizedName (const File& src, int longSide, int percent);
}
