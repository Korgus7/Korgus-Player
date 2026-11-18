#include "VideoView.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace kp
{
static String timeText (double s)
{
    const int t = (int) s;
    if (t >= 3600) return String (t / 3600) + ":" + String ((t / 60) % 60).paddedLeft ('0', 2) + ":" + String (t % 60).paddedLeft ('0', 2);
    return String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2);
}

#if JUCE_WINDOWS
namespace
{
    // Minimum z libmpv client API (stabilné C rozhranie), načítané dynamicky
    struct mpv_handle;
    struct mpv_event { int event_id; int error; uint64 reply_userdata; void* data; };
    constexpr int MPV_FORMAT_FLAG = 3, MPV_FORMAT_DOUBLE = 5;

    struct MpvApi
    {
        HMODULE lib = nullptr;
        mpv_handle* (*create)() = nullptr;
        int (*initialize) (mpv_handle*) = nullptr;
        int (*set_option_string) (mpv_handle*, const char*, const char*) = nullptr;
        int (*command) (mpv_handle*, const char**) = nullptr;
        int (*get_property) (mpv_handle*, const char*, int, void*) = nullptr;
        mpv_event* (*wait_event) (mpv_handle*, double) = nullptr;
        void (*terminate_destroy) (mpv_handle*) = nullptr;

        bool load()
        {
            const auto dir = File::getSpecialLocation (File::currentExecutableFile).getParentDirectory();
            for (auto name : { "libmpv-2.dll", "mpv-2.dll", "mpv-1.dll" })
            {
                lib = LoadLibraryW (dir.getChildFile (name).getFullPathName().toWideCharPointer());
                if (lib != nullptr) break;
            }
            if (lib == nullptr) return false;
            auto get = [this] (auto& fn, const char* n) { fn = reinterpret_cast<std::remove_reference_t<decltype (fn)>> (GetProcAddress (lib, n)); return fn != nullptr; };
            return get (create, "mpv_create") && get (initialize, "mpv_initialize")
                && get (set_option_string, "mpv_set_option_string") && get (command, "mpv_command")
                && get (get_property, "mpv_get_property") && get (wait_event, "mpv_wait_event")
                && get (terminate_destroy, "mpv_terminate_destroy");
        }
    };

    MpvApi* api()
    {
        static MpvApi a;
        static bool ok = false;
        if (! ok) ok = a.load();   // skúsiť znova – knižnica sa mohla medzitým stiahnuť
        return ok ? &a : nullptr;
    }
}

struct VideoView::Impl
{
    HWNDComponent host;
    HWND hwnd = nullptr;
    mpv_handle* mpv = nullptr;

    ~Impl()
    {
        if (mpv != nullptr) api()->terminate_destroy (mpv);
        host.setHWND (nullptr);
        if (hwnd != nullptr) DestroyWindow (hwnd);
    }

    void cmd (std::initializer_list<const char*> args)
    {
        if (mpv == nullptr) return;
        std::vector<const char*> v (args);
        v.push_back (nullptr);
        api()->command (mpv, v.data());
    }
};

bool VideoView::isAvailable() { return api() != nullptr; }

VideoView::VideoView() : impl (std::make_unique<Impl>())
{
    setOpaque (true);
    for (auto* b : { &snapB, &fsB })
    {
        b->background = true;
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    snapB.onClick = [this] { snapshot(); };
    fsB.onClick = [this] { if (onFullscreen) onFullscreen(); };
}

void VideoView::snapshot()
{
    if (file == File()) return;
    const int t = (int) position;
    const auto base = file.getFileNameWithoutExtension() + "_snimka_" + String (t / 60).paddedLeft ('0', 2) + "-" + String (t % 60).paddedLeft ('0', 2);
    const auto out = file.getParentDirectory().getNonexistentChildFile (base, ".png", false);
    impl->cmd ({ "screenshot-to-file", out.getFullPathName().toRawUTF8(), "video" });
    Component::SafePointer<VideoView> sp (this);
    Timer::callAfterDelay (700, [sp, out] { if (sp != nullptr && sp->onSnapshot) sp->onSnapshot (out); });
}

void VideoView::seekRelative (double s)
{
    impl->cmd ({ "seek", String (s, 2).toRawUTF8(), "relative" });
}

void VideoView::setStart (double s) { startAt = s; }

VideoView::~VideoView()
{
    stopTimer();
    impl.reset();
}

bool VideoView::ensurePlayer()
{
    if (impl->mpv != nullptr) return true;
    auto* a = api();
    auto* peer = getPeer();
    if (a == nullptr || peer == nullptr) return false;

    if (impl->hwnd == nullptr)
    {
        impl->hwnd = CreateWindowExW (0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                      0, 0, 16, 16, (HWND) peer->getNativeHandle(), nullptr, GetModuleHandleW (nullptr), nullptr);
        if (impl->hwnd == nullptr) return false;
        impl->host.setHWND (impl->hwnd);
        addAndMakeVisible (impl->host);
        resized();
    }

    impl->mpv = a->create();
    if (impl->mpv == nullptr) return false;
    const auto wid = String ((int64) (pointer_sized_int) impl->hwnd);
    auto opt = [this, a] (const char* k, const char* v) { a->set_option_string (impl->mpv, k, v); };
    opt ("wid", wid.toRawUTF8());
    opt ("config", "no");
    opt ("load-scripts", "no");
    opt ("ytdl", "no");
    opt ("osc", "no");
    opt ("terminal", "no");
    opt ("idle", "yes");
    opt ("keep-open", "yes");
    opt ("hwdec", "auto-safe");
    opt ("input-default-bindings", "no");
    opt ("input-vo-keyboard", "no");
    opt ("input-cursor", "no");
    opt ("cursor-autohide", "no");
    opt ("background-color", "#000000");
    if (a->initialize (impl->mpv) < 0)
    {
        a->terminate_destroy (impl->mpv);
        impl->mpv = nullptr;
        return false;
    }
    return true;
}

bool VideoView::open (const File& f)
{
    file = f;
    if (! ensurePlayer()) return false;
    position = duration = 0;
    paused = false;
    impl->cmd ({ "set", "start", startAt > 0.5 ? ("+" + String (startAt, 2)).toRawUTF8() : "none" });
    startAt = 0;
    impl->cmd ({ "loadfile", f.getFullPathName().toRawUTF8(), "replace" });
    impl->cmd ({ "set", "pause", "no" });
    startTimerHz (10);
    repaint();
    return true;
}

void VideoView::stop()
{
    stopTimer();
    impl->cmd ({ "stop" });
    position = duration = 0;
}

void VideoView::togglePause()
{
    impl->cmd ({ "cycle", "pause" });
    startTimerHz (10);
}

bool VideoView::isPlaying() const { return impl->mpv != nullptr && ! paused && duration > 0; }

void VideoView::seekFromMouse (int x)
{
    if (duration <= 0) return;
    const double t = jlimit (0.0, 1.0, (double) (x - 10) / (double) jmax (1, getWidth() - 20)) * duration;
    impl->cmd ({ "seek", String (t, 3).toRawUTF8(), "absolute" });
}

void VideoView::timerCallback()
{
    auto* a = api();
    if (a == nullptr || impl->mpv == nullptr) return;
    while (true)
    {
        auto* e = a->wait_event (impl->mpv, 0);
        if (e == nullptr || e->event_id == 0) break;
    }
    double d = 0;
    if (a->get_property (impl->mpv, "time-pos", MPV_FORMAT_DOUBLE, &d) >= 0) position = d;
    if (a->get_property (impl->mpv, "duration", MPV_FORMAT_DOUBLE, &d) >= 0) duration = d;
    int p = 0;
    if (a->get_property (impl->mpv, "pause", MPV_FORMAT_FLAG, &p) >= 0) paused = p != 0;
    repaint (barArea());

    // klik do videa presunie klávesnicu do okna mpv – vrátime ju programu
    const HWND focus = GetFocus();
    if (focus != nullptr && impl->hwnd != nullptr && (focus == impl->hwnd || IsChild (impl->hwnd, focus)))
        if (onFocusBack) onFocusBack();
}

void VideoView::resized()
{
    impl->host.setBounds (getLocalBounds().withTrimmedBottom (barH));
    auto b = barArea().reduced (4, 4).removeFromTop (barH - 14);
    fsB.setBounds (b.removeFromRight (28));
    b.removeFromRight (4);
    snapB.setBounds (b.removeFromRight (28));
}

void VideoView::visibilityChanged()
{
    // natívne okno mpv musí zmiznúť spolu s prehrávačom, inak by zakrylo ukážku
    if (impl->hwnd != nullptr)
        ShowWindow (impl->hwnd, isVisible() ? SW_SHOWNA : SW_HIDE);
    impl->host.setVisible (isVisible());
}

#else

struct VideoView::Impl {};
bool VideoView::isAvailable() { return false; }
VideoView::VideoView() : impl (std::make_unique<Impl>()) {}
void VideoView::snapshot() {}
void VideoView::seekRelative (double) {}
void VideoView::setStart (double s) { startAt = s; }
VideoView::~VideoView() = default;
bool VideoView::ensurePlayer() { return false; }
bool VideoView::open (const File&) { return false; }
void VideoView::stop() {}
void VideoView::togglePause() {}
bool VideoView::isPlaying() const { return false; }
void VideoView::seekFromMouse (int) {}
void VideoView::timerCallback() {}
void VideoView::resized() {}
void VideoView::visibilityChanged() {}

#endif

void VideoView::paint (Graphics& g)
{
    g.fillAll (Colours::black);
    auto bar = barArea();
    g.setColour (col::panel);
    g.fillRect (bar);
    auto track = bar.removeFromBottom (12).reduced (10, 3).toFloat();
    g.setColour (col::line);
    g.fillRoundedRectangle (track, 2.0f);
    if (duration > 0)
    {
        g.setColour (col::gold);
        g.fillRoundedRectangle (track.withWidth (track.getWidth() * (float) jlimit (0.0, 1.0, position / duration)), 2.0f);
        g.setColour (col::text);
        g.setFont (FontOptions (12.0f));
        g.drawText ((paused ? tr ("❚❚ ") : tr ("▶ ")) + timeText (position) + " / " + timeText (duration),
                    bar.withTrimmedLeft (10), Justification::centredLeft, false);
    }
}

void VideoView::mouseDown (const MouseEvent& e)
{
    if (barArea().contains (e.getPosition())) seekFromMouse (e.x);
}

void VideoView::mouseDrag (const MouseEvent& e)
{
    if (barArea().contains (e.getMouseDownPosition())) seekFromMouse (e.x);
}
}

namespace kp
{
VideoFullscreen::VideoFullscreen (const File& f, double startSeconds)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    video.showFullscreenButton (false);
    video.onFocusBack = [this] { grabKeyboardFocus(); };
    video.onSnapshot = [this] (const File& out) { if (onSnapshot) onSnapshot (out); };
    addAndMakeVisible (video);
    closeB.background = true;
    closeB.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeB);
    file = f;
    start = startSeconds;
}

void VideoFullscreen::start_()
{
    video.setStart (start);
    video.open (file);
    grabKeyboardFocus();
}

void VideoFullscreen::resized()
{
    video.setBounds (getLocalBounds());
    closeB.setBounds (getWidth() - 4 - 28, getHeight() - 34 + 4, 28, 20);   // v lište (nad videom by ho prekrylo okno mpv)
    closeB.toFront (false);
}

void VideoFullscreen::paint (Graphics& g) { g.fillAll (Colours::black); }

bool VideoFullscreen::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey || k.getKeyCode() == KeyPress::F11Key || k == KeyPress::returnKey) { if (onClose) onClose(); return true; }
    if (k == KeyPress::spaceKey) { video.togglePause(); return true; }
    if (k == KeyPress::leftKey) { video.seekRelative (k.getModifiers().isShiftDown() ? -30.0 : -5.0); return true; }
    if (k == KeyPress::rightKey) { video.seekRelative (k.getModifiers().isShiftDown() ? 30.0 : 5.0); return true; }
    return false;
}
}
