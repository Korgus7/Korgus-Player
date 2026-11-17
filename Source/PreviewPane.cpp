#include "PreviewPane.h"

namespace kp
{
static String timeText (double s)
{
    const int t = (int) s;
    return String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2);
}

PreviewPane::PreviewPane (ThumbCache& t) : thumbs (t)
{
    formats.registerBasicFormats();
    wave.addChangeListener (this);
    readThread.startThread();

    // veľkosť obrázka podľa najväčšej obrazovky (v pixeloch)
    int best = 1920;
    for (auto& d : Desktop::getInstance().getDisplays().displays)
        best = jmax (best, (int) (jmax (d.logicalBounds.getWidth(), d.logicalBounds.getHeight()) * d.scale));
    maxSide = jlimit (1920, 4096, best);
}

PreviewPane::~PreviewPane()
{
    *alive = false;
    stopAudio();
    if (deviceReady)
    {
        deviceManager.removeAudioCallback (&sourcePlayer);
        deviceManager.closeAudioDevice();
    }
    readThread.stopThread (2000);
    loader.removeAllJobs (true, 3000);
}

// ---------------------------------------------------------------- výber súboru

void PreviewPane::show (const File& f)
{
    if (f == current && f != File()) return;
    stopAudio();
    current = f;
    shown.reset();
    infoLine = {};

    if (f == File())
    {
        mode = Mode::None;
        repaint();
        return;
    }

    const auto k = kindOf (f);
    const auto size = f.isDirectory() ? String() : sizeText (f.getSize());
    if (k == Kind::Image || k == Kind::Svg)
    {
        mode = Mode::Image;
        requestImage (f, true);
    }
    else if (k == Kind::Audio)
    {
        mode = Mode::Audio;
        startAudio (f);
    }
    else
    {
        mode = Mode::Other;
        if (k == Kind::Folder) infoLine = String::fromUTF8 ("Priečinok · Enter otvorí");
        else if (k == Kind::Video) infoLine = String::fromUTF8 ("Video · ") + size + String::fromUTF8 (" · Enter prehrá v predvolenom programe");
        else infoLine = size + String::fromUTF8 (" · Enter otvorí v predvolenom programe");
    }
    repaint();
}

void PreviewPane::prefetch (const Array<File>& files)
{
    for (auto& f : files)
    {
        const auto k = kindOf (f);
        if (k == Kind::Image || k == Kind::Svg) requestImage (f, false);
    }
}

void PreviewPane::release (const File& f)
{
    if (f == current)
    {
        stopAudio();
        wave.clear();
    }
}

// ---------------------------------------------------------------- obrázky

void PreviewPane::requestImage (const File& f, bool showNow)
{
    const auto path = f.getFullPathName();
    if (images.contains (path))
    {
        if (showNow) imageArrived (path, images[path]->loaded);
        return;
    }
    if (loading.contains (path)) return;
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
            imageArrived (path, loaded);
        });
    });
}

void PreviewPane::imageArrived (const String& path, const img::Loaded& loaded)
{
    std::shared_ptr<Cached> c;
    if (images.contains (path)) c = images[path];
    else
    {
        c = std::make_shared<Cached>();
        c->loaded = loaded;
        // na grafickú kartu až tu (na hlavnom vlákne)
        c->display = loaded.image.isValid() ? NativeImageType().convert (loaded.image) : Image();
        images.set (path, c);
        imageOrder.push_back (path);
        while (imageOrder.size() > 7)
        {
            if (imageOrder.front() != current.getFullPathName())
                images.remove (imageOrder.front());
            imageOrder.pop_front();
        }
    }

    if (current.getFullPathName() == path)
    {
        shown = c;
        String s;
        if (loaded.origW > 0) s << loaded.origW << String::fromUTF8 (" × ") << loaded.origH << String::fromUTF8 (" · ");
        s << sizeText (current.getSize());
        if (! loaded.image.isValid()) s = String::fromUTF8 ("Obrázok sa nedá načítať · ") + sizeText (current.getSize());
        infoLine = s;
        repaint();
    }
}

// ---------------------------------------------------------------- zvuk

bool PreviewPane::ensureAudioDevice()
{
    if (deviceReady) return true;
    if (deviceFailed) return false;
    const auto err = deviceManager.initialiseWithDefaultDevices (0, 2);
    if (err.isNotEmpty() || deviceManager.getCurrentAudioDevice() == nullptr)
    {
        deviceFailed = true;
        return false;
    }
    sourcePlayer.setSource (&transport);
    deviceManager.addAudioCallback (&sourcePlayer);
    deviceReady = true;
    return true;
}

void PreviewPane::startAudio (const File& f)
{
    wave.setSource (new FileInputSource (f));
    audioLength = 0.0;

    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr)
    {
        infoLine = String::fromUTF8 ("Tento zvukový formát sa zatiaľ nedá prehrať · ") + sizeText (f.getSize());
        return;
    }
    const double sr = reader->sampleRate;
    audioLength = sr > 0 ? (double) reader->lengthInSamples / sr : 0.0;
    const auto dot = String::fromUTF8 (" · ");
    infoLine = timeText (audioLength) + dot + String (sr / 1000.0, 1) + " kHz" + dot
             + (reader->numChannels == 1 ? String ("mono") : reader->numChannels == 2 ? String ("stereo") : String ((int) reader->numChannels) + " k")
             + dot + sizeText (f.getSize());

    readerSource = std::make_unique<AudioFormatReaderSource> (reader.release(), true);
    if (! ensureAudioDevice())
    {
        infoLine << String::fromUTF8 (" · zvukové zariadenie sa nedá otvoriť");
        return;
    }
    transport.setSource (readerSource.get(), 32768, &readThread, sr, 2);
    transport.start();
    startTimerHz (30);
}

void PreviewPane::stopAudio()
{
    stopTimer();
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
}

void PreviewPane::togglePlay()
{
    if (mode != Mode::Audio || readerSource == nullptr) return;
    if (transport.isPlaying()) transport.stop();
    else
    {
        if (transport.getCurrentPosition() >= transport.getLengthInSeconds() - 0.01)
            transport.setPosition (0.0);
        transport.start();
        startTimerHz (30);
    }
    repaint();
}

void PreviewPane::timerCallback()
{
    repaint (waveArea());
    if (! transport.isPlaying()) { stopTimer(); repaint(); }
}

void PreviewPane::changeListenerCallback (ChangeBroadcaster*) { repaint (waveArea()); }

void PreviewPane::seekFromMouse (int x)
{
    auto wa = waveArea();
    if (readerSource == nullptr || wa.getWidth() <= 0) return;
    const double pos = jlimit (0.0, 1.0, (x - wa.getX()) / (double) wa.getWidth()) * transport.getLengthInSeconds();
    transport.setPosition (pos);
    if (! transport.isPlaying()) { transport.start(); startTimerHz (30); }
    repaint (wa);
}

void PreviewPane::mouseDown (const MouseEvent& e)
{
    if (mode == Mode::Audio && waveArea().contains (e.getPosition())) seekFromMouse (e.x);
}

void PreviewPane::mouseDrag (const MouseEvent& e)
{
    if (mode == Mode::Audio && waveArea().getX() <= e.getMouseDownX()) seekFromMouse (e.x);
}

// ---------------------------------------------------------------- kreslenie

Rectangle<int> PreviewPane::infoArea() const { return getLocalBounds().removeFromBottom (54).reduced (12, 6); }

Rectangle<int> PreviewPane::waveArea() const
{
    auto r = getLocalBounds().withTrimmedBottom (54).reduced (16);
    return r.withSizeKeepingCentre (r.getWidth(), jmin (220, r.getHeight()));
}

void PreviewPane::paint (Graphics& g)
{
    g.fillAll (col::bg);

    if (mode == Mode::Image)
    {
        auto area = getLocalBounds().withTrimmedBottom (54).reduced (8);
        Image im = shown != nullptr ? shown->display : thumbs.get (current); // kým sa načíta, ukáž náhľad
        if (im.isValid())
        {
            if (kindOf (current) == Kind::Svg)
            {
                // šachovnica pod priehľadné SVG
                auto fit = RectanglePlacement (RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize)
                               .appliedTo (im.getBounds().toFloat(), area.toFloat());
                g.setColour (col::panelHi);
                g.fillRect (fit);
            }
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImageWithin (im, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                               RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize);
        }
    }
    else if (mode == Mode::Audio)
    {
        auto wa = waveArea();
        g.setColour (col::panel);
        g.fillRoundedRectangle (wa.toFloat(), radius);
        if (wave.getTotalLength() > 0.0)
        {
            const double len = wave.getTotalLength();
            const int split = readerSource != nullptr && len > 0
                                ? wa.getX() + (int) (wa.getWidth() * transport.getCurrentPosition() / len) : wa.getX();
            const auto inner = wa.reduced (0, 10);
            {
                Graphics::ScopedSaveState s (g);
                g.reduceClipRegion (inner.withRight (split));
                g.setColour (col::gold);
                wave.drawChannels (g, inner, 0.0, len, 1.0f);
            }
            {
                Graphics::ScopedSaveState s (g);
                g.reduceClipRegion (inner.withLeft (split));
                g.setColour (col::textDim);
                wave.drawChannels (g, inner, 0.0, len, 1.0f);
            }
            g.setColour (Colours::white.withAlpha (0.85f));
            g.fillRect (split, wa.getY(), 1, wa.getHeight());
        }
        // čas
        g.setColour (col::text);
        g.setFont (FontOptions (13.0f));
        const String state = transport.isPlaying() ? String::fromUTF8 ("▶ ") : String::fromUTF8 ("❚❚ ");
        if (readerSource != nullptr)
            g.drawText (state + timeText (transport.getCurrentPosition()) + " / " + timeText (audioLength),
                        wa.translated (0, wa.getHeight() + 4).withHeight (20), Justification::centredLeft);
    }
    else if (mode == Mode::Other)
    {
        auto box = getLocalBounds().withTrimmedBottom (54).withSizeKeepingCentre (120, 140).toFloat();
        const auto k = kindOf (current);
        g.setColour (col::panelHi);
        g.fillRoundedRectangle (box, radius);
        g.setColour (kindColour (k));
        g.fillRoundedRectangle (box.removeFromBottom (40), radius);
        g.setColour (Colours::white);
        g.setFont (FontOptions (16.0f, Font::bold));
        const auto ext = k == Kind::Folder ? String::fromUTF8 ("PRIEČINOK") : current.getFileExtension().substring (1).toUpperCase();
        g.drawText (ext, getLocalBounds().withTrimmedBottom (54).withSizeKeepingCentre (120, 140).removeFromBottom (40),
                    Justification::centred);
    }

    if (current != File())
    {
        auto ia = infoArea();
        g.setColour (col::text);
        g.setFont (FontOptions (14.0f, Font::bold));
        g.drawText (current.getFileName(), ia.removeFromTop (20), Justification::centredLeft, true);
        g.setColour (col::textDim);
        g.setFont (FontOptions (12.5f));
        g.drawText (infoLine, ia, Justification::centredLeft, true);
    }
}
}
