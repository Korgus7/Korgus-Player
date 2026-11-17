#include "ThumbCache.h"
#include "ImageLoader.h"

namespace kp
{
ThumbCache::ThumbCache (int s)
    : side (s),
      pool (ThreadPoolOptions{}.withThreadName ("Nahlady")
                               .withNumberOfThreads (jlimit (2, 8, SystemStats::getNumCpus() - 1)))
{
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
    if (k != Kind::Image && k != Kind::Svg) return {};

    const int gen = generation.load();
    if (pending.contains (path) && pending[path] == gen) return {};
    pending.set (path, gen);

    auto aliveFlag = alive;
    const int s = side;
    pool.addJob ([this, f, path, gen, aliveFlag, s]
    {
        if (gen != generation.load()) return; // medzitým iný priečinok
        Image im = img::loadThumb (f, s);
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
