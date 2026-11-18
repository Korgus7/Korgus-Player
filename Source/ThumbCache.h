#pragma once
#include "Common.h"
#include <atomic>
#include <deque>
#include <memory>

namespace kp
{
// Náhľady sa robia na pozadí vo viacerých vláknach a držia sa v pamäti.
class ThumbCache
{
public:
    explicit ThumbCache (int side);
    ~ThumbCache();

    // Hotový náhľad, alebo prázdny obrázok (vtedy sa naplánuje výroba).
    Image get (const File& f);

    // Zahodí čakajúce úlohy (pri prechode do iného priečinka)
    void cancelPending();

    // Zabudne náhľad (napr. po presune súboru)
    void forget (const File& f);

    std::function<void()> onReady;
    int getSide() const { return side; }

private:
    void store (const String& path, const Image& im);
    static File cacheDir();
    static File cacheFileFor (const File& f, int side);
    static Image makeThumb (const File& f, int side);   // z disku alebo nový
    static void trimCache();

    const int side;
    ThreadPool pool;
    std::atomic<int> generation { 0 };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    HashMap<String, Image> cache;
    HashMap<String, int> pending;   // cesta -> generácia
    HashMap<String, bool> failed;
    std::deque<String> order;
    static constexpr size_t maxItems = 1500;
};
}
