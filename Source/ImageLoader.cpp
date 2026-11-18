#include "ImageLoader.h"
#include "WinImaging.h"
#include "DocViewers.h"

namespace kp::img
{
namespace
{
    struct ExifInfo
    {
        int orientation = 1;
        int64 thumbOffset = -1; // poloha v bufferi
        int64 thumbLength = 0;
    };

    struct Reader
    {
        const uint8* d; size_t n; bool le;
        bool ok (size_t pos, size_t len) const { return pos + len <= n; }
        uint32 u16 (size_t p) const { return le ? (uint32) (d[p] | (d[p + 1] << 8)) : (uint32) ((d[p] << 8) | d[p + 1]); }
        uint32 u32 (size_t p) const
        {
            return le ? (uint32) d[p] | ((uint32) d[p + 1] << 8) | ((uint32) d[p + 2] << 16) | ((uint32) d[p + 3] << 24)
                      : ((uint32) d[p] << 24) | ((uint32) d[p + 1] << 16) | ((uint32) d[p + 2] << 8) | (uint32) d[p + 3];
        }
    };

    ExifInfo parseExif (const MemoryBlock& mb)
    {
        ExifInfo info;
        auto* d = static_cast<const uint8*> (mb.getData());
        const size_t n = mb.getSize();
        if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return info;

        size_t pos = 2;
        while (pos + 4 <= n)
        {
            if (d[pos] != 0xFF) break;
            const uint8 marker = d[pos + 1];
            if (marker == 0xD9 || marker == 0xDA) break; // koniec / začiatok dát obrazu
            const size_t len = (size_t) ((d[pos + 2] << 8) | d[pos + 3]);
            if (len < 2) break;

            if (marker == 0xE1 && pos + 10 <= n && std::memcmp (d + pos + 4, "Exif\0\0", 6) == 0)
            {
                const size_t tiff = pos + 10;
                if (tiff + 8 > n) break;
                Reader r { d, n, d[tiff] == 'I' };
                const size_t ifd0 = tiff + r.u32 (tiff + 4);
                if (! r.ok (ifd0, 2)) break;
                const uint32 cnt = r.u16 (ifd0);
                for (uint32 i = 0; i < cnt; ++i)
                {
                    const size_t e = ifd0 + 2 + i * 12;
                    if (! r.ok (e, 12)) break;
                    if (r.u16 (e) == 0x0112) info.orientation = (int) r.u16 (e + 8);
                }
                const size_t nextPos = ifd0 + 2 + cnt * 12;
                if (r.ok (nextPos, 4))
                {
                    const uint32 ifd1Rel = r.u32 (nextPos);
                    if (ifd1Rel != 0 && r.ok (tiff + ifd1Rel, 2))
                    {
                        const size_t ifd1 = tiff + ifd1Rel;
                        const uint32 c1 = r.u16 (ifd1);
                        int64 off = -1, tlen = 0;
                        for (uint32 i = 0; i < c1; ++i)
                        {
                            const size_t e = ifd1 + 2 + i * 12;
                            if (! r.ok (e, 12)) break;
                            const auto tag = r.u16 (e);
                            if (tag == 0x0201) off = (int64) r.u32 (e + 8);
                            if (tag == 0x0202) tlen = (int64) r.u32 (e + 8);
                        }
                        if (off > 0 && tlen > 0 && r.ok (tiff + (size_t) off, (size_t) tlen))
                        {
                            info.thumbOffset = (int64) tiff + off;
                            info.thumbLength = tlen;
                        }
                    }
                }
                break;
            }
            pos += 2 + len;
        }
        if (info.orientation < 1 || info.orientation > 8) info.orientation = 1;
        return info;
    }

    // obrázky z vlákien na pozadí držíme v pamäti (nie na GPU)
    Image soft (const Image& im) { return im.isValid() ? SoftwareImageType().convert (im) : im; }

    bool isJpeg (const File& f)
    {
        const auto e = f.getFileExtension().toLowerCase();
        return e == ".jpg" || e == ".jpeg" || e == ".jpe";
    }

    ExifInfo readExif (const File& f, MemoryBlock& head)
    {
        FileInputStream in (f);
        if (! in.openedOk()) return {};
        head.setSize ((size_t) jmin<int64> (in.getTotalLength(), 256 * 1024));
        in.read (head.getData(), (int) head.getSize());
        return parseExif (head);
    }

    Image applyOrientation (const Image& src, int o)
    {
        if (o <= 1 || ! src.isValid()) return src;
        const float w = (float) src.getWidth(), h = (float) src.getHeight();
        const bool swap = o >= 5;
        AffineTransform t;
        switch (o)
        {
            case 2: t = AffineTransform (-1, 0, w, 0, 1, 0); break;
            case 3: t = AffineTransform (-1, 0, w, 0, -1, h); break;
            case 4: t = AffineTransform (1, 0, 0, 0, -1, h); break;
            case 5: t = AffineTransform (0, 1, 0, 1, 0, 0); break;
            case 6: t = AffineTransform (0, -1, h, 1, 0, 0); break;
            case 7: t = AffineTransform (0, -1, h, -1, 0, w); break;
            case 8: t = AffineTransform (0, 1, 0, -1, 0, w); break;
            default: return src;
        }
        Image out (src.getFormat(), swap ? src.getHeight() : src.getWidth(),
                   swap ? src.getWidth() : src.getHeight(), true, SoftwareImageType());
        Graphics g (out);
        g.setImageResamplingQuality (Graphics::lowResamplingQuality);
        g.drawImageTransformed (src, t, false);
        return out;
    }

    Image renderSvg (const File& f, int maxSide, int* ow = nullptr, int* oh = nullptr)
    {
        auto drawable = Drawable::createFromSVGFile (f);
        if (drawable == nullptr) return {};
        auto b = drawable->getDrawableBounds();
        if (b.isEmpty()) return {};
        if (ow) *ow = roundToInt (b.getWidth());
        if (oh) *oh = roundToInt (b.getHeight());
        const float scale = (float) maxSide / jmax (b.getWidth(), b.getHeight());
        const int w = jmax (1, roundToInt (b.getWidth() * scale));
        const int h = jmax (1, roundToInt (b.getHeight() * scale));
        Image out (Image::ARGB, w, h, true, SoftwareImageType());
        Graphics g (out);
        drawable->drawWithin (g, Rectangle<float> (0, 0, (float) w, (float) h), RectanglePlacement::centred, 1.0f);
        return out;
    }
}

Image downscale (const Image& src, int maxSide)
{
    if (! src.isValid() || maxSide <= 0) return src;
    const int w = src.getWidth(), h = src.getHeight();
    if (jmax (w, h) <= maxSide) return src;
    const double s = (double) maxSide / jmax (w, h);
    const int tw = jmax (1, (int) std::round (w * s)), th = jmax (1, (int) std::round (h * s));

    Image cur = src;
    // polovičné kroky, aby zmenšenie nebolo zubaté
    while (cur.getWidth() / 2 >= tw * 2 && cur.getHeight() / 2 >= th * 2)
        cur = cur.rescaled (cur.getWidth() / 2, cur.getHeight() / 2, Graphics::mediumResamplingQuality);
    return cur.rescaled (tw, th, Graphics::highResamplingQuality);
}

Loaded loadFull (const File& f, int maxSide)
{
    Loaded r;
    if (f.getFileExtension().equalsIgnoreCase (".svg"))
    {
        r.image = renderSvg (f, maxSide, &r.origW, &r.origH);
        return r;
    }

    if (hasEmbeddedPreview (f))
    {
        r.image = embeddedPreview (f, maxSide, r.origW, r.origH);
        return r;
    }

    // Windows: rýchle zmenšené dekódovanie a veľa formátov
    {
        int w = 0, h = 0;
        auto im = win::loadImage (f, maxSide, w, h);
        if (im.isValid())
        {
            r.image = im;
            r.origW = w;
            r.origH = h;
            return r;
        }
    }

    int orientation = 1;
    if (isJpeg (f))
    {
        MemoryBlock head;
        orientation = readExif (f, head).orientation;
    }

    Image im = soft (ImageFileFormat::loadFrom (f));
    if (! im.isValid()) return r;
    im = applyOrientation (im, orientation);
    r.origW = im.getWidth();
    r.origH = im.getHeight();
    r.image = downscale (im, maxSide);
    return r;
}

Image loadThumb (const File& f, int side)
{
    if (f.getFileExtension().equalsIgnoreCase (".svg"))
        return renderSvg (f, side);

    const auto kind = kindOf (f);
    if (kind == Kind::Font)  return fontThumb (f, side);
    if (kind == Kind::Pdf)   return downscale (pdfThumb (f, side), side);
    if (kind == Kind::Model) return modelThumb (f, side);
    if (kind == Kind::Cad)   return cadThumb (f, side);
    if (kind == Kind::Text)  return textThumb (f, side);
    if (kind == Kind::Table) return tableThumb (f, side);
    if (kind == Kind::Slides) return slidesThumb (f, side);
    if (kind == Kind::App)   return appThumb (f, side);

    if (kindOf (f) != Kind::Image)
        return hasShellThumbnail (f) ? downscale (win::shellThumbnail (f, side), side) : Image();

    if (hasEmbeddedPreview (f))
    {
        int w = 0, h = 0;
        auto im = embeddedPreview (f, side, w, h);
        return im.isValid() ? downscale (im, side) : downscale (win::shellThumbnail (f, side), side);
    }

    ExifInfo ex;
    if (isJpeg (f))
    {
        MemoryBlock head;
        ex = readExif (f, head);
        if (ex.thumbOffset > 0)
        {
            MemoryInputStream mis (static_cast<const char*> (head.getData()) + ex.thumbOffset,
                                   (size_t) ex.thumbLength, false);
            JPEGImageFormat jpg;
            auto t = soft (jpg.decodeImage (mis));
            // malé EXIF náhľady (160 px) stačia len pre menšie dlaždice
            if (t.isValid() && jmax (t.getWidth(), t.getHeight()) >= side * 3 / 4)
                return downscale (applyOrientation (t, ex.orientation), side);
        }
    }

    {
        int w = 0, h = 0;
        auto im = win::loadImage (f, side, w, h);
        if (im.isValid()) return downscale (im, side);
    }

    Image im = soft (ImageFileFormat::loadFrom (f));
    if (im.isValid()) return downscale (applyOrientation (im, ex.orientation), side);

    // posledná možnosť: náhľad z Windows (napr. RAW s kodekom len pre Prieskumník)
    return downscale (win::shellThumbnail (f, side), side);
}
}
