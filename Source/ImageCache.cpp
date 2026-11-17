#include "ImageCache.h"

namespace kp
{
ImageCache::ImageCache()
{
    // veľkosť podľa najväčšej obrazovky (v pixeloch)
    int best = 1920;
    for (auto& d : Desktop::getInstance().getDisplays().displays)
        best = jmax (best, (int) (jmax (d.logicalBounds.getWidth(), d.logicalBounds.getHeight()) * d.scale));
    maxSide = jlimit (1920, 4096, best);
}

ImageCache::~ImageCache()
{
    *alive = false;
    loader.removeAllJobs (true, 3000);
}

std::shared_ptr<ImageCache::Entry> ImageCache::get (const File& f)
{
    const auto path = f.getFullPathName();
    if (images.contains (path)) return images[path];
    if (loading.contains (path)) return nullptr;
    loading.set (path, true);

    auto aliveFlag = alive;
    const int side = maxSide;
    loader.addJob ([this, f, path, side, aliveFlag]
    {
        auto loaded = img::loadFull (f, side);
        MessageManager::callAsync ([this, aliveFlag, path, loaded]
        {
            if (! *aliveFlag) return;
            loading.remove (path);
            auto e = std::make_shared<Entry>();
            e->loaded = loaded;
            // na grafickú kartu až tu (na hlavnom vlákne)
            e->display = loaded.image.isValid() ? NativeImageType().convert (loaded.image) : Image();
            images.set (path, e);
            order.push_back (path);
            while (order.size() > 7)
            {
                const auto old = order.front();
                order.pop_front();
                if (old == pinned) { order.push_back (old); if (order.size() <= 7) break; continue; }
                images.remove (old);
            }
            if (onLoaded) onLoaded (path);
        });
    });
    return nullptr;
}

void ImageCache::forget (const File& f)
{
    images.remove (f.getFullPathName());
}
}
