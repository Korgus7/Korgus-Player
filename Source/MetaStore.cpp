#include "MetaStore.h"

namespace kp
{
MetaStore::MetaStore()
{
   #if JUCE_WINDOWS
    storeFile = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Korgus Player").getChildFile ("hodnotenia.xml");
   #else
    storeFile = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Korgus Player").getChildFile ("hodnotenia.xml");
   #endif
    if (auto xml = parseXML (storeFile))
        for (auto* e : xml->getChildIterator())
        {
            Meta m { e->getIntAttribute ("r"), e->getIntAttribute ("l") };
            if (m.rating > 0 || m.label > 0) data[e->getStringAttribute ("p")] = m;
        }
}

MetaStore::~MetaStore()
{
    if (isTimerRunning()) save();
}

MetaStore::Meta MetaStore::get (const File& f) const
{
    auto it = data.find (key (f));
    return it != data.end() ? it->second : Meta();
}

void MetaStore::touch (const String& k, const Meta& m)
{
    if (m.rating <= 0 && m.label <= 0) data.erase (k);
    else data[k] = m;
    startTimer (1000);   // uloženie o chvíľu (viac zmien naraz)
}

void MetaStore::setRating (const File& f, int stars)
{
    auto m = get (f);
    m.rating = jlimit (0, 5, stars);
    touch (key (f), m);
}

void MetaStore::setLabel (const File& f, int label)
{
    auto m = get (f);
    m.label = jlimit (0, numLabels, label);
    touch (key (f), m);
}

void MetaStore::moved (const File& from, const File& to)
{
    auto it = data.find (key (from));
    if (it == data.end()) return;
    const auto m = it->second;
    data.erase (it);
    touch (key (to), m);
}

void MetaStore::save()
{
    XmlElement root ("HODNOTENIA");
    for (auto& [p, m] : data)
    {
        auto* e = root.createNewChildElement ("s");
        e->setAttribute ("p", p);
        e->setAttribute ("r", m.rating);
        e->setAttribute ("l", m.label);
    }
    storeFile.getParentDirectory().createDirectory();
    root.writeTo (storeFile);
}

Colour MetaStore::labelColour (int label)
{
    static const uint32 c[] = { 0x00000000, 0xffd9534f, 0xfff0ad4e, 0xfff4d03f, 0xff5cb85c, 0xff4a90d9, 0xff9b59b6 };
    return Colour (c[jlimit (0, numLabels, label)]);
}

String MetaStore::labelName (int label)
{
    static const char* n[] = { "Bez štítka", "Červený", "Oranžový", "Žltý", "Zelený", "Modrý", "Fialový" };
    return tr (n[jlimit (0, numLabels, label)]);
}
}
