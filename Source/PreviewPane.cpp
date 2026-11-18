#include "PreviewPane.h"
#include "ExtraFormats.h"

namespace kp
{
static String timeText (double s)
{
    const int t = (int) s;
    return String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2);
}

PreviewPane::PreviewPane (ThumbCache& t, ImageCache& ic) : thumbs (t), images (ic)
{
    registerAllFormats (formats);
    wave.addChangeListener (this);
    readThread.startThread();

    if (VideoView::isAvailable())
    {
        video = std::make_unique<VideoView>();
        video->onFocusBack = [this] { if (onFocusBack) onFocusBack(); };
        addChildComponent (*video);
    }
}

void PreviewPane::resized()
{
    if (video) video->setBounds (getLocalBounds().withTrimmedBottom (54));
}

PreviewPane::~PreviewPane()
{
    stopAudio();
    if (deviceReady)
    {
        deviceManager.removeAudioCallback (&sourcePlayer);
        deviceManager.closeAudioDevice();
    }
    readThread.stopThread (2000);
}

// ---------------------------------------------------------------- výber súboru

void PreviewPane::show (const File& f)
{
    if (f == current && f != File()) return;
    stopAudio();
    if (mode == Mode::Midi) midi.unload();
    if (mode == Mode::Video && video) { video->stop(); video->setVisible (false); }
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
        images.setPinned (f);
        shown = images.get (f);
        updateImageInfo();
    }
    else if (k == Kind::Audio)
    {
        mode = Mode::Audio;
        startAudio (f);
    }
    else if (k == Kind::Midi)
    {
        mode = Mode::Midi;
        if (! midi.load (f))
            infoLine = String::fromUTF8 ("MIDI súbor sa nedá načítať · ") + size;
        else
        {
            const auto& sm = midi.getSummary();
            const auto dot = String::fromUTF8 (" · ");
            infoLine = timeText (sm.length) + dot + String (sm.tracks) + String::fromUTF8 (" stôp") + dot
                     + String (sm.notes) + String::fromUTF8 (" nôt") + dot + size;
            if (midi.play (0.0)) startTimerHz (30);
            else infoLine << dot << String::fromUTF8 ("MIDI výstup sa nedá otvoriť");
        }
    }
    else if (k == Kind::Video && video != nullptr)
    {
        mode = Mode::Video;
        video->setVisible (true);
        resized();
        infoLine = String::fromUTF8 ("Video · ") + size + String::fromUTF8 (" · Medzerník = pauza · Enter = predvolený program");
        if (! video->open (f))
        {
            video->setVisible (false);
            mode = Mode::Other;
            infoLine = String::fromUTF8 ("Video sa nedá prehrať · ") + size + String::fromUTF8 (" · Enter prehrá v predvolenom programe");
        }
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

void PreviewPane::release (const File& f)
{
    if (f == current)
    {
        midi.stop();
        if (video) video->stop();
        stopAudio();
        wave.clear();
    }
}

// ---------------------------------------------------------------- obrázky

void PreviewPane::imageLoaded (const String& path)
{
    if (mode == Mode::Image && current.getFullPathName() == path)
    {
        shown = images.get (current);
        updateImageInfo();
        repaint();
    }
}

void PreviewPane::updateImageInfo()
{
    if (shown == nullptr) { infoLine = sizeText (current.getSize()); return; }
    const auto& l = shown->loaded;
    if (! l.image.isValid())
    {
        infoLine = String::fromUTF8 ("Obrázok sa nedá načítať · ") + sizeText (current.getSize());
        return;
    }
    String s;
    if (l.origW > 0) s << l.origW << String::fromUTF8 (" × ") << l.origH << String::fromUTF8 (" · ");
    s << sizeText (current.getSize());
    infoLine = s;
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
    // čítačka podľa prípony – pri čítaní z prúdu by MP3 dekodér mylne „prijal“ REX/W64 a vlna by bola zlá
    wave.setReader (formats.createReaderFor (f), f.hashCode64());
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
    if (mode == Mode::Video && video) { video->togglePause(); return; }
    if (mode == Mode::Midi)
    {
        if (midi.isPlaying()) midi.stop();
        else
        {
            const double p = midi.getPosition();
            if (midi.play (p >= midi.getLength() - 0.01 ? 0.0 : p)) startTimerHz (30);
        }
        repaint();
        return;
    }
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
    repaint (waveArea().expanded (0, 30));
    const bool playing = mode == Mode::Midi ? midi.isPlaying() : transport.isPlaying();
    if (! playing) { stopTimer(); repaint(); }
}

void PreviewPane::changeListenerCallback (ChangeBroadcaster*) { repaint (waveArea()); }

void PreviewPane::seekFromMouse (int x)
{
    auto wa = waveArea();
    if (mode == Mode::Midi)
    {
        if (midi.getLength() <= 0 || wa.getWidth() <= 0) return;
        const double pos = jlimit (0.0, 1.0, (x - wa.getX()) / (double) wa.getWidth()) * midi.getLength();
        if (midi.play (pos)) startTimerHz (30);
        repaint();
        return;
    }
    if (readerSource == nullptr || wa.getWidth() <= 0) return;
    const double pos = jlimit (0.0, 1.0, (x - wa.getX()) / (double) wa.getWidth()) * transport.getLengthInSeconds();
    transport.setPosition (pos);
    if (! transport.isPlaying()) { transport.start(); startTimerHz (30); }
    repaint (wa);
}

void PreviewPane::mouseDown (const MouseEvent& e)
{
    if ((mode == Mode::Audio || mode == Mode::Midi) && waveArea().contains (e.getPosition())) seekFromMouse (e.x);
}

void PreviewPane::mouseDrag (const MouseEvent& e)
{
    if ((mode == Mode::Audio || mode == Mode::Midi) && waveArea().contains (e.getMouseDownPosition())) seekFromMouse (e.x);
}

// ---------------------------------------------------------------- kreslenie

void PreviewPane::paintMidi (Graphics& g)
{
    auto wa = waveArea();
    g.setColour (col::panel);
    g.fillRoundedRectangle (wa.toFloat(), radius);
    const double len = midi.getLength();
    if (len <= 0) return;

    const auto inner = wa.reduced (2, 8).toFloat();
    const int lo = midi.getLowest() - 1, hi = midi.getHighest() + 1;
    const float noteH = jmax (1.0f, inner.getHeight() / (float) (hi - lo + 1));
    const double pos = midi.getPosition();
    for (auto& n : midi.getNotes())
    {
        const float x1 = inner.getX() + (float) (n.start / len) * inner.getWidth();
        const float x2 = inner.getX() + (float) (n.end / len) * inner.getWidth();
        const float y = inner.getBottom() - (float) (n.pitch - lo + 1) * noteH;
        const auto c = Colour::fromHSV ((float) ((n.channel - 1) % 16) / 16.0f, 0.45f, 0.9f, 1.0f);
        g.setColour (n.start <= pos ? c : c.withAlpha (0.45f));
        g.fillRect (x1, y, jmax (1.5f, x2 - x1), jmax (1.0f, noteH - 0.5f));
    }
    const int split = wa.getX() + (int) (wa.getWidth() * pos / len);
    g.setColour (Colours::white.withAlpha (0.85f));
    g.fillRect (split, wa.getY(), 1, wa.getHeight());

    g.setColour (col::text);
    g.setFont (FontOptions (13.0f));
    const String state = midi.isPlaying() ? String::fromUTF8 ("▶ ") : String::fromUTF8 ("❚❚ ");
    g.drawText (state + timeText (pos) + " / " + timeText (len)
                    + (midi.getDeviceName().isNotEmpty() ? String::fromUTF8 ("   ·   ") + midi.getDeviceName() : String()),
                wa.translated (0, wa.getHeight() + 4).withHeight (20), Justification::centredLeft, true);
}

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
    else if (mode == Mode::Midi)
        paintMidi (g);
    else if (mode == Mode::Other && thumbs.get (current).isValid())
    {
        // náhľad z Windows (video, PDF…)
        auto area = getLocalBounds().withTrimmedBottom (54).reduced (8);
        auto im = thumbs.get (current);
        auto fit = RectanglePlacement (RectanglePlacement::centred).appliedTo (im.getBounds().toFloat(), area.toFloat().withSizeKeepingCentre (jmin ((float) area.getWidth(), (float) im.getWidth() * 2.0f), jmin ((float) area.getHeight(), (float) im.getHeight() * 2.0f)));
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (im, fit);
        if (kindOf (current) == Kind::Video)
        {
            auto c = fit.getCentre();
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillEllipse (Rectangle<float> (64, 64).withCentre (c));
            Path p;
            p.addTriangle (c.x - 10, c.y - 16, c.x - 10, c.y + 16, c.x + 18, c.y);
            g.setColour (Colours::white);
            g.fillPath (p);
        }
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
