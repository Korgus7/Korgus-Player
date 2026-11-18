#include "PreviewPane.h"
#include "ExtraFormats.h"
#include "FormatInfo.h"
#include "WinImaging.h"
#include "ImageEdit.h"
#include "AudioTags.h"

namespace kp
{
static String timeText (double s)
{
    const int t = (int) s;
    return String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2);
}

PreviewPane::PreviewPane (ThumbCache& t, ImageCache& ic, AudioPlayer& p) : thumbs (t), images (ic), player (p)
{
    registerAllFormats (formats);
    wave.addChangeListener (this);

    openWithButton.setButtonText (tr ("Otvoriť v…"));
    openWithButton.setTooltip (tr ("Otvoriť súbor v inom programe"));
    openWithButton.onClick = [this] { showOpenWithMenu(); };
    addChildComponent (openWithButton);

    for (auto* b : { &rotL, &rotR, &resizeB, &editB })
    {
        b->background = true;
        b->setWantsKeyboardFocus (false);
        addChildComponent (*b);
    }
    rotL.onClick = [this] { if (onRotate) onRotate (false); };
    rotR.onClick = [this] { if (onRotate) onRotate (true); };
    resizeB.onClick = [this] { if (onResize) onResize(); };
    editB.onClick = [this] { if (onEdit) onEdit(); };

    if (VideoView::isAvailable())
    {
        video = std::make_unique<VideoView>();
        video->onFocusBack = [this] { if (onFocusBack) onFocusBack(); };
        video->onFullscreen = [this] { if (onVideoFullscreen) onVideoFullscreen(); };
        video->onSnapshot = [this] (const File& out) { if (onSnapshot) onSnapshot (out); };
        addChildComponent (*video);
    }
}

void PreviewPane::resized()
{
    openWithButton.setBounds (infoArea().removeFromTop (24).removeFromRight (96));
    auto tools = getLocalBounds().withTrimmedBottom (infoH).reduced (10).removeFromTop (30);
    editB.setBounds (tools.removeFromRight (96));
    tools.removeFromRight (4);
    resizeB.setBounds (tools.removeFromRight (96));
    tools.removeFromRight (4);
    rotR.setBounds (tools.removeFromRight (32));
    tools.removeFromRight (4);
    rotL.setBounds (tools.removeFromRight (32));
    if (video) video->setBounds (getLocalBounds().withTrimmedBottom (infoH));
    if (doc) doc->setBounds (getLocalBounds().withTrimmedBottom (infoH));
}

PreviewPane::~PreviewPane()
{
    stopTimer();
}

// ---------------------------------------------------------------- výber súboru

void PreviewPane::show (const File& f)
{
    if (f == current && f != File()) return;
    if (mode == Mode::Midi) midi.unload();
    if (mode == Mode::Video && video) { video->stop(); video->setVisible (false); }
    if (mode == Mode::Doc && doc && (f == File() || kindOf (f) != docKind)) { doc.reset(); docKind = Kind::Other; }
    current = f;
    shown.reset();
    infoLine = {};
    openWithButton.setVisible (f != File() && ! f.isDirectory());
    updateEditButtons();

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
        photo = readPhotoInfo (f);
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
            infoLine = tr ("MIDI súbor sa nedá načítať · ") + size;
        else
        {
            const auto& sm = midi.getSummary();
            const auto dot = tr (" · ");
            infoLine = timeText (sm.length) + dot + String (sm.tracks) + tr (" stôp") + dot
                     + String (sm.notes) + tr (" nôt") + dot + size;
            if (midi.play (0.0)) startTimerHz (30);
            else infoLine << dot << tr ("MIDI výstup sa nedá otvoriť");
        }
    }
    else if (isDocKind (k))
    {
        mode = Mode::Doc;
        if (doc == nullptr || docKind != k)
        {
            doc = createViewer (k);
            docKind = k;
            doc->onInfoChanged = [this] { if (doc) { infoLine = doc->infoText(); repaint (infoArea()); } };
            addAndMakeVisible (*doc);
            resized();
        }
        doc->load (f);
    }
    else if (k == Kind::Video && video != nullptr)
    {
        mode = Mode::Video;
        video->setVisible (true);
        resized();
        infoLine = tr ("Video · ") + size + tr (" · Medzerník = pauza · Enter = celá obrazovka · fotoaparát = uložiť snímku");
        if (! video->open (f))
        {
            video->setVisible (false);
            mode = Mode::Other;
            infoLine = tr ("Video sa nedá prehrať · ") + size + tr (" · Enter prehrá v predvolenom programe");
        }
    }
    else
    {
        mode = Mode::Other;
        if (k == Kind::Folder) infoLine = tr ("Priečinok · Enter otvorí");
        else if (k == Kind::Video) infoLine = tr ("Video · ") + size + tr (" · na prehrávanie tu: ☰ → Stiahnuť prehrávač videa");
        else infoLine = size + tr (" · Enter otvorí v predvolenom programe");
    }
    repaint();
}

void PreviewPane::release (const File& f)
{
    player.releaseIfCurrent (f);
    if (f == current)
    {
        midi.stop();
        if (video) video->stop();
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
        infoLine = tr ("Obrázok sa nedá načítať · ") + sizeText (current.getSize());
        return;
    }
    String s;
    if (l.origW > 0) s << l.origW << tr (" × ") << l.origH << tr (" · ");
    s << sizeText (current.getSize());
    infoLine = s;
}

// ---------------------------------------------------------------- zvuk

void PreviewPane::startAudio (const File& f)
{
    // čítačka podľa prípony – pri čítaní z prúdu by MP3 dekodér mylne „prijal“ REX/W64 a vlna by bola zlá
    wave.setReader (formats.createReaderFor (f), f.hashCode64());
    previewTags = readAudioTags (f);
    playerChanged();
}

void PreviewPane::playerChanged()
{
    if (mode != Mode::Audio) return;
    const auto& tg = previewTags;
    String tagLine;
    if (! tg.empty())
    {
        tagLine = tg.displayName (current);
        if (tg.album.isNotEmpty()) tagLine << tr (" · ") << tg.album;
        if (tg.year.isNotEmpty()) tagLine << " (" << tg.year << ")";
        tagLine << tr (" · ");
    }
    if (playingThis())
    {
        infoLine = tagLine + player.getFormatText() + tr (" · ") + sizeText (current.getSize());
        if (player.getDeviceError().isNotEmpty()) infoLine << tr (" · ") << player.getDeviceError();
    }
    else if (wave.getTotalLength() <= 0.0 && player.getCurrent() == current)
        infoLine = tagLine + tr ("Tento zvukový formát sa nedá prehrať · ") + sizeText (current.getSize());
    else
        infoLine = tagLine + sizeText (current.getSize()) + tr (" · Medzerník / klik do vlny = prehrať");
    if (player.isPlaying() && playingThis()) startTimerHz (30);
    repaint();
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
    if (mode == Mode::Audio && ! playingThis()) { player.play (current); return; }
    player.togglePause();
    startTimerHz (30);
    repaint();
}

void PreviewPane::timerCallback()
{
    repaint (waveArea().expanded (0, 30));
    const bool playing = mode == Mode::Midi ? midi.isPlaying() : (player.isPlaying() && playingThis());
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
    if (wa.getWidth() <= 0) return;
    if (! playingThis()) player.play (current);
    const double pos = jlimit (0.0, 1.0, (x - wa.getX()) / (double) wa.getWidth()) * player.getLength();
    player.seek (pos);
    startTimerHz (30);
    repaint (wa);
}

int PreviewPane::chipAt (Point<int> p) const
{
    for (size_t i = 0; i < chips.size(); ++i)
        if (chips[i].r.contains (p)) return (int) i;
    return -1;
}

void PreviewPane::mouseMove (const MouseEvent& e)
{
    setMouseCursor (chipAt (e.getPosition()) >= 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void PreviewPane::mouseDown (const MouseEvent& e)
{
    if (const int ci = chipAt (e.getPosition()); ci >= 0)
    {
        const auto name = chips[(size_t) ci].name;
        if (name == "__map__") { URL (photo.mapUrl()).launchInDefaultBrowser(); return; }
        if (name.startsWith ("__star")) { if (onRating) onRating (name.substring (6).getIntValue()); return; }
        if (name.startsWith ("__label")) { if (onLabel) onLabel (name.substring (7).getIntValue()); return; }
        auto copy = [this, name]
        {
            SystemClipboard::copyTextToClipboard (name);
            copiedName = name;
            repaint (infoArea());
            Component::SafePointer<PreviewPane> sp (this);
            Timer::callAfterDelay (2500, [sp, name] { if (sp != nullptr && sp->copiedName == name) { sp->copiedName = {}; sp->repaint (sp->infoArea()); } });
        };
        if (! e.mods.isPopupMenu()) { copy(); return; }
        PopupMenu m;
        m.addItem (1, tr ("Kopírovať „") + name + tr ("“"));
        m.addItem (2, tr ("Vyhľadať na webe"));
        m.showMenuAsync (PopupMenu::Options(), [copy, name] (int r)
        {
            if (r == 1) copy();
            if (r == 2) URL ("https://www.google.com/search?q=" + URL::addEscapeChars (name + " download", true)).launchInDefaultBrowser();
        });
        return;
    }
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
    g.setColour (col::text.withAlpha (0.85f));
    g.fillRect (split, wa.getY(), 1, wa.getHeight());

    g.setColour (col::text);
    g.setFont (FontOptions (13.0f));
    const String state = midi.isPlaying() ? tr ("▶ ") : tr ("❚❚ ");
    g.drawText (state + timeText (pos) + " / " + timeText (len)
                    + (midi.getDeviceName().isNotEmpty() ? tr ("   ·   ") + midi.getDeviceName() : String()),
                wa.translated (0, wa.getHeight() + 4).withHeight (20), Justification::centredLeft, true);
}

Rectangle<int> PreviewPane::infoArea() const { return getLocalBounds().removeFromBottom (infoH).reduced (12, 6); }

void PreviewPane::updateEditButtons()
{
    const bool img = current != File() && kindOf (current) == Kind::Image;
    rotL.setVisible (img && edit::canRotate (current));
    rotR.setVisible (img && edit::canRotate (current));
    resizeB.setVisible (img && edit::canResize (current));
    editB.setVisible (img && edit::canResize (current));
}

void PreviewPane::showOpenWithMenu()
{
    const auto f = current;
    if (f == File()) return;
    PopupMenu m;
    const auto apps = win::openWithApps (f);
    for (int i = 0; i < apps.size(); ++i) m.addItem (100 + i, apps[i]);
    if (! apps.isEmpty()) m.addSeparator();
    m.addItem (1, tr ("Predvolený program"));
    m.addItem (2, tr ("Ukázať v priečinku"));
    if (apps.isEmpty())
        if (auto* fi = formatInfo (f); fi != nullptr && fi->freeApps.isNotEmpty())
        {
            m.addSeparator();
            m.addSectionHeader (tr ("Odporúčané zadarmo: ") + fi->freeApps);
        }
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&openWithButton), [f] (int r)
    {
        if (r >= 100) win::openWithApp (f, r - 100);
        else if (r == 1) f.startAsProcess();
        else if (r == 2) f.revealToUser();
    });
}

Rectangle<int> PreviewPane::waveArea() const
{
    auto r = getLocalBounds().withTrimmedBottom (infoH).reduced (16);
    return r.withSizeKeepingCentre (r.getWidth(), jmin (220, r.getHeight()));
}

void PreviewPane::paint (Graphics& g)
{
    g.fillAll (col::bg);

    if (mode == Mode::Image)
    {
        auto area = getLocalBounds().withTrimmedBottom (infoH).reduced (8);
        Image im = shown != nullptr ? shown->display : thumbs.get (current); // kým sa načíta, ukáž náhľad
        if (im.isValid())
        {
            if (im.hasAlphaChannel())
                drawChecker (g, RectanglePlacement (RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize)
                                    .appliedTo (im.getBounds().toFloat(), area.toFloat()));
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImageWithin (im, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                               RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize);
        }
    }
    else if (mode == Mode::Audio)
    {
        auto wa = waveArea();
        if (previewTags.cover.isValid())
        {
            const int top = 12, avail = wa.getY() - top - 10;
            if (avail > 60)
            {
                auto c = Rectangle<int> (0, top, getWidth(), avail).withSizeKeepingCentre (jmin (avail, getWidth() - 40), avail).toFloat();
                g.drawImage (previewTags.cover, RectanglePlacement (RectanglePlacement::centred).appliedTo (previewTags.cover.getBounds().toFloat(), c));
            }
        }
        g.setColour (col::panel);
        g.fillRoundedRectangle (wa.toFloat(), radius);
        if (wave.getTotalLength() > 0.0)
        {
            const double len = wave.getTotalLength();
            const int split = playingThis() && len > 0
                                ? wa.getX() + (int) (wa.getWidth() * player.getPosition() / len) : wa.getX();
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
            g.setColour (col::text.withAlpha (0.85f));
            g.fillRect (split, wa.getY(), 1, wa.getHeight());
        }
        // čas
        g.setColour (col::text);
        g.setFont (FontOptions (13.0f));
        const String state = player.isPlaying() ? tr ("▶ ") : tr ("❚❚ ");
        if (playingThis())
            g.drawText (state + timeText (player.getPosition()) + " / " + timeText (player.getLength()),
                        wa.translated (0, wa.getHeight() + 4).withHeight (20), Justification::centredLeft);
    }
    else if (mode == Mode::Midi)
        paintMidi (g);
    else if (mode == Mode::Other && thumbs.get (current).isValid())
    {
        // náhľad z Windows (video, PDF…)
        auto area = getLocalBounds().withTrimmedBottom (infoH).reduced (8);
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
        auto box = getLocalBounds().withTrimmedBottom (infoH).withSizeKeepingCentre (120, 140).toFloat();
        const auto k = kindOf (current);
        g.setColour (col::panelHi);
        g.fillRoundedRectangle (box, radius);
        g.setColour (kindColour (k));
        g.fillRoundedRectangle (box.removeFromBottom (40), radius);
        g.setColour (Colours::white);
        g.setFont (FontOptions (16.0f, Font::bold));
        const auto ext = k == Kind::Folder ? tr ("PRIEČINOK") : current.getFileExtension().substring (1).toUpperCase();
        g.drawText (ext, getLocalBounds().withTrimmedBottom (infoH).withSizeKeepingCentre (120, 140).removeFromBottom (40),
                    Justification::centred);
    }

    if (current != File())
    {
        auto ia = infoArea();
        auto top = ia.removeFromTop (22);
        top.removeFromRight (104);
        g.setColour (col::text);
        g.setFont (FontOptions (14.0f, Font::bold));
        g.drawText (current.getFileName(), top, Justification::centredLeft, true);
        g.setColour (col::textDim);
        g.setFont (FontOptions (12.5f));
        auto infoRow = ia.removeFromTop (18);
        chips.clear();
        std::vector<Chip> metaChips;
        if (meta != nullptr && ! current.isDirectory())
        {
            // hviezdičky a farebné štítky (klik nastaví)
            const auto m = meta->get (current);
            auto right = infoRow.removeFromRight (5 * 16 + 8 + MetaStore::numLabels * 14);
            for (int i = 1; i <= 5; ++i)
            {
                auto r = right.removeFromLeft (16);
                g.setColour (i <= m.rating ? col::gold : col::textDim.withAlpha (0.5f));
                g.setFont (FontOptions (15.0f));
                g.drawText (tr ("★"), r, Justification::centred, false);
                metaChips.push_back ({ r, {}, "__star" + String (i == m.rating ? 0 : i) });
            }
            right.removeFromLeft (8);
            for (int i = 1; i <= MetaStore::numLabels; ++i)
            {
                auto r = right.removeFromLeft (14);
                auto dot = Rectangle<float> (10, 10).withCentre (r.getCentre().toFloat());
                g.setColour (MetaStore::labelColour (i).withAlpha (i == m.label ? 1.0f : 0.35f));
                g.fillEllipse (dot);
                if (i == m.label) { g.setColour (Colours::white); g.drawEllipse (dot.expanded (1.5f), 1.2f); }
                metaChips.push_back ({ r, {}, "__label" + String (i == m.label ? 0 : i) });
            }
            g.setColour (col::textDim);
            g.setFont (FontOptions (12.5f));
        }
        g.drawText (infoLine, infoRow, Justification::centredLeft, true);

        if (mode == Mode::Image && ! photo.empty())
        {
            ia.removeFromTop (4);
            g.setColour (col::line);
            g.fillRect (ia.removeFromTop (1));
            ia.removeFromTop (4);
            chips.clear();
            g.setColour (col::gold);
            g.setFont (FontOptions (12.5f, Font::bold));
            g.drawText (photo.camera.isNotEmpty() ? photo.camera : tr ("Fotografia"), ia.removeFromTop (17), Justification::centredLeft, true);
            g.setColour (col::text.withAlpha (0.85f));
            g.setFont (FontOptions (12.0f));
            String line = photo.settings;
            if (photo.lens.isNotEmpty()) line << (line.isEmpty() ? String() : tr (" · ")) << photo.lens;
            if (line.isNotEmpty()) g.drawText (line, ia.removeFromTop (17), Justification::centredLeft, true);
            auto last = ia.removeFromTop (22);
            if (photo.hasTaken)
            {
                g.setColour (col::textDim);
                const auto t = tr ("Odfotené ") + photo.taken.formatted ("%d.%m.%Y %H:%M");
                const int w = (int) GlyphArrangement::getStringWidth (Font (FontOptions (12.0f)), t) + 12;
                g.drawText (t, last.removeFromLeft (w), Justification::centredLeft, false);
            }
            if (photo.hasGps)
            {
                Rectangle<int> r (last.getX(), last.getY() + 1, 120, 19);
                chips.push_back ({ r, tr ("Mapa (GPS)"), "__map__" });
                g.setColour (col::accent);
                g.fillRoundedRectangle (r.toFloat(), radius);
                g.setColour (col::text);
                g.drawText (tr ("Ukázať na mape"), r, Justification::centred, true);
            }
        }
        else if (auto* fi = formatInfo (current))
        {
            ia.removeFromTop (4);
            g.setColour (col::line);
            g.fillRect (ia.removeFromTop (1));
            ia.removeFromTop (4);
            g.setColour (col::gold);
            g.setFont (FontOptions (12.5f, Font::bold));
            auto titleRow = ia.removeFromTop (17);
            g.drawText (fi->name, titleRow, Justification::centredLeft, true);
            if (copiedName.isNotEmpty())
            {
                g.setColour (col::text);
                g.setFont (FontOptions (12.0f));
                g.drawText (tr ("Skopírované: ") + copiedName, titleRow, Justification::centredRight, true);
            }
            const int noteH = fi->note.isNotEmpty() ? 15 : 0;

            // čipy s názvami programov (zalamujú sa do riadkov)
            chips.clear();
            auto area = ia.withTrimmedBottom (noteH).withTrimmedTop (3);
            const Font chipFont (FontOptions (12.0f));
            const Font labelFont (FontOptions (12.0f, Font::bold));
            int x = area.getX(), y = area.getY();
            const int chipH = 19, gapX = 5, gapY = 4;
            auto addGroup = [&] (const String& label, const String& list)
            {
                if (list.isEmpty()) return;
                const int lw = (int) GlyphArrangement::getStringWidth (labelFont, label) + 6;
                if (x + lw > area.getRight() && x > area.getX()) { x = area.getX(); y += chipH + gapY; }
                g.setFont (labelFont);
                g.setColour (col::textDim);
                g.drawText (label, x, y, lw, chipH, Justification::centredLeft, false);
                x += lw;
                for (auto part : StringArray::fromTokens (list, ",;", ""))
                {
                    part = part.trim();
                    if (part.isEmpty()) continue;
                    // „úprava: Audacity“ → Audacity; „Word na webe (zadarmo…)“ → bez zátvorky
                    String name = part.fromFirstOccurrenceOf (":", false, false).trim();
                    if (! part.containsChar (':')) name = part;
                    name = name.upToFirstOccurrenceOf (" (", false, false).trim();
                    const int w = jmin (area.getWidth(), (int) std::ceil (GlyphArrangement::getStringWidth (chipFont, name)) + 20);
                    if (x + w > area.getRight() && x > area.getX()) { x = area.getX(); y += chipH + gapY; }
                    if (y + chipH > area.getBottom() + 1) return;
                    Rectangle<int> r (x, y, w, chipH);
                    chips.push_back ({ r, name, name });
                    x += w + gapX;
                }
                x += 6;
            };
            addGroup (tr ("Zadarmo:"), fi->freeApps);
            addGroup (tr ("Pôvodný:"), fi->paidApps);
            g.setFont (chipFont);
            for (auto& c : chips)
            {
                g.setColour (col::panelHi);
                g.fillRoundedRectangle (c.r.toFloat(), radius);
                g.setColour (col::text);
                g.drawText (c.label, c.r.reduced (6, 0), Justification::centred, true);
            }
            if (noteH > 0)
            {
                g.setColour (col::textDim);
                g.drawText (fi->note, ia.removeFromBottom (noteH), Justification::centredLeft, true);
            }
        }
        for (auto& c : metaChips) chips.push_back (c);
    }
}
}

namespace kp
{
double PreviewPane::getVideoPosition() const { return (mode == Mode::Video && video) ? video->getPosition() : 0.0; }
}
