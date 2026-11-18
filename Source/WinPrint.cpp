#include "WinPrint.h"
#include "ImageLoader.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <commdlg.h>
 #include <shellapi.h>
#endif

namespace kp::win
{
#if JUCE_WINDOWS
static bool printImages (const Array<File>& images, HWND owner, PrintResult& res)
{
    PRINTDLGW pd {};
    pd.lStructSize = sizeof (pd);
    pd.hwndOwner = owner;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS | PD_USEDEVMODECOPIESANDCOLLATE;
    pd.nCopies = 1;
    if (! PrintDlgW (&pd))
    {
        if (CommDlgExtendedError() != 0) res.error = tr ("Okno tlače sa nedá otvoriť (je nainštalovaná tlačiareň?).");
        else res.cancelled = true;
        return false;
    }
    HDC dc = pd.hDC;
    DOCINFOW di {};
    di.cbSize = sizeof (di);
    di.lpszDocName = L"Korgus Player";
    if (StartDocW (dc, &di) <= 0) { res.error = tr ("Tlač sa nepodarilo spustiť."); DeleteDC (dc); return false; }

    const int pageW = GetDeviceCaps (dc, HORZRES), pageH = GetDeviceCaps (dc, VERTRES);
    for (auto& f : images)
    {
        auto im = img::loadFull (f, 4000).image;
        if (! im.isValid()) { res.error = tr ("Obrázok sa nedá načítať: ") + f.getFileName(); continue; }
        // na šírku na stranu na výšku → otočiť, aby bol obrázok čo najväčší
        if ((im.getWidth() > im.getHeight()) != (pageW > pageH))
        {
            Image rot (Image::RGB, im.getHeight(), im.getWidth(), true, SoftwareImageType());
            Graphics g (rot);
            g.fillAll (Colours::white);
            g.drawImageTransformed (im, AffineTransform::rotation (MathConstants<float>::halfPi).translated ((float) im.getHeight(), 0.0f));
            im = rot;
        }
        // 24-bit DIB (riadky zdola, zarovnané na 4 bajty), biele pozadie pod priehľadnosťou
        const int w = im.getWidth(), h = im.getHeight();
        const int stride = (w * 3 + 3) & ~3;
        HeapBlock<uint8> bits ((size_t) stride * (size_t) h, true);
        {
            Image::BitmapData bd (im, Image::BitmapData::readOnly);
            for (int y = 0; y < h; ++y)
            {
                uint8* row = bits + (size_t) (h - 1 - y) * (size_t) stride;
                for (int x = 0; x < w; ++x)
                {
                    const auto c = bd.getPixelColour (x, y);
                    const float a = c.getFloatAlpha();
                    row[x * 3 + 0] = (uint8) roundToInt (c.getBlue() * a + 255.0f * (1.0f - a));
                    row[x * 3 + 1] = (uint8) roundToInt (c.getGreen() * a + 255.0f * (1.0f - a));
                    row[x * 3 + 2] = (uint8) roundToInt (c.getRed() * a + 255.0f * (1.0f - a));
                }
            }
        }
        BITMAPINFO bmi {};
        bmi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        const double scale = jmin ((double) pageW / w, (double) pageH / h);
        const int dw = (int) (w * scale), dh = (int) (h * scale);
        const int dx = (pageW - dw) / 2, dy = (pageH - dh) / 2;
        if (StartPage (dc) <= 0) { res.error = tr ("Tlač sa nepodarilo spustiť."); break; }
        SetStretchBltMode (dc, HALFTONE);
        SetBrushOrgEx (dc, 0, 0, nullptr);
        StretchDIBits (dc, dx, dy, dw, dh, 0, 0, w, h, bits.getData(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        EndPage (dc);
        ++res.sent;
    }
    EndDoc (dc);
    DeleteDC (dc);
    if (pd.hDevMode != nullptr) GlobalFree (pd.hDevMode);
    if (pd.hDevNames != nullptr) GlobalFree (pd.hDevNames);
    return res.sent > 0;
}

PrintResult printFiles (const Array<File>& files, void* ownerWindow)
{
    PrintResult res;
    Array<File> images, others;
    for (auto& f : files)
    {
        if (! f.existsAsFile()) continue;
        const auto k = kindOf (f);
        (k == Kind::Image ? images : others).add (f);
    }
    if (! images.isEmpty()) printImages (images, (HWND) ownerWindow, res);

    for (auto& f : others)
    {
        SHELLEXECUTEINFOW sei {};
        sei.cbSize = sizeof (sei);
        // NOASYNC: bez toho Windows tlač zruší, keď sa volanie vráti
        sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        sei.hwnd = (HWND) ownerWindow;
        sei.lpVerb = L"print";
        const std::wstring path (f.getFullPathName().toWideCharPointer());
        sei.lpFile = path.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW (&sei)) ++res.sent;
        else
        {
            // niektoré programy nemajú „print“ – aspoň otvoriť, nech sa dá tlačiť z nich
            res.error = tr ("Windows nemá pre ") + f.getFileExtension() + tr (" priamu tlač – otváram súbor, vytlač ho z programu (Ctrl+P).");
            f.startAsProcess();
        }
    }
    return res;
}
#else
PrintResult printFiles (const Array<File>&, void*) { PrintResult r; r.error = tr ("Tlač je len vo Windows."); return r; }
#endif
}
