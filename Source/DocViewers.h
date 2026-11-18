#pragma once
#include "Common.h"

namespace kp
{
// Spoločný základ prehliadačov pre fonty, PDF, 3D modely a DXF výkresy.
// Ten istý prehliadač sa používa vo veľkej ukážke aj na celej obrazovke.
class DocViewer : public Component
{
public:
    virtual void load (const File& f) = 0;
    virtual String infoText() const { return {}; }
    virtual bool handleKey (const KeyPress&) { return false; }   // napr. strany PDF
    virtual String hintText() const { return {}; }               // nápoveda na celej obrazovke
    std::function<void()> onInfoChanged;

protected:
    void infoChanged() { if (onInfoChanged) onInfoChanged(); }
};

std::unique_ptr<DocViewer> createViewer (Kind k);

// Náhľady do mriežky (volajú sa z vlákien na pozadí)
Image fontThumb (const File& f, int side);
Image pdfThumb (const File& f, int side);
Image modelThumb (const File& f, int side);
Image cadThumb (const File& f, int side);
}
