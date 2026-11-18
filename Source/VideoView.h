#pragma once
#include "Common.h"
#include "Icons.h"

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
    void visibilityChanged() override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;

    std::function<void()> onFocusBack;   // mpv si vzal klávesnicu – vrátiť ju programu
    std::function<void()> onFullscreen;
    std::function<void (const File&)> onSnapshot;
    void seekRelative (double seconds);
    void setStart (double seconds);       // pred open(): začať od času
    double getPosition() const { return position; }
    const File& getFile() const { return file; }
    void showFullscreenButton (bool b) { fsB.setVisible (b); }

private:
    void timerCallback() override;
    bool ensurePlayer();
    Rectangle<int> barArea() const { return getLocalBounds().removeFromBottom (barH); }
    void seekFromMouse (int x);

    void snapshot();
    File file;
    double startAt = 0;
    IconButton snapB { Icon::Camera, tr ("Uložiť snímku z videa ako obrázok") },
               fsB { Icon::Fit, tr ("Na celú obrazovku") };

    struct Impl;
    std::unique_ptr<Impl> impl;
    double position = 0, duration = 0;
    bool paused = false;
    static constexpr int barH = 34;
};

// Video na celú obrazovku (Esc/Enter/F11 zavrie, ←/→ posun o 5 s, Shift ±30 s, medzerník pauza)
class VideoFullscreen : public Component
{
public:
    VideoFullscreen (const File& f, double startSeconds);
    void start_();
    std::function<void()> onClose;
    std::function<void (const File&)> onSnapshot;
    double getPosition() const { return video.getPosition(); }
    void paint (Graphics& g) override;
    void resized() override;
    bool keyPressed (const KeyPress& k) override;
private:
    VideoView video;
    IconButton closeB { Icon::Close, tr ("Zavrieť (Esc)") };
    File file;
    double start = 0;
};
}
