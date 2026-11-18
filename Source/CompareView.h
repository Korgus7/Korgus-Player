#pragma once
#include "Common.h"

namespace kp
{
// Porovnanie dvoch fotiek vedľa seba (spoločné priblíženie a posun).
// ← → mení pravú fotku (ďalšie v priečinku), 1 / 2 = ponechať ľavú / pravú, Esc koniec.
class CompareView : public Component
{
public:
    CompareView (const File& left, const File& right, Array<File> folderImages);

    std::function<void()> onClose;
    std::function<void (const File& keep, const File& other)> onPick;   // 1 / 2

    void paint (Graphics& g) override;
    bool keyPressed (const KeyPress& k) override;
    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    void mouseDoubleClick (const MouseEvent&) override { zoom = 1.0f; pan = {}; repaint(); }

private:
    struct Side { File file; Image image; int w = 0, h = 0; };
    void load (Side& s, const File& f);
    void paintSide (Graphics& g, const Side& s, Rectangle<float> area);
    Side a, b;
    Array<File> images;
    float zoom = 1.0f;
    Point<float> pan, dragPan;
};
}
