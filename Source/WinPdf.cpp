#include "WinPdf.h"

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <winrt/Windows.Foundation.h>
 #include <winrt/Windows.Storage.h>
 #include <winrt/Windows.Storage.Streams.h>
 #include <winrt/Windows.Graphics.Imaging.h>
 #include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.UI.h>

namespace kp::pdf
{
bool render (const std::wstring& path, int pageIndex, int pixelWidth, Page& out)
{
    using namespace winrt;
    using namespace winrt::Windows::Storage;
    using namespace winrt::Windows::Storage::Streams;
    using namespace winrt::Windows::Data::Pdf;

    try
    {
        try { init_apartment (apartment_type::multi_threaded); } catch (...) {}

        auto file = StorageFile::GetFileFromPathAsync (path).get();
        auto doc = PdfDocument::LoadFromFileAsync (file).get();
        out.pageCount = (int) doc.PageCount();
        if (pageIndex < 0 || pageIndex >= out.pageCount) return false;

        auto page = doc.GetPage ((uint32_t) pageIndex);
        const auto size = page.Size();
        out.width = size.Width;
        out.height = size.Height;

        InMemoryRandomAccessStream stream;
        PdfPageRenderOptions opt;
        opt.DestinationWidth ((uint32_t) (pixelWidth > 0 ? pixelWidth : 1200));
        opt.BackgroundColor (winrt::Windows::UI::Colors::White());
        opt.BitmapEncoderId (winrt::Windows::Graphics::Imaging::BitmapEncoder::PngEncoderId());
        page.RenderToStreamAsync (stream, opt).get();

        const auto n = (uint32_t) stream.Size();
        DataReader reader (stream.GetInputStreamAt (0));
        reader.LoadAsync (n).get();
        out.png.resize (n);
        reader.ReadBytes (out.png);
        return n > 0;
    }
    catch (...)
    {
        return false;
    }
}
}

#else

namespace kp::pdf
{
bool render (const std::wstring&, int, int, Page&) { return false; }
}

#endif
