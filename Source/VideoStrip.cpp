#include "VideoStrip.h"
#include <thread>
#include <cstring>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

namespace kp
{
namespace
{
    // Minimum z libmpv client API (stabilné C rozhranie), načítané dynamicky.
    // Windows: libmpv-2.dll vedľa programu (rovnaká ako pri prehrávaní videa), inde libmpv.so.2 (len na skúšanie).
    struct mpv_handle;
    struct mpv_node_list;
    struct mpv_byte_array { void* data; size_t size; };
    struct mpv_node { union { char* string; int flag; int64 int64_; double double_; mpv_node_list* list; mpv_byte_array* ba; } u; int format; };
    struct mpv_node_list { int num; mpv_node* values; char** keys; };
    struct mpv_event { int event_id; int error; uint64 reply_userdata; void* data; };
    struct mpv_event_end_file { int reason; int error; };
    constexpr int FMT_STRING = 1, FMT_DOUBLE = 5, FMT_NODE_ARRAY = 7, FMT_NODE_MAP = 8;
    constexpr int EV_NONE = 0, EV_SHUTDOWN = 1, EV_END_FILE = 7, EV_FILE_LOADED = 8, EV_PLAYBACK_RESTART = 21;

    struct MpvLite
    {
        void* lib = nullptr;
        mpv_handle* (*create)() = nullptr;
        int (*initialize) (mpv_handle*) = nullptr;
        int (*set_option_string) (mpv_handle*, const char*, const char*) = nullptr;
        int (*command) (mpv_handle*, const char**) = nullptr;
        int (*command_node) (mpv_handle*, mpv_node*, mpv_node*) = nullptr;
        void (*free_node_contents) (mpv_node*) = nullptr;
        int (*get_property) (mpv_handle*, const char*, int, void*) = nullptr;
        mpv_event* (*wait_event) (mpv_handle*, double) = nullptr;
        void (*terminate_destroy) (mpv_handle*) = nullptr;

        void* sym (const char* n)
        {
           #if JUCE_WINDOWS
            return (void*) GetProcAddress ((HMODULE) lib, n);
           #else
            return dlsym (lib, n);
           #endif
        }
        bool load()
        {
           #if JUCE_WINDOWS
            const auto dir = File::getSpecialLocation (File::currentExecutableFile).getParentDirectory();
            for (auto name : { "libmpv-2.dll", "mpv-2.dll", "mpv-1.dll" })
            {
                lib = (void*) LoadLibraryW (dir.getChildFile (name).getFullPathName().toWideCharPointer());
                if (lib != nullptr) break;
            }
           #else
            lib = dlopen ("libmpv.so.2", RTLD_NOW);
           #endif
            if (lib == nullptr) return false;
            auto get = [this] (auto& fn, const char* n) { fn = reinterpret_cast<std::remove_reference_t<decltype (fn)>> (sym (n)); return fn != nullptr; };
            return get (create, "mpv_create") && get (initialize, "mpv_initialize")
                && get (set_option_string, "mpv_set_option_string") && get (command, "mpv_command")
                && get (command_node, "mpv_command_node") && get (free_node_contents, "mpv_free_node_contents")
                && get (get_property, "mpv_get_property") && get (wait_event, "mpv_wait_event")
                && get (terminate_destroy, "mpv_terminate_destroy");
        }
    };

    MpvLite* mpv()
    {
        static MpvLite a;
        static bool ok = false;
        static std::mutex m;
        std::lock_guard<std::mutex> g (m);
        if (! ok) ok = a.load();
        return ok ? &a : nullptr;
    }

    // headless prehrávač: bez okna a zvuku, pozastavený
    mpv_handle* createHeadless (MpvLite* a, std::initializer_list<std::pair<const char*, const char*>> extra)
    {
        auto* h = a->create();
        if (h == nullptr) return nullptr;
        for (auto [k, v] : { std::pair<const char*, const char*> { "config", "no" }, { "terminal", "no" }, { "load-scripts", "no" },
                             { "ytdl", "no" }, { "osc", "no" }, { "input-default-bindings", "no" }, { "hwdec", "no" } })
            a->set_option_string (h, k, v);
        for (auto [k, v] : extra) a->set_option_string (h, k, v);
        if (a->initialize (h) < 0) { a->terminate_destroy (h); return nullptr; }
        return h;
    }

    mpv_handle* createHeadless (MpvLite* a, const std::vector<std::pair<std::string, std::string>>& extra)
    {
        auto* h = a->create();
        if (h == nullptr) return nullptr;
        for (auto [k, v] : { std::pair<const char*, const char*> { "config", "no" }, { "terminal", "no" }, { "load-scripts", "no" },
                             { "ytdl", "no" }, { "osc", "no" }, { "input-default-bindings", "no" }, { "hwdec", "no" } })
            a->set_option_string (h, k, v);
        for (auto& [k, v] : extra) a->set_option_string (h, k.c_str(), v.c_str());
        if (a->initialize (h) < 0) { a->terminate_destroy (h); return nullptr; }
        return h;
    }

    void cmd (MpvLite* a, mpv_handle* h, std::initializer_list<const char*> args)
    {
        std::vector<const char*> v (args);
        v.push_back (nullptr);
        a->command (h, v.data());
    }

    // čaká na udalosť; false pri časovom limite, konci súboru alebo vypnutí
    bool waitFor (MpvLite* a, mpv_handle* h, int id, double timeoutSec, const std::function<bool()>& abort)
    {
        const auto end = Time::getMillisecondCounterHiRes() + timeoutSec * 1000.0;
        while (Time::getMillisecondCounterHiRes() < end)
        {
            if (abort && abort()) return false;
            auto* e = a->wait_event (h, 0.05);
            if (e == nullptr || e->event_id == EV_NONE) continue;
            if (e->event_id == id) return true;
            if (e->event_id == EV_SHUTDOWN || (e->event_id == EV_END_FILE && id != EV_END_FILE)) return false;
        }
        return false;
    }

    // aktuálna snímka ako obrázok (screenshot-raw, formát bgr0)
    Image grab (MpvLite* a, mpv_handle* h)
    {
        mpv_node args[2];
        args[0].format = FMT_STRING; args[0].u.string = const_cast<char*> ("screenshot-raw");
        args[1].format = FMT_STRING; args[1].u.string = const_cast<char*> ("video");
        mpv_node_list l { 2, args, nullptr };
        mpv_node arr; arr.format = FMT_NODE_ARRAY; arr.u.list = &l;
        mpv_node res; std::memset (&res, 0, sizeof (res));
        if (a->command_node (h, &arr, &res) < 0) return {};
        Image img;
        if (res.format == FMT_NODE_MAP && res.u.list != nullptr)
        {
            int64 w = 0, ht = 0, stride = 0; const uint8* data = nullptr; size_t size = 0; String fmt;
            for (int i = 0; i < res.u.list->num; ++i)
            {
                const String k (res.u.list->keys[i]);
                auto& v = res.u.list->values[i];
                if (k == "w") w = v.u.int64_;
                else if (k == "h") ht = v.u.int64_;
                else if (k == "stride") stride = v.u.int64_;
                else if (k == "format" && v.format == FMT_STRING) fmt = v.u.string;
                else if (k == "data" && v.u.ba != nullptr) { data = (const uint8*) v.u.ba->data; size = v.u.ba->size; }
            }
            if (w > 0 && ht > 0 && data != nullptr && stride >= w * 4 && size >= (size_t) (stride * ht) && fmt == "bgr0")
            {
                img = Image (Image::RGB, (int) w, (int) ht, false);
                Image::BitmapData bd (img, Image::BitmapData::writeOnly);
                for (int y = 0; y < (int) ht; ++y)
                {
                    const uint8* src = data + y * stride;
                    for (int x = 0; x < (int) w; ++x)
                        bd.setPixelColour (x, y, Colour (src[x * 4 + 2], src[x * 4 + 1], src[x * 4 + 0]));
                }
            }
        }
        a->free_node_contents (&res);
        return img;
    }
}

// ---------------------------------------------------------------- náhľady

bool VideoThumbs::available() { return mpv() != nullptr; }

VideoThumbs::VideoThumbs() : Thread ("video nahlady") {}

VideoThumbs::~VideoThumbs()
{
    *alive = false;
    ++generation;
    signalThreadShouldExit();
    wake.signal();
    stopThread (5000);
}

void VideoThumbs::setFile (const File& f)
{
    {
        std::lock_guard<std::mutex> g (lock);
        if (f == file) return;
        file = f;
        pending.clear();
        cache.clear();
        order.clear();
    }
    ++generation;
    if (f != File() && ! isThreadRunning() && available()) startThread (Thread::Priority::low);
    wake.signal();
}

void VideoThumbs::request (const std::vector<double>& times, bool exact)
{
    {
        std::lock_guard<std::mutex> g (lock);
        pending = times;
        pendingExact = exact;
    }
    ++generation;
    if (! isThreadRunning() && available() && file != File()) startThread (Thread::Priority::low);
    wake.signal();
}

Image VideoThumbs::get (double t, double tol) const
{
    std::lock_guard<std::mutex> g (lock);
    if (cache.empty()) return {};
    const int64 k = (int64) std::llround (t * 1000.0), tolMs = (int64) (tol * 1000.0);
    auto it = cache.lower_bound (k);
    int64 bestD = std::numeric_limits<int64>::max(); Image best;
    if (it != cache.end()) { bestD = it->first - k; best = it->second; }
    if (it != cache.begin()) { auto p = std::prev (it); if (k - p->first < bestD) { bestD = k - p->first; best = p->second; } }
    return bestD <= tolMs ? best : Image();
}

void VideoThumbs::run()
{
    auto* a = mpv();
    if (a == nullptr) return;
    mpv_handle* h = nullptr;
    auto closeH = [&] { if (h != nullptr) { a->terminate_destroy (h); h = nullptr; } loadedFile = File(); };

    while (! threadShouldExit())
    {
        wake.wait (500);
        if (threadShouldExit()) break;
        File f; std::vector<double> todo; bool exact = false;
        {
            std::lock_guard<std::mutex> g (lock);
            f = file; todo = pending; exact = pendingExact;
        }
        if (f == File()) { closeH(); continue; }
        const int gen = generation.load();
        auto aborted = [&] { return threadShouldExit() || generation.load() != gen; };

        if (f != loadedFile)
        {
            closeH();
            h = createHeadless (a, { { "vo", "null" }, { "ao", "null" }, { "aid", "no" }, { "sid", "no" }, { "pause", "yes" },
                                     { "idle", "yes" }, { "keep-open", "yes" },
                                     { "vf", "scale=w=192:h=108:force_original_aspect_ratio=decrease" } });
            if (h == nullptr) continue;
            cmd (a, h, { "loadfile", f.getFullPathName().toRawUTF8() });
            if (! waitFor (a, h, EV_FILE_LOADED, 15.0, [&] { return threadShouldExit() || [&] { std::lock_guard<std::mutex> g (lock); return file != f; }(); }))
            { closeH(); continue; }
            loadedFile = f;
        }

        for (double t : todo)
        {
            if (aborted()) break;
            {
                // už máme
                const auto im = get (t, 0.02);
                if (im.isValid()) continue;
            }
            cmd (a, h, { "seek", String (t, 3).toRawUTF8(), exact ? "absolute+exact" : "absolute+keyframes" });
            if (! waitFor (a, h, EV_PLAYBACK_RESTART, 6.0, aborted)) { if (aborted()) break; continue; }
            auto img = grab (a, h);
            if (! img.isValid()) continue;
            {
                std::lock_guard<std::mutex> g (lock);
                if (file != f) break;
                const int64 k = (int64) std::llround (t * 1000.0);
                if (cache.find (k) == cache.end()) order.push_back (k);
                cache[k] = img;
                while (order.size() > 600) { cache.erase (order.front()); order.erase (order.begin()); }
                if (notifyQueued) continue;
                notifyQueued = true;
            }
            MessageManager::callAsync ([this, al = alive] { if (! al->load()) return; { std::lock_guard<std::mutex> g (lock); notifyQueued = false; } if (onNew) onNew(); });
        }
        // hotovo: tieto časy už znova neskúšať (kým nepríde nová požiadavka)
        if (! aborted()) { std::lock_guard<std::mutex> g (lock); if (generation.load() == gen) pending.clear(); }
    }
    closeH();
}

// ---------------------------------------------------------------- filmový pás

static String stripTime (double s, bool fine)
{
    const int t = (int) s;
    String r = t >= 3600 ? String (t / 3600) + ":" + String ((t / 60) % 60).paddedLeft ('0', 2) + ":" + String (t % 60).paddedLeft ('0', 2)
                         : String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2);
    if (fine) r << "." << String ((int) ((s - t) * 10.0));
    return r;
}

VideoStrip::VideoStrip()
{
    thumbs.onNew = [this] { repaint(); };
    setRepaintsOnMouseActivity (false);
}

void VideoStrip::setFile (const File& f)
{
    if (f == file) return;
    file = f;
    duration = position = 0;
    viewA = viewB = 0;
    selA = selB = -1;
    thumbs.setFile (f);
    repaint();
}

void VideoStrip::setDuration (double d)
{
    if (d <= 0 || std::abs (d - duration) < 1e-6) return;
    const bool first = duration <= 0;
    duration = d;
    if (first || viewB > duration) { viewA = 0; viewB = duration; }
    requestThumbs();
    repaint();
}

void VideoStrip::setPosition (double t)
{
    if (std::abs (t - position) < 1e-4) return;
    position = t;
    // pri priblížení sa pás posúva za prehrávaním
    const double span = viewB - viewA;
    if (duration > 0 && span < duration - 1e-6 && dragEdge < 0 && ! selecting && ! draggingView && ! isMouseButtonDown()
        && (t > viewB || t < viewA - span * 0.02))
        setView (t - span * 0.05, t + span * 0.95);
    repaint();
}

void VideoStrip::clearSelection()
{
    selA = selB = -1;
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

int VideoStrip::tileCount() const
{
    const auto ta = tilesArea();
    const int tileW = jmax (40, ta.getHeight() * 16 / 9);
    return jmax (1, (ta.getWidth() + tileW - 1) / tileW);
}

void VideoStrip::requestThumbs()
{
    if (duration <= 0 || viewB <= viewA) return;
    const int n = tileCount();
    const double step = (viewB - viewA) / n;
    std::vector<double> ts;
    for (int i = 0; i < n; ++i) ts.push_back (jmin (duration - 0.05, viewA + (i + 0.5) * step));
    // najprv stred, potom do strán (pri dlhom čakaní je hneď vidieť to podstatné)
    std::stable_sort (ts.begin(), ts.end(), [c = (viewA + viewB) / 2] (double x, double y) { return std::abs (x - c) < std::abs (y - c); });
    thumbs.request (ts, step < 4.0);
    startTimer (400);
}

int VideoStrip::readyThumbs() const
{
    if (duration <= 0) return 0;
    const int n = tileCount();
    const double step = (viewB - viewA) / n;
    int k = 0;
    for (int i = 0; i < n; ++i) if (thumbs.get (jmin (duration - 0.05, viewA + (i + 0.5) * step), step * 0.5 + 0.02).isValid()) ++k;
    return k;
}

void VideoStrip::timerCallback()
{
    stopTimer();
    repaint();
}

double VideoStrip::xToTime (int x) const
{
    const auto ta = tilesArea();
    return viewA + jlimit (0.0, 1.0, (x - ta.getX()) / (double) jmax (1, ta.getWidth())) * (viewB - viewA);
}

int VideoStrip::timeToX (double t) const
{
    const auto ta = tilesArea();
    return ta.getX() + roundToInt ((t - viewA) / jmax (1e-9, viewB - viewA) * ta.getWidth());
}

void VideoStrip::setView (double a, double b)
{
    if (duration <= 0) return;
    double span = jlimit (jmin (duration, 0.4), duration, b - a);
    a = jlimit (0.0, duration - span, a);
    if (std::abs (a - viewA) < 1e-9 && std::abs (a + span - viewB) < 1e-9) return;
    viewA = a; viewB = a + span;
    requestThumbs();
    repaint();
}

int VideoStrip::edgeAt (Point<int> p) const
{
    if (! hasSelection() || ! tilesArea().expanded (0, 4).contains (p)) return -1;
    if (std::abs (p.x - timeToX (selA)) <= 8) return 0;
    if (std::abs (p.x - timeToX (selB)) <= 8) return 1;
    return -1;
}

void VideoStrip::paint (Graphics& g)
{
    g.fillAll (col::panel);
    const auto ta = tilesArea();
    g.setColour (col::bg);
    g.fillRect (ta);
    if (duration <= 0) return;

    const int n = tileCount();
    const double step = (viewB - viewA) / n;
    const bool fine = viewB - viewA < 30.0;
    for (int i = 0; i < n; ++i)
    {
        const int x0 = ta.getX() + ta.getWidth() * i / n, x1 = ta.getX() + ta.getWidth() * (i + 1) / n;
        const auto tile = juce::Rectangle<int> (x0, ta.getY(), x1 - x0, ta.getHeight());
        const double t = jmin (duration - 0.05, viewA + (i + 0.5) * step);
        const auto im = thumbs.get (t, step * 0.5 + 0.02);
        if (im.isValid())
        {
            Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (tile.reduced (1, 0));
            g.drawImage (im, tile.toFloat(), RectanglePlacement::fillDestination);
        }
        else
        {
            g.setColour (col::panelHi);
            g.fillRect (tile.reduced (1, 0));
        }
        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillRect (tile.getX() + 1, tile.getY(), jmin (tile.getWidth() - 2, fine ? 46 : 38), 13);
        g.setColour (Colours::white.withAlpha (0.85f));
        g.setFont (FontOptions (10.0f));
        g.drawText (stripTime (viewA + i * step, fine), tile.getX() + 3, tile.getY(), 60, 13, Justification::centredLeft);
    }

    // výber
    if (hasSelection())
    {
        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (ta.expanded (8, 4));
        const int x0 = timeToX (selA), x1 = timeToX (selB);
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillRect (juce::Rectangle<int> (ta.getX(), ta.getY(), jmax (0, x0 - ta.getX()), ta.getHeight()));
        g.fillRect (juce::Rectangle<int> (x1, ta.getY(), jmax (0, ta.getRight() - x1), ta.getHeight()));
        g.setColour (col::accentHi);
        g.drawRect (juce::Rectangle<int> (x0, ta.getY(), jmax (2, x1 - x0), ta.getHeight()), 2);
        for (int x : { x0 + 1, x1 - 1 })
        {
            const auto t = juce::Rectangle<float> (16.0f, 18.0f).withCentre ({ (float) x, (float) ta.getCentreY() });
            g.setColour (col::accentHi); g.fillRoundedRectangle (t, 3.0f);
            Path p;
            p.addTriangle (t.getCentreX() - 6.0f, t.getCentreY(), t.getCentreX() - 2.0f, t.getCentreY() - 4.0f, t.getCentreX() - 2.0f, t.getCentreY() + 4.0f);
            p.addTriangle (t.getCentreX() + 6.0f, t.getCentreY(), t.getCentreX() + 2.0f, t.getCentreY() - 4.0f, t.getCentreX() + 2.0f, t.getCentreY() + 4.0f);
            g.setColour (col::bg); g.fillPath (p);
        }
    }

    // prehrávanie
    if (position >= viewA && position <= viewB)
    {
        g.setColour (col::gold);
        g.fillRect (timeToX (position) - 1, ta.getY() - 2, 2, ta.getHeight() + 4);
    }

    // spodný riadok: prehľad (pri priblížení) alebo nápoveda
    const auto ov = overviewArea();
    if (viewB - viewA < duration - 1e-6)
    {
        g.setColour (col::bg);
        g.fillRoundedRectangle (ov.toFloat(), 2.0f);
        const auto vr = juce::Rectangle<float> (ov.getX() + (float) (ov.getWidth() * viewA / duration), (float) ov.getY(),
                                          jmax (3.0f, (float) (ov.getWidth() * (viewB - viewA) / duration)), (float) ov.getHeight());
        g.setColour (col::accentHi.withAlpha (0.5f));
        g.fillRect (vr);
        if (hasSelection())
        {
            g.setColour (col::gold.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (ov.getX() + (float) (ov.getWidth() * selA / duration), ov.getY() + ov.getHeight() - 3.0f,
                                          jmax (2.0f, (float) (ov.getWidth() * (selB - selA) / duration)), 3.0f));
        }
        g.setColour (col::gold);
        g.fillRect ((float) ov.getX() + (float) (ov.getWidth() * position / duration), (float) ov.getY(), 1.0f, (float) ov.getHeight());
    }
    else
    {
        g.setColour (col::textDim);
        g.setFont (FontOptions (11.0f));
        g.drawText (tr ("Ťahaním vyznač výrez · koliesko = priblížiť · klik = skok"), ov, Justification::centredRight);
    }
    if (hasSelection())
    {
        g.setColour (col::text);
        g.setFont (FontOptions (11.0f));
        g.drawText (stripTime (selA, true) + " – " + stripTime (selB, true) + "  (" + String (selB - selA, 1) + " s)", ov, Justification::centredLeft);
    }
}

void VideoStrip::mouseMove (const MouseEvent& e)
{
    const bool ov = duration > 0 && viewB - viewA < duration - 1e-6 && overviewArea().contains (e.getPosition());
    setMouseCursor (edgeAt (e.getPosition()) >= 0 ? MouseCursor::LeftRightResizeCursor : ov ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
}

void VideoStrip::mouseDown (const MouseEvent& e)
{
    if (duration <= 0) return;
    wasSeekClick = false;
    draggingView = viewB - viewA < duration - 1e-6 && overviewArea().contains (e.getPosition());
    if (draggingView)
    {
        const auto ov = overviewArea();
        const double c = jlimit (0.0, 1.0, (e.x - ov.getX()) / (double) jmax (1, ov.getWidth())) * duration, span = viewB - viewA;
        setView (c - span / 2, c + span / 2);
        return;
    }
    dragEdge = edgeAt (e.getPosition());
    if (dragEdge >= 0) { dragT0 = dragEdge == 0 ? selA : selB; return; }
    selecting = false;
    wasSeekClick = true;
}

void VideoStrip::mouseDrag (const MouseEvent& e)
{
    if (duration <= 0) return;
    if (draggingView)
    {
        const auto ov = overviewArea();
        const double c = jlimit (0.0, 1.0, (e.x - ov.getX()) / (double) jmax (1, ov.getWidth())) * duration, span = viewB - viewA;
        setView (c - span / 2, c + span / 2);
        return;
    }
    if (dragEdge >= 0)
    {
        const double perPx = (viewB - viewA) / (double) jmax (1, tilesArea().getWidth());
        const double t = jlimit (0.0, duration, e.mods.isShiftDown() ? dragT0 + e.getDistanceFromDragStartX() * perPx / 10.0 : xToTime (e.x));
        const double gap = jmax (0.04, perPx);
        if (dragEdge == 0) selA = jmin (t, selB - gap); else selB = jmax (t, selA + gap);
        if (onSeek) onSeek (dragEdge == 0 ? selA : selB);   // video ukáže snímku na okraji
        repaint();
        return;
    }
    if (! selecting && std::abs (e.getDistanceFromDragStartX()) < 4) return;
    selecting = true;
    wasSeekClick = false;
    selA = jmin (xToTime (e.getMouseDownX()), xToTime (e.x));
    selB = jmax (xToTime (e.getMouseDownX()), xToTime (e.x));
    repaint();
}

void VideoStrip::mouseUp (const MouseEvent& e)
{
    if (draggingView) { draggingView = false; return; }
    if (dragEdge >= 0) { dragEdge = -1; if (onSelectionChanged) onSelectionChanged(); return; }
    if (selecting)
    {
        selecting = false;
        if (timeToX (selB) - timeToX (selA) < 3) { clearSelection(); return; }
        if (onSeek) onSeek (selA);
        if (onSelectionChanged) onSelectionChanged();
        return;
    }
    if (wasSeekClick && tilesArea().contains (e.getPosition()) && onSeek) onSeek (xToTime (e.x));
}

void VideoStrip::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (duration <= 0) return;
    const double span = viewB - viewA;
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const double d = (std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : -w.deltaY) * span * 0.5;
        setView (viewA + d, viewB + d);
        return;
    }
    const double t = xToTime (e.x), f = std::exp (-w.deltaY * 1.2), ns = span * f;
    const double rel = (t - viewA) / jmax (1e-9, span);
    setView (t - rel * ns, t - rel * ns + ns);
}

void VideoStrip::mouseDoubleClick (const MouseEvent& e)
{
    if (duration <= 0) return;
    if (viewB - viewA < duration - 1e-6 && overviewArea().contains (e.getPosition())) { setView (0, duration); return; }
    if (const int ed = edgeAt (e.getPosition()); ed >= 0)
    {
        const double t = ed == 0 ? selA : selB;
        setView (t - 2.0, t + 2.0);
    }
}

double probeVideoDuration (const File& f)
{
    auto* a = mpv();
    if (a == nullptr) return 0;
    auto* h = createHeadless (a, { { "vo", "null" }, { "ao", "null" }, { "pause", "yes" }, { "idle", "yes" } });
    if (h == nullptr) return 0;
    cmd (a, h, { "loadfile", f.getFullPathName().toRawUTF8() });
    double d = 0;
    if (waitFor (a, h, EV_FILE_LOADED, 15.0, {})) a->get_property (h, "duration", FMT_DOUBLE, &d);
    a->terminate_destroy (h);
    return d;
}

// ---------------------------------------------------------------- uloženie výrezu

namespace videocut
{
    static std::atomic<bool> running { false };
    bool busy() { return running.load(); }

    File run (const File& in, double a, double b, std::function<void (float)> progress, String& message)
    {
        auto* api = mpv();
        if (api == nullptr) { message = tr ("Na ukladanie videa treba knižnicu libmpv (Nastavenia → stiahnuť libmpv)."); return {}; }
        const auto out = in.getParentDirectory().getNonexistentChildFile (in.getFileNameWithoutExtension() + "_vyrez", ".mp4", false);
        // H.264 (x264), ak by chýbal, kóder Windows (Media Foundation), nakoniec MPEG-4
        const char* codecs[][2] = { { "libx264", "crf=18,preset=veryfast" }, { "h264_mf", "b=12000000" }, { "mpeg4", "q=2" } };
        for (auto& c : codecs)
        {
            out.deleteFile();
            auto* h = createHeadless (api, { { "o", out.getFullPathName().toRawUTF8() },
                                             { "start", String (a, 3).toRawUTF8() }, { "end", String (b, 3).toRawUTF8() },
                                             { "ovc", c[0] }, { "ovcopts", c[1] }, { "oac", "aac" }, { "oacopts", "b=192000" },
                                             { "sid", "no" }, { "idle", "no" } });
            if (h == nullptr) continue;
            cmd (api, h, { "loadfile", in.getFullPathName().toRawUTF8() });
            int reason = -1, err = 0;
            double lastP = -1;
            while (true)
            {
                auto* e = api->wait_event (h, 0.2);
                if (e != nullptr && e->event_id == EV_END_FILE && e->data != nullptr)
                {
                    auto* ef = (mpv_event_end_file*) e->data;
                    reason = ef->reason; err = ef->error;
                    break;
                }
                if (e != nullptr && e->event_id == EV_SHUTDOWN) break;
                double pos = 0;
                if (api->get_property (h, "time-pos", FMT_DOUBLE, &pos) >= 0 && b > a)
                {
                    const double p = jlimit (0.0, 1.0, (pos - a) / (b - a));
                    if (p - lastP > 0.01 && progress) { lastP = p; progress ((float) p); }
                }
            }
            api->terminate_destroy (h);
            if (reason == 0 && err >= 0 && out.getSize() > 1000)
            {
                message = tr ("Výrez videa uložený: ") + out.getFileName() + " (" + String (b - a, 1) + " s)";
                return out;
            }
        }
        out.deleteFile();
        message = tr ("Výrez videa sa nepodarilo uložiť.");
        return {};
    }

    void start (const File& in, double a, double b, std::function<void (float)> progress, std::function<void (const File&, const String&)> done)
    {
        if (running.exchange (true)) return;
        std::thread ([in, a, b, progress, done]
        {
            String msg;
            const auto out = run (in, a, b, [progress] (float p) { MessageManager::callAsync ([progress, p] { if (progress) progress (p); }); }, msg);
            running = false;
            MessageManager::callAsync ([done, out, msg] { if (done) done (out, msg); });
        }).detach();
    }
}

// ---------------------------------------------------------------- prevod videa do iného formátu

namespace videoconv
{
    String extensionFor (int format)
    {
        switch (format) { case 1: return ".mkv"; case 2: return ".webm"; case 3: return ".mp3"; default: return ".mp4"; }
    }

    File outputFor (const File& in, const Options& o)
    {
        const String suffix = o.format == 3 ? String ("_zvuk") : (o.height > 0 ? "_" + String (o.height) + "p" : String ("_prevod"));
        return in.getParentDirectory().getNonexistentChildFile (in.getFileNameWithoutExtension() + suffix, extensionFor (o.format), false);
    }

    bool run (const String& input, const File& out, const Options& o, std::function<void (float)> progress,
              const std::atomic<bool>* cancel, String& message)
    {
        auto* api = mpv();
        if (api == nullptr) { message = tr ("Na prevod videa treba knižnicu libmpv (Nastavenia → stiahnuť libmpv)."); return false; }
        const int q = jlimit (0, 2, o.quality);
        using Opts = std::vector<std::pair<std::string, std::string>>;
        std::vector<Opts> tries;   // pokusy od najlepšieho kódera po náhradný

        // zmenšenie (nikdy nezväčší) + 8-bit 4:2:0, aby video išlo prehrať všade
        String vf = "lavfi=[";
        if (o.height > 0) vf << "scale=-2:'min(" << o.height << ",ih)',";
        vf << "format=yuv420p]";

        if (o.format == 3)
        {
            const char* br[] = { "320000", "192000", "128000" };
            tries.push_back ({ { "vid", "no" }, { "oac", "libmp3lame" }, { "oacopts", std::string ("b=") + br[q] } });
        }
        else if (o.format == 2)
        {
            const int crf[] = { 31, 36, 42 };
            const char* ab[] = { "128000", "96000", "64000" };
            const auto vp9 = "crf=" + std::to_string (crf[q]) + ",b=0,deadline=realtime,cpu-used=8,row-mt=1";
            const auto vp8 = "crf=" + std::to_string (crf[q] / 3) + ",b=" + std::to_string ((3 - q) * 1500000);
            for (auto& c : { std::pair<std::string, std::string> { "libvpx-vp9", vp9 }, { "libvpx", vp8 } })
                tries.push_back ({ { "vf", vf.toStdString() }, { "ovc", c.first }, { "ovcopts", c.second },
                                   { "oac", "libopus" }, { "oacopts", std::string ("b=") + ab[q] } });
        }
        else
        {
            const int crf[] = { 20, 24, 28 };
            const int base = o.height == 480 ? 2500000 : (o.height == 720 ? 5000000 : 10000000);
            const int mq[] = { 2, 4, 7 };
            const char* ab[] = { "192000", "160000", "128000" };
            for (auto& c : { std::pair<std::string, std::string> { "libx264", "crf=" + std::to_string (crf[q]) + ",preset=veryfast" },
                             { "h264_mf", "b=" + std::to_string (base >> q) },
                             { "mpeg4", "q=" + std::to_string (mq[q]) } })
                tries.push_back ({ { "vf", vf.toStdString() }, { "ovc", c.first }, { "ovcopts", c.second },
                                   { "oac", "aac" }, { "oacopts", std::string ("b=") + ab[q] } });
        }

        for (auto& t : tries)
        {
            out.deleteFile();
            Opts opts = t;
            opts.push_back ({ "o", out.getFullPathName().toStdString() });
            opts.push_back ({ "sid", "no" });
            opts.push_back ({ "idle", "no" });
            auto* h = createHeadless (api, opts);
            if (h == nullptr) continue;
            cmd (api, h, { "loadfile", input.toRawUTF8() });
            int reason = -1, err = 0;
            double dur = 0, lastP = -1, lastPos = -1, lastMove = Time::getMillisecondCounterHiRes();
            bool cancelled = false, stalled = false;
            while (true)
            {
                if (cancel != nullptr && cancel->load()) { cancelled = true; break; }
                // poškodené video: kóder sa môže zaseknúť – po 30 s bez pohybu skončiť
                if (Time::getMillisecondCounterHiRes() - lastMove > 30000.0) { stalled = true; break; }
                auto* e = api->wait_event (h, 0.2);
                if (e != nullptr && e->event_id == EV_END_FILE && e->data != nullptr)
                {
                    auto* ef = (mpv_event_end_file*) e->data;
                    reason = ef->reason; err = ef->error;
                    break;
                }
                if (e != nullptr && e->event_id == EV_SHUTDOWN) break;
                if (dur <= 0) api->get_property (h, "duration", FMT_DOUBLE, &dur);
                double pos = 0;
                const bool hasPos = api->get_property (h, "time-pos", FMT_DOUBLE, &pos) >= 0;
                if (hasPos && pos != lastPos) { lastPos = pos; lastMove = Time::getMillisecondCounterHiRes(); }
                if (dur > 0 && hasPos)
                {
                    const double p = jlimit (0.0, 1.0, pos / dur);
                    if (p - lastP > 0.005 && progress) { lastP = p; progress ((float) p); }
                }
            }
            api->terminate_destroy (h);
            if (cancelled) { out.deleteFile(); message = tr ("Prevod zrušený."); return false; }
            if (stalled) break;
            if (reason == 0 && err >= 0 && out.getSize() > 1000)
            {
                message = tr ("Uložené: ") + out.getFileName();
                return true;
            }
        }
        out.deleteFile();
        message = tr ("Video sa nepodarilo previesť: ") + File (input).getFileName();
        return false;
    }
}
}
