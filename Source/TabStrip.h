#pragma once
#include "Common.h"

namespace kp
{
// Karty s priečinkami nad zoznamom súborov (ako v prehliadači webu)
class TabStrip : public Component, public SettableTooltipClient
{
public:
    TabStrip() { setRepaintsOnMouseActivity (true); }
    void setTabs (const Array<File>& t, int cur) { tabs = t; current = cur; repaint(); }
    const Array<File>& getTabs() const { return tabs; }
    int getCurrent() const { return current; }

    std::function<void (int)> onSelect, onClose;
    std::function<void()> onAdd;

    void paint (Graphics& g) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDoubleClick (const MouseEvent& e) override { if (tabAt (e.position) < 0 && onAdd) onAdd(); }
    void mouseMove (const MouseEvent& e) override;

private:
    Rectangle<float> tabBounds (int i) const;
    Rectangle<float> closeBounds (int i) const { auto r = tabBounds (i); return r.removeFromRight (22).withSizeKeepingCentre (14, 14); }
    Rectangle<float> addBounds() const;
    int tabAt (Point<float> p) const;
    float tabWidth() const;
    Array<File> tabs;
    int current = 0;
};
}
