#pragma once
#include "Common.h"

namespace kp
{
// Údaje z fotoaparátu (EXIF v JPG): fotoaparát, objektív, nastavenia, dátum fotenia, GPS
struct PhotoInfo
{
    String camera, lens, settings;   // settings: „f/4 · 1/125 s · ISO 400 · 50 mm“
    Time taken;
    bool hasTaken = false, hasGps = false;
    double lat = 0, lon = 0;
    bool empty() const { return camera.isEmpty() && settings.isEmpty() && ! hasTaken && ! hasGps; }
    String mapUrl() const;
};

PhotoInfo readPhotoInfo (const File& f);
}
