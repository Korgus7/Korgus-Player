#include "FullscreenDoc.h"

namespace kp
{
FullscreenDoc::FullscreenDoc (Kind k) : kind (k), viewer (createViewer (k))
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    if (viewer)
    {
        viewer->setWantsKeyboardFocus (false);
        viewer->onInfoChanged = [this] { repaint (getLocalBounds().removeFromBottom (barH)); };
        addAndMakeVisible (*viewer);
    }
}

void FullscreenDoc::setFile (const File& f, int pos, int tot)
{
    position = pos;
    total = tot;
    if (f != file && viewer) viewer->load (f);
    file = f;
    repaint();
}

void FullscreenDoc::showMessage (const String& m, bool error)
{
    message = m;
    messageIsError = error;
    startTimer (2500);
    repaint();
}

void FullscreenDoc::resized()
{
    if (viewer) viewer->setBounds (getLocalBounds().withTrimmedBottom (barH));
}

void FullscreenDoc::paint (Graphics& g)
{
    g.fillAll (Colours::black);
    auto bar = getLocalBounds().removeFromBottom (barH);
    g.setColour (col::panel);
    g.fillRect (bar);
    g.setFont (FontOptions (13.0f));
    g.setColour (col::text);
    const auto dot = String::fromUTF8 ("   ·   ");
    String info = file.getFileName() + dot + String (position) + " / " + String (total);
    if (viewer) info << dot << viewer->infoText();
    g.drawText (info, bar.reduced (14, 0), Justification::centredLeft, true);
    g.setColour (col::textDim);
    if (viewer) g.drawText (viewer->hintText(), bar.reduced (14, 0), Justification::centredRight, true);
}

bool FullscreenDoc::keyPressed (const KeyPress& key)
{
    const int k = key.getKeyCode();
    const auto mods = key.getModifiers();
    if (! mods.isCtrlDown() && ! mods.isAltDown())
    {
        if (k == KeyPress::escapeKey || k == KeyPress::returnKey || k == KeyPress::F11Key) { if (onClose) onClose(); return true; }
        if (viewer && viewer->handleKey (key)) return true;
        if (k == KeyPress::leftKey)  { if (onStep) onStep (-1); return true; }
        if (k == KeyPress::rightKey) { if (onStep) onStep (1); return true; }
        if (k == KeyPress::homeKey)  { if (onStep) onStep (-100000); return true; }
        if (k == KeyPress::endKey)   { if (onStep) onStep (100000); return true; }
    }
    return onOtherKey != nullptr && onOtherKey (key);
}
}
