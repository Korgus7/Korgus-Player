#pragma once
#include "Common.h"

namespace kp
{
// Spodná lišta: cieľové priečinky 1–9 a správa o poslednej akcii.
class StatusBar : public Component, public SettableTooltipClient, private Timer
{
public:
    StatusBar();

    void setTargets (const Array<File>& t) { targets = t; repaint(); }
    void showMessage (const String& m, bool error = false);
    void setFolderInfo (const String& s) { folderInfo = s; repaint(); }

    std::function<void (int)> onSlotClicked;   // ľavý klik = vybrať priečinok
    std::function<void (int)> onSlotCleared;   // pravý klik = zrušiť
    void resized() override;

    void paint (Graphics& g) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseMove (const MouseEvent& e) override;
    void mouseExit (const MouseEvent&) override { hover = -1; repaint(); }

private:
    void timerCallback() override { message = {}; stopTimer(); repaint(); }
    Rectangle<int> slotBounds (int i) const;
    int slotAt (Point<int> p) const;

    Array<File> targets;
    String message, folderInfo;
    bool messageIsError = false;
    int hover = -1;
    TextButton helpButton { "?" };
    void showHelp();
};
}
