#include "ImageEdit.h"
#include "ImageLoader.h"
#include <cstring>

namespace kp::edit
{
namespace
{
    bool isJpeg (const File& f) { return f.hasFileExtension ("jpg;jpeg;jpe;jfif"); }
    bool isPng (const File& f) { return f.hasFileExtension ("png"); }

    int rotateOrientation (int o, bool cw)
    {
        static const int cwMap[9] = { 0, 6, 7, 8, 5, 2, 3, 4, 1 };
        static const int ccwMap[9] = { 0, 8, 5, 6, 7, 4, 1, 2, 3 };
        o = jlimit (1, 8, o);
        return cw ? cwMap[o] : ccwMap[o];
    }

    // nájde hodnotu orientácie v EXIF; vráti pozíciu 2 bajtov hodnoty a poradie bajtov
    bool findOrientation (const MemoryBlock& mb, size_t& valuePos, bool& little, size_t& app1Pos, int& value)
    {
        auto* d = static_cast<const uint8*> (mb.getData());
        const size_t n = mb.getSize();
        app1Pos = 0;
        if (n < 4 || d[0] != 0xff || d[1] != 0xd8) return false;
        size_t pos = 2;
        while (pos + 4 <= n && d[pos] == 0xff)
        {
            const uint8 marker = d[pos + 1];
            if (marker == 0xda || marker == 0xd9) break;
            const size_t len = (size_t) ((d[pos + 2] << 8) | d[pos + 3]);
            if (marker == 0xe1 && pos + 10 <= n && std::memcmp (d + pos + 4, "Exif\0\0", 6) == 0)
            {
                app1Pos = pos;
                const size_t tiff = pos + 10;
                if (tiff + 8 > n) return false;
                little = d[tiff] == 'I';
                auto u16 = [&] (size_t p) { return little ? (uint32) (d[p] | (d[p + 1] << 8)) : (uint32) ((d[p] << 8) | d[p + 1]); };
                auto u32 = [&] (size_t p) { return little ? ((uint32) d[p] | ((uint32) d[p + 1] << 8) | ((uint32) d[p + 2] << 16) | ((uint32) d[p + 3] << 24))
                                                          : (((uint32) d[p] << 24) | ((uint32) d[p + 1] << 16) | ((uint32) d[p + 2] << 8) | (uint32) d[p + 3]); };
                const size_t ifd0 = tiff + u32 (tiff + 4);
                if (ifd0 + 2 > n) return false;
                const uint32 cnt = u16 (ifd0);
                for (uint32 i = 0; i < cnt; ++i)
                {
                    const size_t e = ifd0 + 2 + i * 12;
                    if (e + 12 > n) break;
                    if (u16 (e) == 0x0112)
                    {
                        valuePos = e + 8;
                        value = (int) u16 (e + 8);
                        return true;
                    }
                }
                return false;
            }
            pos += 2 + len;
        }
        return false;
    }

    bool writeAtomically (const File& f, const void* data, size_t size, String& error)
    {
        const auto tmp = f.getSiblingFile (f.getFileName() + ".korgus-tmp");
        if (! tmp.replaceWithData (data, size)) { error = String::fromUTF8 ("Nedá sa zapisovať do priečinka."); return false; }
        // súbor môže chvíľu čítať náhľad – skúsiť niekoľkokrát
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            if (tmp.moveFileTo (f)) return true;
            Thread::sleep (100);
        }
        tmp.deleteFile();
        error = String::fromUTF8 ("Súbor sa nedá prepísať (možno je otvorený inde).");
        return false;
    }

    Image rotatePixels (const Image& src, bool cw)
    {
        const int w = src.getWidth(), h = src.getHeight();
        Image out (src.getFormat(), h, w, true, SoftwareImageType());
        Graphics g (out);
        g.setImageResamplingQuality (Graphics::lowResamplingQuality);
        g.drawImageTransformed (src, cw ? AffineTransform (0, -1, (float) h, 1, 0, 0) : AffineTransform (0, 1, 0, -1, 0, (float) w), false);
        return out;
    }

    bool encode (const Image& im, const File& dst, bool png, String& error)
    {
        MemoryOutputStream mos;
        bool ok;
        if (png) { PNGImageFormat f; ok = f.writeImageToStream (im, mos); }
        else
        {
            JPEGImageFormat f;
            f.setQuality (0.92f);
            // JPG nemá priehľadnosť – podložiť bielou
            Image rgb (Image::RGB, im.getWidth(), im.getHeight(), true, SoftwareImageType());
            { Graphics g (rgb); g.fillAll (Colours::white); g.drawImageAt (im, 0, 0); }
            ok = f.writeImageToStream (rgb, mos);
        }
        if (! ok) { error = String::fromUTF8 ("Obrázok sa nepodarilo uložiť."); return false; }
        return writeAtomically (dst, mos.getData(), mos.getDataSize(), error);
    }
}

bool canRotate (const File& f) { return isJpeg (f) || isPng (f); }
bool canResize (const File& f) { return kindOf (f) == Kind::Image && ! f.hasFileExtension ("eps;epsf;epsi;cdr;dwg;blend;emf;wmf"); }

bool rotate (const File& f, bool cw, String& error)
{
    MemoryBlock mb;
    if (! f.loadFileAsData (mb)) { error = String::fromUTF8 ("Súbor sa nedá prečítať."); return false; }

    if (isJpeg (f))
    {
        size_t valuePos = 0, app1 = 0;
        bool little = true;
        int o = 1;
        if (findOrientation (mb, valuePos, little, app1, o))
        {
            // bez straty: len zmena čísla orientácie
            const int no = rotateOrientation (o, cw);
            auto* d = static_cast<uint8*> (mb.getData());
            d[valuePos] = (uint8) (little ? no : 0);
            d[valuePos + 1] = (uint8) (little ? 0 : no);
            return writeAtomically (f, mb.getData(), mb.getSize(), error);
        }
        if (app1 == 0)
        {
            // bez EXIF: vložiť malý EXIF blok s orientáciou hneď za SOI
            const uint8 seg[] = { 0xff, 0xe1, 0x00, 0x22, 'E', 'x', 'i', 'f', 0, 0,
                                  'I', 'I', 0x2a, 0x00, 0x08, 0x00, 0x00, 0x00,
                                  0x01, 0x00,
                                  0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, (uint8) rotateOrientation (1, cw), 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00 };
            MemoryBlock out;
            out.append (mb.getData(), 2);
            out.append (seg, sizeof (seg));
            out.append (static_cast<const uint8*> (mb.getData()) + 2, mb.getSize() - 2);
            return writeAtomically (f, out.getData(), out.getSize(), error);
        }
        // EXIF bez orientácie: uložiť nanovo (s malou stratou)
        auto loaded = img::loadFull (f, 0);
        if (! loaded.image.isValid()) { error = String::fromUTF8 ("Obrázok sa nedá načítať."); return false; }
        return encode (rotatePixels (loaded.image, cw), f, false, error);
    }

    if (isPng (f))
    {
        MemoryInputStream mis (mb, false);
        auto im = ImageFileFormat::loadFrom (mis);
        if (! im.isValid()) { error = String::fromUTF8 ("Obrázok sa nedá načítať."); return false; }
        return encode (rotatePixels (SoftwareImageType().convert (im), cw), f, true, error);
    }

    error = String::fromUTF8 ("Otáčať sa dajú zatiaľ len JPG a PNG.");
    return false;
}

File resizedName (const File& src, int longSide, int percent, const File& targetDir)
{
    const auto ext = isPng (src) ? String (".png") : String (".jpg");
    const auto dir = targetDir.isDirectory() ? targetDir : src.getParentDirectory();
    // do iného priečinka ide pod pôvodným menom, vedľa originálu s príponou _1920
    const auto base = targetDir.isDirectory() && targetDir != src.getParentDirectory()
                        ? src.getFileNameWithoutExtension()
                        : src.getFileNameWithoutExtension() + "_" + (percent > 0 ? String (percent) + "pct" : String (longSide));
    auto f = dir.getChildFile (base + ext);
    if (f.exists()) f = dir.getNonexistentChildFile (base, ext, true);
    return f;
}

bool resize (const File& src, const File& dst, int longSide, int percent, String& error)
{
    int target = longSide;
    auto full = img::loadFull (src, 0);
    if (! full.image.isValid()) { error = String::fromUTF8 ("Obrázok sa nedá načítať: ") + src.getFileName(); return false; }
    const int cur = jmax (full.image.getWidth(), full.image.getHeight());
    if (percent > 0) target = jmax (1, cur * percent / 100);
    Image out = target < cur ? img::downscale (full.image, target) : full.image;
    return encode (out, dst, isPng (dst), error);
}
}
