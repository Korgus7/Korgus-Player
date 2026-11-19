#include "StatusBar.h"

namespace kp
{
StatusBar::StatusBar()
{
    targets.resize (9);
    helpButton.setTooltip (tr ("Ako funguje triedenie do priečinkov 1–9"));
    helpButton.setWantsKeyboardFocus (false);
    helpButton.onClick = [this] { showHelp(); };
    addAndMakeVisible (helpButton);
}

void StatusBar::resized()
{
    auto area = getLocalBounds().reduced (6, 5);
    area.removeFromLeft (area.getWidth() / 3);
    helpButton.setBounds (area.removeFromLeft (26).withSizeKeepingCentre (24, jmin (24, area.getHeight())));
}

void StatusBar::showHelp()
{
    struct Help : public Component
    {
        Help()
        {
            text.setMultiLine (true);
            text.setReadOnly (true);
            text.setCaretVisible (false);
            text.setScrollbarsShown (false);
            text.setFont (FontOptions (14.0f));
            text.setColour (TextEditor::backgroundColourId, Colours::transparentBlack);
            text.setColour (TextEditor::outlineColourId, Colours::transparentBlack);
            text.setColour (TextEditor::focusedOutlineColourId, Colours::transparentBlack);
            text.setColour (TextEditor::textColourId, col::text);
            text.setText (tr ("TRIEDENIE DO PRIEČINKOV 1–9\n\n"
                              "1. Nastav cieľ: klikni na políčko (napr. 2) a vyber priečinok. "
                              "Alebo otvor priečinok a stlač Ctrl+2 – cieľom sa stane práve otvorený priečinok.\n\n"
                              "2. Triedenie: pri prezeraní stlač 2 – vybraný súbor sa hneď presunie do cieľa 2. "
                              "Označené súbory (Ctrl/Shift + klik) sa presunú všetky naraz.\n\n"
                              "3. Pomýlil si sa? Ctrl+Z vráti posledný presun (aj viackrát).\n\n"
                              "4. Pravý klik na políčko cieľ zruší.\n\n"
                              "Príklad: fotky z dovolenky – 1 = Dobré, 2 = Na tlač, 3 = Zmazať. "
                              "Listuješ šípkami (aj na celej obrazovke cez Enter) a len stláčaš 1, 2, 3. "
                              "Rovnako sa triedia samply (1 = kicky, 2 = snare…)."));
            addAndMakeVisible (text);
            setSize (440, 270);
        }
        void resized() override { text.setBounds (getLocalBounds().reduced (6)); }
        TextEditor text;
    };
    CallOutBox::launchAsynchronously (std::make_unique<Help>(), helpButton.getScreenBounds(), nullptr);
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
    area.removeFromLeft (30);   // tlačidlo „?“
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
        g.setColour (set ? col::selText : col::textDim);
        g.setFont (FontOptions (13.0f));
        g.drawText (String (i + 1), rr.removeFromLeft (12), Justification::centredLeft);
        g.setFont (FontOptions (12.0f));
        g.drawText (set ? targets[i].getFileName() : tr ("—"), rr.withTrimmedLeft (4), Justification::centredLeft, true);
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
                             + tr ("Klik = vybrať priečinok, Ctrl+") + String (i + 1)
                             + tr (" = aktuálny priečinok, pravý klik = zrušiť")
                           : String());
        repaint();
    }
}
}
