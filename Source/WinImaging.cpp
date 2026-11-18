#include "WinImaging.h"
#include <map>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <wincodec.h>
 #include <shobjidl.h>
 #include <shlobj.h>
 #include <shlguid.h>
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

static Image shellImage (const File& f, int side, int flags);

Image shellThumbnail (const File& f, int side) { return shellImage (f, side, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK); }
Image shellIcon (const File& f, int side) { return shellImage (f, side, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK); }

static Image shellImage (const File& f, int side, int flags)
{
    ensureCom();
    Com<IShellItemImageFactory> sf;
    if (FAILED (SHCreateItemFromParsingName (f.getFullPathName().toWideCharPointer(), nullptr, IID_PPV_ARGS (&sf.p))))
        return {};
    HBITMAP hb = nullptr;
    if (FAILED (sf->GetImage ({ side, side }, (SIIGBF) flags, &hb)) || hb == nullptr)
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


Image decodeMemory (const void* data, size_t size, int maxSide)
{
    ensureCom();
    auto* fac = factory();
    if (fac == nullptr || size == 0) return {};
    Com<IWICStream> st;
    if (FAILED (fac->CreateStream (&st))) return {};
    if (FAILED (st->InitializeFromMemory ((BYTE*) const_cast<void*> (data), (DWORD) size))) return {};
    Com<IWICBitmapDecoder> dec;
    if (FAILED (fac->CreateDecoderFromStream (st.p, nullptr, WICDecodeMetadataCacheOnDemand, &dec))) return {};
    Com<IWICBitmapFrameDecode> frame;
    if (FAILED (dec->GetFrame (0, &frame))) return {};
    UINT w = 0, h = 0;
    frame->GetSize (&w, &h);
    IWICBitmapSource* src = frame.p;
    Com<IWICBitmapScaler> scaler;
    if (maxSide > 0 && (int) jmax (w, h) > maxSide)
    {
        const double sc = (double) maxSide / (double) jmax (w, h);
        if (SUCCEEDED (fac->CreateBitmapScaler (&scaler))
            && SUCCEEDED (scaler->Initialize (frame.p, jmax (1u, (UINT) std::lround (w * sc)), jmax (1u, (UINT) std::lround (h * sc)), WICBitmapInterpolationModeFant)))
            src = scaler.p;
    }
    return fromSource (src);
}

Image renderMetafile (const void* data, size_t size, int maxSide, int& origW, int& origH)
{
    auto* d = static_cast<const uint8*> (data);
    HENHMETAFILE emf = nullptr;
    if (size >= 44 && std::memcmp (d + 40, " EMF", 4) == 0)
        emf = SetEnhMetaFileBits ((UINT) size, d);
    else
    {
        // WMF, prípadne s „placeable“ hlavičkou (22 bajtov)
        size_t off = 0;
        METAFILEPICT mfp {};
        mfp.mm = MM_ANISOTROPIC;
        if (size >= 22 && d[0] == 0xd7 && d[1] == 0xcd && d[2] == 0xc6 && d[3] == 0x9a)
        {
            const auto i16 = [&] (size_t o) { return (int) (int16) (d[o] | (d[o + 1] << 8)); };
            const int l = i16 (6), t = i16 (8), r = i16 (10), b = i16 (12), inch = jmax (1, (int) (uint16) (d[14] | (d[15] << 8)));
            mfp.xExt = (r - l) * 2540 / inch;
            mfp.yExt = (b - t) * 2540 / inch;
            off = 22;
        }
        if (size > off) emf = SetWinMetaFileBits ((UINT) (size - off), d + off, nullptr, mfp.xExt > 0 ? &mfp : nullptr);
    }
    if (emf == nullptr) return {};

    ENHMETAHEADER hdr {};
    GetEnhMetaFileHeader (emf, sizeof (hdr), &hdr);
    double w = (hdr.rclFrame.right - hdr.rclFrame.left) * 96.0 / 2540.0;
    double h = (hdr.rclFrame.bottom - hdr.rclFrame.top) * 96.0 / 2540.0;
    if (w < 1 || h < 1) { w = hdr.rclBounds.right - hdr.rclBounds.left; h = hdr.rclBounds.bottom - hdr.rclBounds.top; }
    if (w < 1 || h < 1) { DeleteEnhMetaFile (emf); return {}; }
    origW = (int) std::lround (w);
    origH = (int) std::lround (h);
    const double sc = maxSide > 0 ? jmin (4.0, (double) maxSide / jmax (w, h)) : 1.0;
    const int iw = jmax (1, (int) std::lround (w * sc)), ih = jmax (1, (int) std::lround (h * sc));

    BITMAPINFO bi {};
    bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = iw;
    bi.bmiHeader.biHeight = -ih;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC (nullptr);
    HBITMAP dib = CreateDIBSection (dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    Image im;
    if (dib != nullptr && bits != nullptr)
    {
        auto old = SelectObject (dc, dib);
        RECT rc { 0, 0, iw, ih };
        FillRect (dc, &rc, (HBRUSH) GetStockObject (WHITE_BRUSH));
        SetStretchBltMode (dc, HALFTONE);
        PlayEnhMetaFile (dc, emf, &rc);
        GdiFlush();
        im = Image (Image::ARGB, iw, ih, false, SoftwareImageType());
        Image::BitmapData bd (im, Image::BitmapData::writeOnly);
        for (int y = 0; y < ih; ++y)
        {
            auto* dst = bd.getLinePointer (y);
            std::memcpy (dst, static_cast<uint8*> (bits) + (size_t) y * (size_t) iw * 4, (size_t) iw * 4);
            for (int x = 0; x < iw; ++x) dst[x * 4 + 3] = 255;
        }
        SelectObject (dc, old);
    }
    if (dib) DeleteObject (dib);
    DeleteDC (dc);
    DeleteEnhMetaFile (emf);
    return im;
}

static void forEachHandler (const File& f, std::function<bool (IAssocHandler*, int)> fn)
{
    ensureCom();
    Com<IEnumAssocHandlers> en;
    if (FAILED (SHAssocEnumHandlers (f.getFileExtension().toWideCharPointer(), ASSOC_FILTER_RECOMMENDED, &en))) return;
    int i = 0;
    while (true)
    {
        IAssocHandler* h = nullptr;
        ULONG got = 0;
        if (en->Next (1, &h, &got) != S_OK || got == 0 || h == nullptr) break;
        const bool stop = fn (h, i++);
        h->Release();
        if (stop) break;
    }
}

StringArray openWithApps (const File& f)
{
    StringArray names;
    forEachHandler (f, [&] (IAssocHandler* h, int)
    {
        LPWSTR n = nullptr;
        if (SUCCEEDED (h->GetUIName (&n)) && n != nullptr) { names.add (String (n)); CoTaskMemFree (n); }
        else names.add ("?");
        return names.size() >= 20;
    });
    return names;
}

bool openWithApp (const File& f, int index)
{
    bool ok = false;
    forEachHandler (f, [&] (IAssocHandler* h, int i)
    {
        if (i != index) return false;
        Com<IShellItem> item;
        if (SUCCEEDED (SHCreateItemFromParsingName (f.getFullPathName().toWideCharPointer(), nullptr, IID_PPV_ARGS (&item.p))))
        {
            Com<IDataObject> dobj;
            if (SUCCEEDED (item->BindToHandler (nullptr, BHID_DataObject, IID_PPV_ARGS (&dobj.p))))
                ok = SUCCEEDED (h->Invoke (dobj.p));
        }
        return true;
    });
    return ok;
}

String decodeAnsi (const char* data, size_t size)
{
    if (size == 0) return {};
    const int n = MultiByteToWideChar (CP_ACP, 0, data, (int) size, nullptr, 0);
    if (n <= 0) return String::createStringFromData (data, (int) size);
    std::vector<wchar_t> buf ((size_t) n + 1, 0);
    MultiByteToWideChar (CP_ACP, 0, data, (int) size, buf.data(), n);
    return String (buf.data(), (size_t) n);
}

StringPairArray versionInfo (const File& f)
{
    StringPairArray r;
    const auto path = f.getFullPathName();
    DWORD dummy = 0;
    const DWORD len = GetFileVersionInfoSizeW (path.toWideCharPointer(), &dummy);
    if (len == 0) return r;
    std::vector<uint8> buf (len);
    if (! GetFileVersionInfoW (path.toWideCharPointer(), 0, len, buf.data())) return r;
    struct LangCp { WORD lang, cp; };
    LangCp* tr = nullptr;
    UINT trLen = 0;
    String prefix = "040904b0";
    if (VerQueryValueW (buf.data(), L"\\VarFileInfo\\Translation", (LPVOID*) &tr, &trLen) && trLen >= sizeof (LangCp))
        prefix = String::toHexString ((int) tr->lang).paddedLeft ('0', 4) + String::toHexString ((int) tr->cp).paddedLeft ('0', 4);
    for (auto key : { "ProductName", "FileDescription", "CompanyName", "FileVersion", "ProductVersion", "LegalCopyright" })
    {
        const String q = "\\StringFileInfo\\" + prefix + "\\" + key;
        wchar_t* val = nullptr;
        UINT vlen = 0;
        if (VerQueryValueW (buf.data(), q.toWideCharPointer(), (LPVOID*) &val, &vlen) && vlen > 0 && val != nullptr)
            r.set (key, String (val).trim());
    }
    return r;
}

int printFiles (const Array<File>& files)
{
    CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    // zoskupiť podľa priečinka – kontextové menu vie viac súborov len z jedného priečinka
    std::map<String, Array<File>> groups;
    for (auto& f : files) if (f.existsAsFile()) groups[f.getParentDirectory().getFullPathName()].add (f);
    int sent = 0;
    for (auto& [dir, list] : groups)
    {
        PIDLIST_ABSOLUTE folderPidl = nullptr;
        if (FAILED (SHParseDisplayName (dir.toWideCharPointer(), nullptr, &folderPidl, 0, nullptr))) continue;
        IShellFolder* folder = nullptr;
        if (SUCCEEDED (SHBindToObject (nullptr, folderPidl, nullptr, IID_IShellFolder, (void**) &folder)))
        {
            std::vector<PITEMID_CHILD> items;
            for (auto& f : list)
            {
                PITEMID_CHILD child = nullptr;
                ULONG eaten = 0;
                std::wstring name (f.getFileName().toWideCharPointer());
                if (SUCCEEDED (folder->ParseDisplayName (nullptr, nullptr, name.data(), &eaten, (PIDLIST_RELATIVE*) &child, nullptr)))
                    items.push_back (child);
            }
            IContextMenu* menu = nullptr;
            if (! items.empty()
                && SUCCEEDED (folder->GetUIObjectOf (nullptr, (UINT) items.size(), (PCUITEMID_CHILD_ARRAY) items.data(), IID_IContextMenu, nullptr, (void**) &menu)))
            {
                HMENU h = CreatePopupMenu();
                menu->QueryContextMenu (h, 0, 1, 0x7fff, CMF_NORMAL);
                CMINVOKECOMMANDINFOEX info {};
                info.cbSize = sizeof (info);
                info.fMask = CMIC_MASK_UNICODE;
                info.lpVerb = "print";
                info.lpVerbW = L"print";
                info.nShow = SW_SHOWNORMAL;
                if (SUCCEEDED (menu->InvokeCommand ((CMINVOKECOMMANDINFO*) &info))) sent += (int) items.size();
                else
                    for (auto& f : list)   // náhradne po jednom
                        if ((INT_PTR) ShellExecuteW (nullptr, L"print", f.getFullPathName().toWideCharPointer(), nullptr, nullptr, SW_SHOWNORMAL) > 32) ++sent;
                DestroyMenu (h);
                menu->Release();
            }
            for (auto c : items) CoTaskMemFree (c);
            folder->Release();
        }
        CoTaskMemFree (folderPidl);
    }
    return sent;
}

#else

int printFiles (const Array<File>&) { return 0; }
Image loadImage (const File&, int, int&, int&) { return {}; }
Image shellThumbnail (const File&, int) { return {}; }
Image shellIcon (const File&, int) { return {}; }
Image decodeMemory (const void*, size_t, int) { return {}; }
Image renderMetafile (const void*, size_t, int, int&, int&) { return {}; }
String decodeAnsi (const char* data, size_t size)
{
    String s;
    for (size_t i = 0; i < size; ++i) s << String::charToString ((juce_wchar) (uint8) data[i]);   // Latin-1
    return s;
}
StringPairArray versionInfo (const File&) { return {}; }
StringArray openWithApps (const File&) { return {}; }
bool openWithApp (const File&, int) { return false; }

#endif
}
