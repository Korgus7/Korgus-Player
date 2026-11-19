#pragma once
#include "Common.h"
#include "AudioPlayer.h"
#include "Icons.h"

namespace kp
{
// Lišta prehrávača hudby: ⏮ ▶ ⏹ ⏭, skladba, posuvník, opakovanie, náhodne, hlasitosť
class PlayerBar : public Component, private Timer
{
public:
    explicit PlayerBar (AudioPlayer& p);

    void update();   // po zmene stavu prehrávača
    std::function<void()> onSettingsChanged;
    std::function<void (const String&)> onInfo;
    std::function<void (const File&)> onShowTrack;

    void paint (Graphics& g) override;
    void resized() override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;

private:
    void timerCallback() override { repaint (progressArea.expanded (0, 6)); repaint (timeArea); }
    void seekFrom (int x);

    AudioPlayer& player;
    IconButton prevB { Icon::Prev, tr ("Predošlá skladba (Ctrl+←)") },
               playB { Icon::Play, tr ("Prehrať / pauza (medzerník)") },
               stopB { Icon::Stop, tr ("Zastaviť") },
               nextB { Icon::Next, tr ("Ďalšia skladba (Ctrl+→)") },
               repeatB { Icon::Repeat, tr ("Opakovanie: vypnuté / celý zoznam / jedna skladba") },
               shuffleB { Icon::Shuffle, tr ("Náhodné poradie") };
    Slider volume;
    TextButton tempoB;
public:
    // tempo má zmysel len pri zvuku – pri obrázkoch a iných súboroch sa skryje
    void setTempoVisible (bool v) { tempoB.setVisible (v); }
private:
    void showTempoMenu();
    void updateTempoButton();
    Rectangle<int> titleArea, progressArea, timeArea;
};
}
