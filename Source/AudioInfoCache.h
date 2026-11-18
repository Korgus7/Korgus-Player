#pragma once
#include "Common.h"
#include <memory>

namespace kp
{
// Údaje o zvukových súboroch pre zoznam: dĺžka, formát a malá vlna. Počíta sa na pozadí.
class AudioInfoCache
{
public:
    struct Info
    {
        bool ok = false;
        double length = 0, sampleRate = 0;
        int bits = 0, channels = 0;
        std::vector<float> peaks;   // 0..1, jeden stĺpec vlny na prvok
        String format;              // text do stĺpca Formát
        double bpm = 0;
        String key;
    };

    AudioInfoCache();
    ~AudioInfoCache();

    // Hotové údaje, alebo nullptr (vtedy sa naplánuje výpočet)
    const Info* get (const File& f);
    void cancelPending();
    void forget (const File& f) { infos.remove (f.getFullPathName()); }

    std::function<void()> onReady;
    // výpočet hneď (pre export; volať z vlákna na pozadí)
    Info computeNow (const File& f) { return compute (formats, f); }

    static constexpr int numPeaks = 96;

private:
    static Info compute (AudioFormatManager& fm, const File& f);

    AudioFormatManager formats;
    ThreadPool pool;
    std::atomic<int> generation { 0 };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    HashMap<String, std::shared_ptr<Info>> infos;
    HashMap<String, int> pending;
};
}
