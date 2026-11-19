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

// kde sa skončilo pozeranie filmov + hlasitosť videa
namespace resume
{
    static PropertiesFile& store()
    {
        static PropertiesFile pf ([]
        {
            PropertiesFile::Options o;
            o.applicationName = "pozicie-videa";
            o.filenameSuffix = ".xml";
            o.folderName = "Korgus Player";
            o.osxLibrarySubFolder = "Application Support";
            o.millisecondsBeforeSaving = 2000;
            return o;
        }());
        return pf;
    }
    static String key (const File& f) { return "v" + String::toHexString (f.getFullPathName().toLowerCase().hashCode64()); }
    double get (const File& f) { return store().getDoubleValue (key (f), 0.0); }
    void set (const File& f, double pos, double dur)
    {
        // krátke videá a začiatok/koniec si nepamätáme
        if (dur >= 300 && pos >= 30 && pos < dur - 60) store().setValue (key (f), pos);
        else store().removeValue (key (f));
    }
    double volume() { return store().getDoubleValue ("volume", 100.0); }
    void setVolume (double v) { store().setValue ("volume", v); }
}

void VideoView::rememberPosition()
{
    if (file != File() && duration > 0) resume::set (file, position, duration);
}

void VideoView::setupBar()
{
    tracksB.setButtonText (tr ("Titulky / zvuk"));
    tracksB.setTooltip (tr ("Výber titulkov a zvukovej stopy"));
    tracksB.setWantsKeyboardFocus (false);
    tracksB.onClick = [this] { showTracksMenu(); };
    addAndMakeVisible (tracksB);
    volume.setSliderStyle (Slider::LinearHorizontal);
    volume.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    volume.setRange (0, 130, 1);
    volume.setValue (resume::volume(), dontSendNotification);
    volume.setTooltip (tr ("Hlasitosť videa (↑ ↓ na celej obrazovke)"));
    volume.setWantsKeyboardFocus (false);
    volume.setColour (Slider::trackColourId, col::gold);
    volume.setColour (Slider::backgroundColourId, col::line);
    volume.onValueChange = [this] { setVolume (volume.getValue()); };
    addAndMakeVisible (volume);
}

void VideoView::layoutBar()
{
    auto b = barArea().reduced (4, 4).removeFromTop (barH - 14);
    if (fsB.isVisible()) { fsB.setBounds (b.removeFromRight (28)); b.removeFromRight (4); }
    else b.removeFromRight (36);   // miesto pre „zavrieť“ na celej obrazovke
    snapB.setBounds (b.removeFromRight (28));
    b.removeFromRight (6);
    volume.setBounds (b.removeFromRight (90));
    b.removeFromRight (6);
    tracksB.setBounds (b.removeFromRight (110));
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
        char* (*get_property_string) (mpv_handle*, const char*) = nullptr;
        void (*free_) (void*) = nullptr;

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
                && get (terminate_destroy, "mpv_terminate_destroy")
                && get (get_property_string, "mpv_get_property_string") && get (free_, "mpv_free");
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

    String prop (const char* name)
    {
        if (mpv == nullptr) return {};
        char* c = api()->get_property_string (mpv, name);
        if (c == nullptr) return {};
        String r = String::fromUTF8 (c);
        api()->free_ (c);
        return r;
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
    setupBar();
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

void VideoView::setVolume (double v)
{
    resume::setVolume (v);
    volume.setValue (v, dontSendNotification);
    impl->cmd ({ "set", "volume", String (v, 0).toRawUTF8() });
}

void VideoView::showTracksMenu()
{
    if (impl->mpv == nullptr) return;
    PopupMenu audio, subs;
    const int n = impl->prop ("track-list/count").getIntValue();
    bool anySub = false, subOn = false;
    for (int i = 0; i < n; ++i)
    {
        const String base = "track-list/" + String (i) + "/";
        const auto type = impl->prop ((base + "type").toRawUTF8());
        const int id = impl->prop ((base + "id").toRawUTF8()).getIntValue();
        const bool sel = impl->prop ((base + "selected").toRawUTF8()) == "yes";
        String name = impl->prop ((base + "lang").toRawUTF8());
        const auto title = impl->prop ((base + "title").toRawUTF8());
        if (title.isNotEmpty()) name << (name.isNotEmpty() ? " – " : "") << title;
        if (impl->prop ((base + "external").toRawUTF8()) == "yes") name << tr (" (súbor)");
        if (name.isEmpty()) name = "#" + String (id);
        if (type == "audio") audio.addItem (1000 + id, name, true, sel);
        else if (type == "sub") { subs.addItem (2000 + id, name, true, sel); anySub = true; subOn = subOn || sel; }
    }
    PopupMenu m;
    m.addSectionHeader (tr ("Zvuková stopa"));
    for (PopupMenu::MenuItemIterator it (audio); it.next();) m.addItem (it.getItem());
    if (audio.getNumItems() == 0) m.addItem (-1, tr ("(žiadna)"), false);
    m.addSectionHeader (tr ("Titulky"));
    m.addItem (2000, tr ("Vypnuté"), true, ! subOn);
    for (PopupMenu::MenuItemIterator it (subs); it.next();) m.addItem (it.getItem());
    if (! anySub) m.addItem (-1, tr ("(film nemá titulky – .srt s rovnakým menom vedľa filmu sa načíta sám)"), false);
    m.addSeparator();
    m.addItem (3000, tr ("Načítať titulky zo súboru…"));
    Component::SafePointer<VideoView> sp (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&tracksB), [sp] (int r)
    {
        if (sp == nullptr || r <= 0) return;
        if (r >= 3000)
        {
            sp->chooser = std::make_unique<FileChooser> (tr ("Titulky"), sp->file.getParentDirectory(), "*.srt;*.ass;*.ssa;*.vtt;*.sub");
            sp->chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [sp] (const FileChooser& fc)
            {
                if (sp != nullptr && fc.getResult().existsAsFile())
                    sp->impl->cmd ({ "sub-add", fc.getResult().getFullPathName().toRawUTF8(), "select" });
            });
        }
        else if (r >= 2000) sp->impl->cmd ({ "set", "sid", r == 2000 ? "no" : String (r - 2000).toRawUTF8() });
        else if (r >= 1000) sp->impl->cmd ({ "set", "aid", String (r - 1000).toRawUTF8() });
    });
}

void VideoView::seekRelative (double s)
{
    impl->cmd ({ "seek", String (s, 2).toRawUTF8(), "relative" });
}

void VideoView::setStart (double s) { startAt = s; }

VideoView::~VideoView()
{
    rememberPosition();
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
    opt ("sub-auto", "fuzzy");          // titulky .srt vedľa filmu sa načítajú samy
    opt ("slang", "sk,slk,slo,cs,cze,ces,en,eng");
    opt ("alang", "sk,slk,slo,cs,cze,ces,en,eng");
    opt ("volume", String (resume::volume()).toRawUTF8());
    if (a->initialize (impl->mpv) < 0)
    {
        a->terminate_destroy (impl->mpv);
        impl->mpv = nullptr;
        return false;
    }
    return true;
}

bool VideoView::open (const File& f, bool startPaused)
{
    rememberPosition();
    file = f;
    if (! ensurePlayer()) return false;
    position = duration = 0;
    paused = startPaused;
    resumedFrom = 0;
    if (startAt <= 0.5)
    {
        const double saved = resume::get (f);
        if (saved > 0) { startAt = saved; resumedFrom = saved; }
    }
    impl->cmd ({ "set", "start", startAt > 0.5 ? ("+" + String (startAt, 2)).toRawUTF8() : "none" });
    startAt = 0;
    impl->cmd ({ "loadfile", f.getFullPathName().toRawUTF8(), "replace" });
    impl->cmd ({ "set", "pause", startPaused ? "yes" : "no" });
    startTimerHz (10);
    repaint();
    return true;
}

void VideoView::stop()
{
    rememberPosition();
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
    layoutBar();
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
VideoView::VideoView() : impl (std::make_unique<Impl>()) { setupBar(); }
void VideoView::snapshot() {}
void VideoView::setVolume (double v) { resume::setVolume (v); }
void VideoView::showTracksMenu() {}
void VideoView::seekRelative (double) {}
void VideoView::setStart (double s) { startAt = s; }
VideoView::~VideoView() = default;
bool VideoView::ensurePlayer() { return false; }
bool VideoView::open (const File&, bool) { return false; }
void VideoView::stop() {}
void VideoView::togglePause() {}
bool VideoView::isPlaying() const { return false; }
void VideoView::seekFromMouse (int) {}
void VideoView::timerCallback() {}
void VideoView::resized() { layoutBar(); }
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
        String t = (paused ? tr ("❚❚ ") : tr ("▶ ")) + timeText (position) + " / " + timeText (duration);
        if (resumedFrom > 0 && position < resumedFrom + 8)
            t << tr ("  · pokračujem tam, kde si skončil (klik na začiatok lišty = od začiatku)");
        g.drawText (t, bar.withTrimmedLeft (10).withTrimmedRight (getWidth() - tracksB.getX() + 6).withTrimmedBottom (12),
                    Justification::centredLeft, true);
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
    if (k == KeyPress::upKey) { video.setVolume (jmin (130.0, resume::volume() + 5)); return true; }
    if (k == KeyPress::downKey) { video.setVolume (jmax (0.0, resume::volume() - 5)); return true; }
    if (k == KeyPress::leftKey) { video.seekRelative (k.getModifiers().isShiftDown() ? -30.0 : -5.0); return true; }
    if (k == KeyPress::rightKey) { video.seekRelative (k.getModifiers().isShiftDown() ? 30.0 : 5.0); return true; }
    return false;
}
}
