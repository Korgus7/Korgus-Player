#pragma once
#include "Common.h"
#include <map>

namespace kp
{
// Hodnotenie (0–5 hviezdičiek) a farebný štítok (0 = žiadny) pre súbory.
// Ukladá sa do súboru v priečinku nastavení, nie do samotných súborov.
class MetaStore : private Timer
{
public:
    struct Meta { int rating = 0, label = 0; };

    MetaStore();
    ~MetaStore() override;

    Meta get (const File& f) const;
    void setRating (const File& f, int stars);
    void setLabel (const File& f, int label);
    void moved (const File& from, const File& to);   // po presune / premenovaní

    static Colour labelColour (int label);
    static String labelName (int label);
    static constexpr int numLabels = 6;

private:
    void timerCallback() override { save(); stopTimer(); }
    void save();
    static String key (const File& f) { return f.getFullPathName().toLowerCase(); }
    void touch (const String& k, const Meta& m);

    File storeFile;
    std::map<String, Meta> data;
};
}
