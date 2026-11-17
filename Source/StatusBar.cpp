#include "StatusBar.h"

namespace kp
{
StatusBar::StatusBar()
{
    targets.resize (9);
}

void StatusBar::showMessage (const String& m, bool error)
{
    message = m;
    messageIsError = error;
    startTimer (6000);
    repaint();
}

Rectangle<int> StatusBar::slotBounds (int i) const
{
    // sloty zaberajú pravé dve tretiny lišty
    auto area = getLocalBounds().reduced (6, 5);
    area.removeFromLeft (area.getWidth() / 3);
    const int w = area.getWidth() / 9;
    return { area.getX() + i * w, area.getY(), w - 4, area.getHeight() };
}

int StatusBar::slotAt (Point<int> p) const
{
    for (int i = 0; i < 9; ++i)
        if (slotBounds (i).contains (p)) return i;
    return -1;
}

void StatusBar::paint (Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::line);
    g.fillRect (0, 0, getWidth(), 1);

    auto left = getLocalBounds().reduced (10, 0);
    left.setWidth (getWidth() / 3 - 16);
    g.setFont (FontOptions (12.5f));
    if (message.isNotEmpty())
    {
        g.setColour (messageIsError ? Colour (0xffd7807a) : col::gold);
        g.drawText (message, left, Justification::centredLeft, true);
    }
    else
    {
        g.setColour (col::textDim);
        g.drawText (folderInfo, left, Justification::centredLeft, true);
    }

    for (int i = 0; i < 9; ++i)
    {
        auto r = slotBounds (i).toFloat();
        const bool set = targets[i] != File();
        g.setColour (i == hover ? col::accentHi : (set ? col::accent : col::panelHi));
        g.fillRoundedRectangle (r, radius);

        auto rr = r.reduced (6, 0);
        g.setColour (set ? Colours::white : col::textDim);
        g.setFont (FontOptions (13.0f, Font::bold));
        g.drawText (String (i + 1), rr.removeFromLeft (12), Justification::centredLeft);
        g.setFont (FontOptions (12.0f));
        g.drawText (set ? targets[i].getFileName() : String::fromUTF8 ("—"), rr.withTrimmedLeft (4), Justification::centredLeft, true);
    }
}

void StatusBar::mouseDown (const MouseEvent& e)
{
    const int i = slotAt (e.getPosition());
    if (i < 0) return;
    if (e.mods.isPopupMenu()) { if (onSlotCleared) onSlotCleared (i); }
    else if (onSlotClicked) onSlotClicked (i);
}

void StatusBar::mouseMove (const MouseEvent& e)
{
    const int i = slotAt (e.getPosition());
    if (i != hover)
    {
        hover = i;
        setTooltip (i >= 0 ? (targets[i] != File() ? targets[i].getFullPathName() + "\n" : String())
                             + String::fromUTF8 ("Klik = vybrať priečinok, Ctrl+") + String (i + 1)
                             + String::fromUTF8 (" = aktuálny priečinok, pravý klik = zrušiť")
                           : String());
        repaint();
    }
}
}
