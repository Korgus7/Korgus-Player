#pragma once
#include "Common.h"

namespace kp::similar
{
    // Rozdielový odtlačok obrázka (dHash, 64 bitov) – podobné fotky majú malú Hammingovu vzdialenosť
    struct ImageSig { uint64 hash = 0; uint8 colours[4 * 4 * 3] {}; };
    bool imageHash (const File& f, ImageSig& sig);
    // podobné = rovnaká štruktúra (dHash) aj farby
    bool similarImages (const ImageSig& a, const ImageSig& b);
    inline int distance (uint64 a, uint64 b) { int n = 0; for (uint64 x = a ^ b; x != 0; x &= x - 1) ++n; return n; }

    // Charakteristika zvuku (dĺžka, hlasitosť, jas, nástup, doznievanie, pásma…)
    struct SoundFeatures { bool ok = false; std::vector<float> v; };
    SoundFeatures soundFeatures (AudioFormatManager& fm, const File& f);
}
