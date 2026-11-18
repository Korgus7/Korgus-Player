#include "WinPrint.h"
#include "ImageLoader.h"
#include "WinImaging.h"

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
static Image renderSvg (const File& f)
{
    auto d = Drawable::createFromSVGFile (f);
    if (d == nullptr) return {};
    auto b = d->getDrawableBounds();
    if (b.isEmpty()) return {};
    const float scale = 3000.0f / jmax (b.getWidth(), b.getHeight());
    Image im (Image::ARGB, jmax (1, roundToInt (b.getWidth() * scale)), jmax (1, roundToInt (b.getHeight() * scale)), true, SoftwareImageType());
    Graphics g (im);
    d->drawWithin (g, im.getBounds().toFloat(), RectanglePlacement::centred, 1.0f);
    return im;
}

static String readText (const File& f)
{
    MemoryBlock mb;
    if (! f.loadFileAsData (mb)) return {};
    auto* p = static_cast<const char*> (mb.getData());
    size_t n = mb.getSize();
    if (n >= 3 && (uint8) p[0] == 0xef && (uint8) p[1] == 0xbb && (uint8) p[2] == 0xbf) { p += 3; n -= 3; }
    if (n >= 2 && (uint8) p[0] == 0xff && (uint8) p[1] == 0xfe)
        return String (CharPointer_UTF16 (reinterpret_cast<const CharPointer_UTF16::CharType*> (p + 2)), CharPointer_UTF16 (reinterpret_cast<const CharPointer_UTF16::CharType*> (p + n)));
    if (CharPointer_UTF8::isValidString (p, (int) n)) return String::fromUTF8 (p, (int) n);
    return decodeAnsi (p, n);   // staršie texty v kódovaní Windows (1250)
}

// text: Consolas 10 pt, okraje 15 mm, zalamovanie dlhých riadkov, číslo strany dole
static void printText (HDC dc, const File& f, PrintResult& res)
{
    const auto text = readText (f).replace ("\t", "    ");
    const int dpiX = GetDeviceCaps (dc, LOGPIXELSX), dpiY = GetDeviceCaps (dc, LOGPIXELSY);
    const int offX = GetDeviceCaps (dc, PHYSICALOFFSETX), offY = GetDeviceCaps (dc, PHYSICALOFFSETY);
    const int physW = GetDeviceCaps (dc, PHYSICALWIDTH), physH = GetDeviceCaps (dc, PHYSICALHEIGHT);
    const int mx = dpiX * 15 / 25 - offX, my = dpiY * 15 / 25 - offY;   // 15 mm od okraja papiera
    const int right = physW - offX - dpiX * 15 / 25, bottom = physH - offY - dpiY * 15 / 25;
    HFONT font = CreateFontW (-MulDiv (10, dpiY, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    HFONT footFont = CreateFontW (-MulDiv (8, dpiY, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    auto old = SelectObject (dc, font);
    TEXTMETRICW tm {};
    GetTextMetricsW (dc, &tm);
    const int lineH = tm.tmHeight + tm.tmExternalLeading;
    const int width = right - mx;

    // rozdeliť na riadky strany (zalomiť podľa šírky)
    StringArray out;
    for (auto& line : StringArray::fromLines (text))
    {
        std::wstring w (line.toWideCharPointer());
        if (w.empty()) { out.add ({}); continue; }
        size_t pos = 0;
        while (pos < w.size())
        {
            int fit = 0;
            SIZE sz {};
            GetTextExtentExPointW (dc, w.c_str() + pos, (int) (w.size() - pos), width, &fit, nullptr, &sz);
            if (fit <= 0) fit = 1;
            size_t take = (size_t) fit;
            if (pos + take < w.size())   // zalomiť na medzere, ak sa dá
            {
                const auto sp = w.rfind (L' ', pos + take);
                if (sp != std::wstring::npos && sp > pos + take / 2) take = sp - pos + 1;
            }
            out.add (String (w.substr (pos, take).c_str()));
            pos += take;
        }
    }
    const int perPage = jmax (1, (bottom - my - lineH * 2) / lineH);
    const int pages = jmax (1, (out.size() + perPage - 1) / perPage);
    for (int p = 0; p < pages; ++p)
    {
        if (StartPage (dc) <= 0) { res.error = tr ("Tlač sa nepodarilo spustiť."); break; }
        SelectObject (dc, font);
        SetBkMode (dc, TRANSPARENT);
        for (int i = 0; i < perPage; ++i)
        {
            const int idx = p * perPage + i;
            if (idx >= out.size()) break;
            const auto s = out[idx];
            TextOutW (dc, mx, my + i * lineH, s.toWideCharPointer(), s.length());
        }
        SelectObject (dc, footFont);
        const String foot = f.getFileName() + "   " + String (p + 1) + " / " + String (pages);
        SetTextAlign (dc, TA_RIGHT);
        TextOutW (dc, right, bottom - lineH, foot.toWideCharPointer(), foot.length());
        SetTextAlign (dc, TA_LEFT);
        EndPage (dc);
    }
    SelectObject (dc, old);
    DeleteObject (font);
    DeleteObject (footFont);
    ++res.sent;
}

static bool printImages (const Array<File>& images, const Array<File>& texts, HWND owner, PrintResult& res)
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
        auto im = kindOf (f) == Kind::Svg ? renderSvg (f) : img::loadFull (f, 4000).image;
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
    for (auto& f : texts) printText (dc, f, res);
    EndDoc (dc);
    DeleteDC (dc);
    if (pd.hDevMode != nullptr) GlobalFree (pd.hDevMode);
    if (pd.hDevNames != nullptr) GlobalFree (pd.hDevNames);
    return res.sent > 0;
}

PrintResult printFiles (const Array<File>& files, void* ownerWindow)
{
    PrintResult res;
    Array<File> images, texts, others;
    for (auto& f : files)
    {
        if (! f.existsAsFile()) continue;
        const auto k = kindOf (f);
        if (k == Kind::Image || k == Kind::Svg) images.add (f);
        else if ((k == Kind::Text && ! f.hasFileExtension ("rtf;docx;odt;doc")) || (k == Kind::Table && f.hasFileExtension ("csv;tsv"))) texts.add (f);
        else others.add (f);
    }
    if (! images.isEmpty() || ! texts.isEmpty()) printImages (images, texts, (HWND) ownerWindow, res);

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
