#pragma once
#include "Common.h"

// Načítanie obrázkov a SVG (volá sa z vlákien na pozadí)
namespace kp::img
{
    struct Loaded
    {
        Image image;
        int origW = 0, origH = 0;
    };

    // Celý obrázok, zmenšený tak, aby dlhšia strana nebola väčšia ako maxSide.
    // Otočí sa podľa EXIF orientácie.
    Loaded loadFull (const File& f, int maxSide);

    // Náhľad, dlhšia strana = side. Pri JPEG najprv skúsi náhľad vložený v EXIF.
    Image loadThumb (const File& f, int side);

    // Náhľady vložené v súboroch (EPS, CDR, DWG, Blend) a EMF/WMF
    bool hasEmbeddedPreview (const File& f);
    Image embeddedPreview (const File& f, int maxSide, int& origW, int& origH);

    // Zmenšenie s dobrou kvalitou (postupné polovičné kroky)
    Image downscale (const Image& src, int maxSide);
}
