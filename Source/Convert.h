#pragma once
#include "Common.h"

namespace kp::convert
{
    enum class Target { Jpg, Png, Wav16, Wav24, Flac, Ogg, Mp3, M4a };
    struct Option { Target target; String label; bool audio; };
    // ponuka formátov podľa toho, čo je označené (obrázky / zvuk)
    std::vector<Option> optionsFor (bool images, bool audio);
    String extensionOf (Target t);
    // prevedie jeden súbor vedľa originálu (nový názov, nič neprepíše); vráti cieľ alebo chybu
    bool run (const File& src, Target t, File& out, String& error);
}
