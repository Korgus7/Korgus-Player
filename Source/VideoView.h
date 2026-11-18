#pragma once
#include "Common.h"

namespace kp
{
// Prehrávanie videa cez libmpv (libmpv-2.dll vedľa programu). Ak knižnica chýba,
// isAvailable() vráti false a video sa otvára v predvolenom programe ako doteraz.
class VideoView : public Component, private Timer
{
public:
    VideoView();
    ~VideoView() override;

    static bool isAvailable();

    bool open (const File& f);
    void stop();
    void togglePause();
    bool isPlaying() const;

    void paint (Graphics& g) override;
    void resized() override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;

    std::function<void()> onFocusBack;   // mpv si vzal klávesnicu – vrátiť ju programu

private:
    void timerCallback() override;
    bool ensurePlayer();
    Rectangle<int> barArea() const { return getLocalBounds().removeFromBottom (barH); }
    void seekFromMouse (int x);

    struct Impl;
    std::unique_ptr<Impl> impl;
    double position = 0, duration = 0;
    bool paused = false;
    static constexpr int barH = 34;
};
}
