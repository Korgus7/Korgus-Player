#include "WinImaging.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <wincodec.h>
 #include <shobjidl.h>
#endif

namespace kp::win
{
#if JUCE_WINDOWS
namespace
{
    template <class T> struct Com
    {
        T* p = nullptr;
        ~Com() { if (p) p->Release(); }
        T** operator&() { return &p; }
        T* operator->() const { return p; }
        explicit operator bool() const { return p != nullptr; }
    };

    // COM musí byť zapnutý v každom vlákne zvlášť
    void ensureCom()
    {
        thread_local bool done = false;
        if (! done)
        {
            CoInitializeEx (nullptr, COINIT_MULTITHREADED);
            done = true;
        }
    }

    IWICImagingFactory* factory()
    {
        thread_local Com<IWICImagingFactory> f;
        if (! f)
            CoCreateInstance (CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&f.p));
        return f.p;
    }

    int readOrientation (IWICBitmapFrameDecode* frame)
    {
        Com<IWICMetadataQueryReader> q;
        if (FAILED (frame->GetMetadataQueryReader (&q))) return 1;
        PROPVARIANT v;
        PropVariantInit (&v);
        int o = 1;
        if (SUCCEEDED (q->GetMetadataByName (L"System.Photo.Orientation", &v)))
        {
            if (v.vt == VT_UI2) o = v.uiVal;
            else if (v.vt == VT_UI4) o = (int) v.ulVal;
        }
        PropVariantClear (&v);
        return (o >= 1 && o <= 8) ? o : 1;
    }

    Image fromSource (IWICBitmapSource* src)
    {
        UINT w = 0, h = 0;
        if (FAILED (src->GetSize (&w, &h)) || w == 0 || h == 0 || w > 30000 || h > 30000) return {};

        Com<IWICFormatConverter> conv;
        if (FAILED (factory()->CreateFormatConverter (&conv))) return {};
        if (FAILED (conv->Initialize (src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                      nullptr, 0.0, WICBitmapPaletteTypeCustom)))
            return {};

        // JUCE ARGB = predelené BGRA v pamäti, rovnako ako PBGRA
        Image im (Image::ARGB, (int) w, (int) h, false, SoftwareImageType());
        Image::BitmapData bd (im, Image::BitmapData::writeOnly);
        if (FAILED (conv->CopyPixels (nullptr, (UINT) bd.lineStride, (UINT) (bd.lineStride * (int) h), bd.data)))
            return {};
        return im;
    }

    Image orient (const Image& src, int o)
    {
        if (o <= 1 || ! src.isValid()) return src;
        const float w = (float) src.getWidth(), h = (float) src.getHeight();
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
        const bool swap = o >= 5;
        Image out (src.getFormat(), swap ? src.getHeight() : src.getWidth(), swap ? src.getWidth() : src.getHeight(),
                   true, SoftwareImageType());
        Graphics g (out);
        g.setImageResamplingQuality (Graphics::lowResamplingQuality);
        g.drawImageTransformed (src, t, false);
        return out;
    }
}

Image loadImage (const File& f, int maxSide, int& origW, int& origH)
{
    ensureCom();
    auto* fac = factory();
    if (fac == nullptr) return {};

    Com<IWICBitmapDecoder> dec;
    if (FAILED (fac->CreateDecoderFromFilename (f.getFullPathName().toWideCharPointer(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnDemand, &dec)))
        return {};
    Com<IWICBitmapFrameDecode> frame;
    if (FAILED (dec->GetFrame (0, &frame))) return {};

    UINT w = 0, h = 0;
    if (FAILED (frame->GetSize (&w, &h)) || w == 0 || h == 0) return {};
    const int o = readOrientation (frame.p);
    const bool swap = o >= 5;
    origW = (int) (swap ? h : w);
    origH = (int) (swap ? w : h);

    IWICBitmapSource* src = frame.p;
    Com<IWICBitmapScaler> scaler;
    if (maxSide > 0 && (int) jmax (w, h) > maxSide)
    {
        const double s = (double) maxSide / (double) jmax (w, h);
        const UINT tw = jmax (1u, (UINT) std::lround (w * s)), th = jmax (1u, (UINT) std::lround (h * s));
        // Fant pri JPEG využije zmenšenie priamo v dekodéri (rýchle)
        if (SUCCEEDED (fac->CreateBitmapScaler (&scaler))
            && SUCCEEDED (scaler->Initialize (frame.p, tw, th, WICBitmapInterpolationModeFant)))
            src = scaler.p;
    }
    return orient (fromSource (src), o);
}

Image shellThumbnail (const File& f, int side)
{
    ensureCom();
    Com<IShellItemImageFactory> sf;
    if (FAILED (SHCreateItemFromParsingName (f.getFullPathName().toWideCharPointer(), nullptr, IID_PPV_ARGS (&sf.p))))
        return {};
    HBITMAP hb = nullptr;
    if (FAILED (sf->GetImage ({ side, side }, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &hb)) || hb == nullptr)
        return {};

    BITMAP bm {};
    GetObject (hb, sizeof (bm), &bm);
    Image im;
    if (bm.bmWidth > 0 && bm.bmHeight > 0)
    {
        BITMAPINFO bi {};
        bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = bm.bmWidth;
        bi.bmiHeader.biHeight = -bm.bmHeight;   // zhora dole
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        im = Image (Image::ARGB, bm.bmWidth, bm.bmHeight, false, SoftwareImageType());
        Image::BitmapData bd (im, Image::BitmapData::readWrite);
        HDC dc = GetDC (nullptr);
        std::vector<uint8> buf ((size_t) bm.bmWidth * (size_t) bm.bmHeight * 4);
        const bool ok = GetDIBits (dc, hb, 0, (UINT) bm.bmHeight, buf.data(), &bi, DIB_RGB_COLORS) != 0;
        ReleaseDC (nullptr, dc);
        if (ok)
        {
            bool anyAlpha = false;
            for (size_t i = 3; i < buf.size(); i += 4) if (buf[i] != 0) { anyAlpha = true; break; }
            for (int y = 0; y < bm.bmHeight; ++y)
            {
                auto* dst = bd.getLinePointer (y);
                const auto* s = buf.data() + (size_t) y * (size_t) bm.bmWidth * 4;
                std::memcpy (dst, s, (size_t) bm.bmWidth * 4);
                if (! anyAlpha)
                    for (int x = 0; x < bm.bmWidth; ++x) dst[x * 4 + 3] = 255;
            }
        }
        else im = {};
    }
    DeleteObject (hb);
    return im;
}

#else

Image loadImage (const File&, int, int&, int&) { return {}; }
Image shellThumbnail (const File&, int) { return {}; }

#endif
}
