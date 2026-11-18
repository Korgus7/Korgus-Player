#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Vykreslenie strany PDF cez Windows (Windows.Data.Pdf, Windows 10+). Bez JUCE hlavičiek.
// Volať len z vlákien na pozadí. Na iných systémoch vráti false.
namespace kp::pdf
{
    struct Page
    {
        std::vector<uint8_t> png;
        int pageCount = 0;
        double width = 0, height = 0;   // veľkosť strany v bodoch
    };

    bool render (const std::wstring& path, int pageIndex, int pixelWidth, Page& out);
}
