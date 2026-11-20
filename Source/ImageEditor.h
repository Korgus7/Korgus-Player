#pragma once
#include "Common.h"

namespace kp
{
// Jednoduchý editor: orezanie (aj s pomerom strán), jas, kontrast, sýtosť
class ImageEditor : public Component
{
public:
    explicit ImageEditor (const File& f);

    std::function<void (const File& saved, bool overwritten)> onSaved;
    std::function<void()> onClose;
    std::function<void (const String&, bool)> onMessage;

    void paint (Graphics& g) override;
    void resized() override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    bool keyPressed (const KeyPress& key) override;

private:
    void updatePreview();
    Image process (const Image& src) const;
    Rectangle<float> imageArea() const;      // kde je obrázok na obrazovke
    Rectangle<float> cropOnScreen() const;
    void save (bool overwrite);
    void applyAspect();
    void nudge (int dx, int dy, int dw, int dh);   // presne nastavenie vyrezu v pixeloch originalu
    float aspectRatio() const;

    File file;
    Image original, preview, adjusted;
    Rectangle<float> crop { 0, 0, 1, 1 };   // relatívne 0..1
    Rectangle<float> dragStartCrop;
    Point<float> dragStart;
    bool moving = false;

    Slider brightness, contrast, saturation;
    Label bLabel, cLabel, sLabel;
    ComboBox aspect;
    TextButton saveCopy, overwrite, cancel, resetB;
    TextButton mvL, mvR, mvU, mvD, wMinus, wPlus, hMinus, hPlus;
    int fullW = 1, fullH = 1;
    static constexpr int panelW = 260;
};
}
