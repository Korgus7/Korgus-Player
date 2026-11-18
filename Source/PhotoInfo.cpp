#include "PhotoInfo.h"
#include <cstring>

namespace kp
{
namespace
{
    struct Tiff
    {
        const uint8* d; size_t n; size_t base; bool le;
        bool ok (size_t p, size_t len) const { return p + len <= n; }
        uint32 u16 (size_t p) const { return le ? (uint32) (d[p] | (d[p + 1] << 8)) : (uint32) ((d[p] << 8) | d[p + 1]); }
        uint32 u32 (size_t p) const
        {
            return le ? (uint32) d[p] | ((uint32) d[p + 1] << 8) | ((uint32) d[p + 2] << 16) | ((uint32) d[p + 3] << 24)
                      : ((uint32) d[p] << 24) | ((uint32) d[p + 1] << 16) | ((uint32) d[p + 2] << 8) | (uint32) d[p + 3];
        }

        // poloha hodnoty položky (priamo v položke alebo cez posun)
        size_t valuePos (size_t e, size_t bytes) const { return bytes <= 4 ? e + 8 : base + u32 (e + 8); }

        static size_t typeSize (uint32 t) { return t == 3 ? 2 : (t == 4 || t == 9) ? 4 : (t == 5 || t == 10) ? 8 : 1; }

        String ascii (size_t e) const
        {
            const size_t cnt = u32 (e + 4);
            const size_t p = valuePos (e, cnt);
            if (! ok (p, cnt)) return {};
            String s;
            for (size_t i = 0; i < cnt && d[p + i]; ++i) s << String::charToString ((juce_wchar) d[p + i]);
            return s.trim();
        }

        double rational (size_t e, int index = 0) const
        {
            const size_t p = valuePos (e, 8 * u32 (e + 4)) + (size_t) index * 8;
            if (! ok (p, 8)) return 0;
            const double num = (double) u32 (p), den = (double) u32 (p + 4);
            return den != 0 ? num / den : 0;
        }

        uint32 number (size_t e) const
        {
            const uint32 type = u16 (e + 2);
            return type == 3 ? u16 (e + 8) : u32 (e + 8);
        }

        template <typename Fn> void forEach (size_t ifd, Fn fn) const
        {
            if (! ok (ifd, 2)) return;
            const uint32 cnt = u16 (ifd);
            for (uint32 i = 0; i < cnt && i < 500; ++i)
            {
                const size_t e = ifd + 2 + i * 12;
                if (! ok (e, 12)) break;
                fn (u16 (e), e);
            }
        }
    };

    String exposureText (double t)
    {
        if (t <= 0) return {};
        if (t >= 1.0) return String (t, t < 10 ? 1 : 0) + " s";
        return "1/" + String (roundToInt (1.0 / t)) + " s";
    }

    bool parseDate (const String& s, Time& out)
    {
        // „2025:08:12 14:33:05“
        if (s.length() < 19) return false;
        const int y = s.substring (0, 4).getIntValue(), mo = s.substring (5, 7).getIntValue(), d = s.substring (8, 10).getIntValue();
        const int h = s.substring (11, 13).getIntValue(), mi = s.substring (14, 16).getIntValue(), se = s.substring (17, 19).getIntValue();
        if (y < 1900 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
        out = Time (y, mo - 1, d, h, mi, se, 0, true);
        return true;
    }
}

String PhotoInfo::mapUrl() const
{
    return "https://www.openstreetmap.org/?mlat=" + String (lat, 6) + "&mlon=" + String (lon, 6) + "#map=16/" + String (lat, 6) + "/" + String (lon, 6);
}

PhotoInfo readPhotoInfo (const File& f)
{
    PhotoInfo info;
    if (! f.hasFileExtension ("jpg;jpeg;jpe;jfif")) return info;
    FileInputStream in (f);
    if (! in.openedOk()) return info;
    MemoryBlock mb;
    in.readIntoMemoryBlock (mb, 256 * 1024);
    auto* d = static_cast<const uint8*> (mb.getData());
    const size_t n = mb.getSize();
    if (n < 4 || d[0] != 0xff || d[1] != 0xd8) return info;

    size_t pos = 2;
    while (pos + 4 <= n && d[pos] == 0xff)
    {
        const uint8 marker = d[pos + 1];
        if (marker == 0xda || marker == 0xd9) break;
        const size_t len = (size_t) ((d[pos + 2] << 8) | d[pos + 3]);
        if (marker == 0xe1 && pos + 10 <= n && std::memcmp (d + pos + 4, "Exif\0\0", 6) == 0)
        {
            Tiff t { d, n, pos + 10, d[pos + 10] == 'I' };
            const size_t ifd0 = t.base + t.u32 (t.base + 4);
            String make, model, dateTime;
            size_t exifIfd = 0, gpsIfd = 0;
            t.forEach (ifd0, [&] (uint32 tag, size_t e)
            {
                if (tag == 0x010f) make = t.ascii (e);
                else if (tag == 0x0110) model = t.ascii (e);
                else if (tag == 0x0132) dateTime = t.ascii (e);
                else if (tag == 0x8769) exifIfd = t.base + t.u32 (e + 8);
                else if (tag == 0x8825) gpsIfd = t.base + t.u32 (e + 8);
            });
            // „Canon Canon EOS R6“ → „Canon EOS R6“
            info.camera = model.startsWithIgnoreCase (make.upToFirstOccurrenceOf (" ", false, false)) || make.isEmpty() ? model : make + " " + model;

            double fnum = 0, expo = 0, focal = 0;
            uint32 iso = 0, focal35 = 0;
            String original;
            if (exifIfd > 0)
                t.forEach (exifIfd, [&] (uint32 tag, size_t e)
                {
                    if (tag == 0x829a) expo = t.rational (e);
                    else if (tag == 0x829d) fnum = t.rational (e);
                    else if (tag == 0x8827) iso = t.number (e);
                    else if (tag == 0x9003) original = t.ascii (e);
                    else if (tag == 0x920a) focal = t.rational (e);
                    else if (tag == 0xa405) focal35 = t.number (e);
                    else if (tag == 0xa434) info.lens = t.ascii (e);
                });
            StringArray parts;
            if (fnum > 0) parts.add ("f/" + (std::abs (fnum - std::round (fnum)) < 0.05 ? String (roundToInt (fnum)) : String (fnum, 1).replace (".", ",")));
            if (expo > 0) parts.add (exposureText (expo));
            if (iso > 0) parts.add ("ISO " + String (iso));
            if (focal > 0) parts.add (String (roundToInt (focal)) + " mm" + (focal35 > 0 && (int) focal35 != roundToInt (focal) ? " (" + String (focal35) + " mm)" : String()));
            info.settings = parts.joinIntoString (String::fromUTF8 (" · "));
            info.hasTaken = parseDate (original.isNotEmpty() ? original : dateTime, info.taken);

            if (gpsIfd > 0)
            {
                String latRef, lonRef;
                double lat = 0, lon = 0;
                bool hasLat = false, hasLon = false;
                t.forEach (gpsIfd, [&] (uint32 tag, size_t e)
                {
                    auto dms = [&] { return t.rational (e, 0) + t.rational (e, 1) / 60.0 + t.rational (e, 2) / 3600.0; };
                    if (tag == 1) latRef = t.ascii (e);
                    else if (tag == 2) { lat = dms(); hasLat = true; }
                    else if (tag == 3) lonRef = t.ascii (e);
                    else if (tag == 4) { lon = dms(); hasLon = true; }
                });
                if (hasLat && hasLon && (lat != 0 || lon != 0))
                {
                    info.hasGps = true;
                    info.lat = latRef.startsWithIgnoreCase ("S") ? -lat : lat;
                    info.lon = lonRef.startsWithIgnoreCase ("W") ? -lon : lon;
                }
            }
            break;
        }
        pos += 2 + len;
    }
    return info;
}
}
