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
    IconButton prevB { Icon::Prev, String::fromUTF8 ("Predošlá skladba (Ctrl+←)") },
               playB { Icon::Play, String::fromUTF8 ("Prehrať / pauza (medzerník)") },
               stopB { Icon::Stop, String::fromUTF8 ("Zastaviť") },
               nextB { Icon::Next, String::fromUTF8 ("Ďalšia skladba (Ctrl+→)") },
               repeatB { Icon::Repeat, String::fromUTF8 ("Opakovanie: vypnuté / celý zoznam / jedna skladba") },
               shuffleB { Icon::Shuffle, String::fromUTF8 ("Náhodné poradie") };
    Slider volume;
    Rectangle<int> titleArea, progressArea, timeArea;
};
}
