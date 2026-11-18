#pragma once
#include "Common.h"
#include "ImageLoader.h"
#include <deque>
#include <memory>

namespace kp
{
// Veľké obrázky na zobrazenie (ukážka aj celá obrazovka). Načítava na pozadí, drží posledných pár.
class ImageCache
{
public:
    struct Entry
    {
        img::Loaded loaded;
        Image display;   // pripravený na kreslenie (GPU)
    };

    ImageCache();
    ~ImageCache();

    // Hotový obrázok, alebo nullptr (vtedy sa začne načítavať)
    std::shared_ptr<Entry> get (const File& f);
    void prefetch (const File& f) { get (f); }
    void forget (const File& f);

    // Obrázok, ktorý sa nesmie vyhodiť (práve zobrazený)
    void setPinned (const File& f) { pinned = f.getFullPathName(); }

    std::function<void (const String& path)> onLoaded;
    int getMaxSide() const { return maxSide; }

private:
    ThreadPool loader { ThreadPoolOptions{}.withThreadName ("Obrazky").withNumberOfThreads (2) };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    HashMap<String, std::shared_ptr<Entry>> images;
    std::deque<String> order;
    HashMap<String, bool> loading;
    HashMap<String, int> versions;   // zvýši sa pri forget – staršie výsledky sa zahodia
    String pinned;
    int maxSide = 2560;
};
}
