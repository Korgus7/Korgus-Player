#include "ThumbCache.h"
#include "ImageLoader.h"

namespace kp
{
ThumbCache::ThumbCache (int s)
    : side (s),
      pool (ThreadPoolOptions{}.withThreadName ("Nahlady")
                               .withNumberOfThreads (jlimit (2, 8, SystemStats::getNumCpus() - 1)))
{
    // upratanie starých náhľadov na disku (na pozadí)
    pool.addJob ([] { trimCache(); });
}

// ---------------------------------------------------------------- náhľady na disku

File ThumbCache::cacheDir()
{
   #if JUCE_WINDOWS
    return File::getSpecialLocation (File::windowsLocalAppData).getChildFile ("Korgus Player").getChildFile ("Nahlady");
   #else
    return File::getSpecialLocation (File::tempDirectory).getChildFile ("korgus-player-nahlady");
   #endif
}

File ThumbCache::cacheFileFor (const File& f, int side)
{
    // kľúč: cesta + veľkosť + čas zmeny → po úprave súboru sa náhľad urobí znova
    const String key = f.getFullPathName().toLowerCase() + "|" + String (f.getSize()) + "|"
                     + String (f.getLastModificationTime().toMilliseconds()) + "|" + String (side) + "|2";
    const auto h = String::toHexString (key.hashCode64()).paddedLeft ('0', 16);
    return cacheDir().getChildFile (h.substring (0, 2)).getChildFile (h + ".kpt");
}

Image ThumbCache::makeThumb (const File& f, int side)
{
    const auto cf = cacheFileFor (f, side);
    if (cf.existsAsFile())
    {
        auto im = ImageFileFormat::loadFrom (cf);
        if (im.isValid()) return SoftwareImageType().convert (im);
    }

    auto im = img::loadThumb (f, side);
    if (! im.isValid()) return im;

    // obrázky s priehľadnosťou ako PNG, ostatné JPEG
    cf.getParentDirectory().createDirectory();
    const auto tmp = cf.getSiblingFile (cf.getFileName() + ".tmp");
    {
        FileOutputStream out (tmp);
        if (out.openedOk())
        {
            out.setPosition (0);
            out.truncate();
            if (im.hasAlphaChannel())
            {
                PNGImageFormat png;
                png.writeImageToStream (im, out);
            }
            else
            {
                JPEGImageFormat jpg;
                jpg.setQuality (0.88f);
                jpg.writeImageToStream (im, out);
            }
        }
    }
    tmp.moveFileTo (cf);
    return im;
}

void ThumbCache::trimCache()
{
    const auto dir = cacheDir();
    if (! dir.isDirectory()) return;
    struct Item { int64 time, size; File f; };
    std::vector<Item> items;
    int64 total = 0;
    for (const auto& de : RangedDirectoryIterator (dir, true, "*.kpt", File::findFiles))
    {
        items.push_back ({ de.getModificationTime().toMilliseconds(), de.getFileSize(), de.getFile() });
        total += de.getFileSize();
    }
    const int64 limit = (int64) 600 * 1024 * 1024;
    if (total <= limit) return;
    std::sort (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.time < b.time; });
    for (auto& it : items)
    {
        if (total <= limit * 3 / 4) break;
        if (it.f.deleteFile()) total -= it.size;
    }
}

ThumbCache::~ThumbCache()
{
    *alive = false;
    pool.removeAllJobs (true, 3000);
}

Image ThumbCache::get (const File& f)
{
    const auto path = f.getFullPathName();
    if (cache.contains (path)) return cache[path];
    if (failed.contains (path)) return {};

    const auto k = kindOf (f);
    if (k != Kind::Image && k != Kind::Svg && ! hasShellThumbnail (f)) return {};

    const int gen = generation.load();
    if (pending.contains (path) && pending[path] == gen) return {};
    pending.set (path, gen);

    auto aliveFlag = alive;
    const int s = side;
    pool.addJob ([this, f, path, gen, aliveFlag, s]
    {
        if (gen != generation.load()) return; // medzitým iný priečinok
        Image im = makeThumb (f, s);
        MessageManager::callAsync ([this, aliveFlag, path, im]
        {
            if (! *aliveFlag) return;
            pending.remove (path);
            store (path, im);
            if (onReady) onReady();
        });
    });
    return {};
}

void ThumbCache::store (const String& path, const Image& im)
{
    if (! im.isValid())
    {
        failed.set (path, true);
        return;
    }
    if (! cache.contains (path)) order.push_back (path);
    cache.set (path, im);
    while (order.size() > maxItems)
    {
        cache.remove (order.front());
        order.pop_front();
    }
}

void ThumbCache::cancelPending()
{
    ++generation;
    pool.removeAllJobs (false, 0);
    pending.clear();
}

void ThumbCache::forget (const File& f)
{
    const auto path = f.getFullPathName();
    cache.remove (path);
    failed.remove (path);
}
}
