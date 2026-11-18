#pragma once
#include "Common.h"
#include "DocViewers.h"

namespace kp
{
// Celá obrazovka pre fonty, PDF, 3D modely a výkresy
class FullscreenDoc : public Component, private Timer
{
public:
    explicit FullscreenDoc (Kind k);

    Kind getKind() const { return kind; }
    void setFile (const File& f, int position, int total);
    void showMessage (const String& m, bool error = false);

    std::function<void (int)> onStep;
    std::function<void()> onClose;
    std::function<bool (const KeyPress&)> onOtherKey;

    void paint (Graphics& g) override;
    void resized() override;
    bool keyPressed (const KeyPress& key) override;

private:
    void timerCallback() override { message = {}; stopTimer(); repaint(); }

    Kind kind;
    std::unique_ptr<DocViewer> viewer;
    File file;
    int position = 0, total = 0;
    String message;
    bool messageIsError = false;
    static constexpr int barH = 30;
};
}
