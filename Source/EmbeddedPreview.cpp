#include "ImageLoader.h"
#include "WinImaging.h"
#include <cstring>

// Náhľady uložené vo vnútri súborov: EPS, CorelDRAW, AutoCAD DWG, Blender; a EMF/WMF cez Windows
namespace kp::img
{
namespace
{
    uint32 le32 (const uint8* p) { return (uint32) p[0] | ((uint32) p[1] << 8) | ((uint32) p[2] << 16) | ((uint32) p[3] << 24); }
    uint64 le64 (const uint8* p) { return (uint64) le32 (p) | ((uint64) le32 (p + 4) << 32); }

    Image soft (const Image& im) { return im.isValid() ? SoftwareImageType().convert (im) : im; }

    // DIB bez hlavičky súboru → BMP → WIC
    Image decodeDib (const uint8* d, size_t n, int maxSide)
    {
        if (n < 40) return {};
        const uint32 hs = le32 (d);
        if (hs < 12 || hs > n) return {};
        const int bits = d[14] | (d[15] << 8);
        uint32 colours = hs >= 36 ? le32 (d + 32) : 0;
        if (colours == 0 && bits <= 8) colours = 1u << bits;
        const uint32 offset = 14 + hs + colours * 4;
        MemoryBlock bmp;
        bmp.setSize (14 + n, true);
        auto* b = static_cast<uint8*> (bmp.getData());
        b[0] = 'B'; b[1] = 'M';
        const uint32 total = (uint32) (14 + n);
        std::memcpy (b + 2, &total, 4);
        std::memcpy (b + 10, &offset, 4);
        std::memcpy (b + 14, d, n);
        return win::decodeMemory (bmp.getData(), bmp.getSize(), maxSide);
    }

    Image decodeAny (const uint8* d, size_t n, int maxSide)
    {
        MemoryInputStream mis (d, n, false);
        auto im = soft (ImageFileFormat::loadFrom (mis));
        if (im.isValid()) return downscale (im, maxSide);
        return win::decodeMemory (d, n, maxSide);
    }

    // ---------------------------------------------------------------- EPS
    Image epsPreview (const MemoryBlock& mb, int maxSide)
    {
        auto* d = static_cast<const uint8*> (mb.getData());
        const size_t n = mb.getSize();
        if (n >= 30 && d[0] == 0xc5 && d[1] == 0xd0 && d[2] == 0xd3 && d[3] == 0xc6)
        {
            const uint32 wmfOff = le32 (d + 12), wmfLen = le32 (d + 16), tifOff = le32 (d + 20), tifLen = le32 (d + 24);
            if (tifLen > 0 && (size_t) tifOff + tifLen <= n)
                if (auto im = win::decodeMemory (d + tifOff, tifLen, maxSide); im.isValid()) return im;
            if (wmfLen > 0 && (size_t) wmfOff + wmfLen <= n)
            {
                int w = 0, h = 0;
                if (auto im = win::renderMetafile (d + wmfOff, wmfLen, maxSide, w, h); im.isValid()) return im;
            }
        }
        // EPSI: %%BeginPreview: šírka výška hĺbka riadky, potom hex riadky začínajúce %
        const String text (CharPointer_UTF8 ((const char*) d), jmin<size_t> (n, 8 * 1024 * 1024));
        const int p = text.indexOf ("%%BeginPreview:");
        if (p < 0) return {};
        auto head = StringArray::fromTokens (text.substring (p + 15).upToFirstOccurrenceOf ("\n", false, false), " \t\r", "");
        head.removeEmptyStrings();
        if (head.size() < 3) return {};
        const int w = head[0].getIntValue(), h = head[1].getIntValue(), depth = head[2].getIntValue();
        if (w <= 0 || h <= 0 || w > 4000 || h > 4000 || (depth != 1 && depth != 2 && depth != 4 && depth != 8)) return {};
        const int end = text.indexOf (p, "%%EndPreview");
        MemoryBlock bits;
        {
            String hex;
            for (auto line : StringArray::fromLines (text.substring (p, end > 0 ? end : text.length())))
            {
                if (line.startsWith ("%%")) continue;
                if (line.startsWith ("%")) hex << line.substring (1).trim();
            }
            bits.loadFromHexString (hex);
        }
        const int rowBytes = (w * depth + 7) / 8;
        if ((int) bits.getSize() < rowBytes * h) return {};
        Image im (Image::RGB, w, h, false, SoftwareImageType());
        Image::BitmapData bd (im, Image::BitmapData::writeOnly);
        auto* src = static_cast<const uint8*> (bits.getData());
        const int maxV = (1 << depth) - 1;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const int bitPos = x * depth;
                const int v = (src[y * rowBytes + bitPos / 8] >> (8 - depth - (bitPos % 8))) & maxV;
                const uint8 g = (uint8) (255 - v * 255 / maxV);   // 1 = čierna
                bd.setPixelColour (x, y, Colour (g, g, g));
            }
        return downscale (im, maxSide);
    }

    // ---------------------------------------------------------------- CorelDRAW
    Image cdrPreview (const File& f, const MemoryBlock& mb, int maxSide)
    {
        auto* d = static_cast<const uint8*> (mb.getData());
        const size_t n = mb.getSize();
        if (n > 4 && d[0] == 'P' && d[1] == 'K')
        {
            ZipFile zip (f);
            const ZipFile::ZipEntry* best = nullptr;
            for (int i = 0; i < zip.getNumEntries(); ++i)
            {
                auto* e = zip.getEntry (i);
                const auto nm = e->filename.toLowerCase();
                if (nm.contains ("thumbnail") || nm.contains ("preview"))
                    if (nm.endsWith (".png") || nm.endsWith (".bmp") || nm.endsWith (".jpg"))
                        if (best == nullptr || e->uncompressedSize > best->uncompressedSize) best = e;
            }
            if (best == nullptr) return {};
            std::unique_ptr<InputStream> in (zip.createStreamForEntry (*best));
            if (in == nullptr) return {};
            MemoryBlock img;
            in->readIntoMemoryBlock (img);
            return decodeAny (static_cast<const uint8*> (img.getData()), img.getSize(), maxSide);
        }
        // staršie verzie: RIFF s blokom DISP (DIB)
        for (size_t i = 12; i + 8 < n; ++i)
            if (std::memcmp (d + i, "DISP", 4) == 0)
            {
                const uint32 len = le32 (d + i + 4);
                const uint8* body = d + i + 8;
                const size_t avail = jmin<size_t> (len, n - (i + 8));
                if (avail > 44 && le32 (body) == 40) return decodeDib (body, avail, maxSide);
                if (avail > 48 && le32 (body + 4) == 40) return decodeDib (body + 4, avail - 4, maxSide);
                return {};
            }
        return {};
    }

    // ---------------------------------------------------------------- AutoCAD DWG
    Image dwgPreview (const MemoryBlock& mb, int maxSide)
    {
        static const uint8 sentinel[16] = { 0x1F, 0x25, 0x6D, 0x07, 0xD4, 0x36, 0x28, 0x28, 0x9D, 0x57, 0xCA, 0x3F, 0x9D, 0x44, 0x10, 0x2B };
        auto* d = static_cast<const uint8*> (mb.getData());
        const size_t n = mb.getSize();
        if (n < 0x20 || std::memcmp (d, "AC10", 4) != 0) return {};
        size_t pos = le32 (d + 0x0D);
        if (pos + 16 + 5 > n || std::memcmp (d + pos, sentinel, 16) != 0)
        {
            // záloha: nájsť značku
            pos = 0;
            for (size_t i = 0x20; i + 16 < jmin<size_t> (n, 4 * 1024 * 1024); ++i)
                if (std::memcmp (d + i, sentinel, 16) == 0) { pos = i; break; }
            if (pos == 0) return {};
        }
        size_t p = pos + 16 + 4;
        const int count = d[p++];
        Image best;
        for (int i = 0; i < count && p + 9 <= n; ++i, p += 9)
        {
            const int code = d[p];
            const uint32 start = le32 (d + p + 1), len = le32 (d + p + 5);
            if ((size_t) start + len > n || len == 0) continue;
            if (code == 6) return decodeAny (d + start, len, maxSide);              // PNG
            if (code == 2) best = decodeDib (d + start, len, maxSide);              // BMP
            if (code == 3 && ! best.isValid())
            {
                int w = 0, h = 0;
                best = win::renderMetafile (d + start, len, maxSide, w, h);
            }
        }
        return best;
    }

    // ---------------------------------------------------------------- Blender
    Image blendPreview (const MemoryBlock& raw, int maxSide)
    {
        MemoryBlock mb;
        auto* r = static_cast<const uint8*> (raw.getData());
        if (raw.getSize() > 2 && r[0] == 0x1f && r[1] == 0x8b)
        {
            MemoryInputStream mis (raw, false);
            GZIPDecompressorInputStream gz (&mis, false, GZIPDecompressorInputStream::gzipFormat);
            gz.readIntoMemoryBlock (mb, 64 * 1024 * 1024);
        }
        else mb = raw;
        auto* d = static_cast<const uint8*> (mb.getData());
        const size_t n = mb.getSize();
        if (n < 12 || std::memcmp (d, "BLENDER", 7) != 0) return {};

        size_t pos;
        int ptr = 4;
        bool little = true, large = false;
        if (d[7] == '_' || d[7] == '-')
        {
            ptr = d[7] == '-' ? 8 : 4;
            little = d[8] == 'v';
            pos = 12;
        }
        else
        {
            // novší formát (Blender 5): "BLENDER17-01v0500"
            large = true;
            ptr = 8;
            little = d[12] == 'v';
            pos = 17;
        }
        if (! little) return {};

        while (pos + 16 < n)
        {
            const uint8* h = d + pos;
            size_t len, headLen;
            if (large) { len = (size_t) le64 (h + 16); headLen = 32; }
            else { len = le32 (h + 4); headLen = 4 + 4 + (size_t) ptr + 4 + 4; }
            if (std::memcmp (h, "TEST", 4) == 0 && pos + headLen + 8 <= n)
            {
                const uint8* b = d + pos + headLen;
                const int w = (int) le32 (b), hh = (int) le32 (b + 4);
                if (w <= 0 || hh <= 0 || w > 2048 || hh > 2048 || pos + headLen + 8 + (size_t) w * (size_t) hh * 4 > n) return {};
                Image im (Image::ARGB, w, hh, true, SoftwareImageType());
                Image::BitmapData bd (im, Image::BitmapData::writeOnly);
                const uint8* px = b + 8;
                for (int y = 0; y < hh; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const uint8* q = px + ((size_t) (hh - 1 - y) * (size_t) w + (size_t) x) * 4;   // riadky zdola
                        bd.setPixelColour (x, y, Colour (q[0], q[1], q[2], q[3]));
                    }
                return downscale (im, maxSide);
            }
            if (std::memcmp (h, "ENDB", 4) == 0 || len > n) break;
            pos += headLen + len;
        }
        return {};
    }
}

bool hasEmbeddedPreview (const File& f)
{
    return f.hasFileExtension ("eps;epsf;epsi;cdr;dwg;blend;emf;wmf");
}

Image embeddedPreview (const File& f, int maxSide, int& origW, int& origH)
{
    MemoryBlock mb;
    const auto e = f.getFileExtension().toLowerCase();
    if (! f.loadFileAsData (mb) || mb.getSize() < 16) return {};
    Image im;
    if (e == ".emf" || e == ".wmf") return win::renderMetafile (mb.getData(), mb.getSize(), maxSide, origW, origH);
    if (e == ".eps" || e == ".epsf" || e == ".epsi") im = epsPreview (mb, maxSide);
    else if (e == ".cdr") im = cdrPreview (f, mb, maxSide);
    else if (e == ".dwg") im = dwgPreview (mb, maxSide);
    else if (e == ".blend") im = blendPreview (mb, maxSide);
    origW = im.getWidth();
    origH = im.getHeight();
    return im;
}
}
