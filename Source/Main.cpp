#include "Common.h"
#include "MainComponent.h"

namespace kp
{
class KorgusLook : public LookAndFeel_V4
{
public:
    KorgusLook()
    {
        setColourScheme ({ col::bg, col::panel, col::panel, col::line, col::text,
                           col::accent, Colours::white, col::accentHi, col::text });
        setColour (TextButton::buttonColourId, col::panelHi);
        setColour (TextButton::textColourOffId, col::text);
        setColour (ScrollBar::thumbColourId, col::line.brighter (0.2f));
        setColour (TooltipWindow::backgroundColourId, col::panelHi);
        setColour (TooltipWindow::textColourId, col::text);
        setColour (TooltipWindow::outlineColourId, col::line);
    }

    void drawButtonBackground (Graphics& g, Button& b, const Colour& bg, bool over, bool down) override
    {
        auto c = bg;
        if (down) c = col::accent;
        else if (over) c = col::accentHi;
        g.setColour (c);
        g.fillRoundedRectangle (b.getLocalBounds().toFloat(), radius);
    }

    void drawStretchableLayoutResizerBar (Graphics& g, int w, int h, bool, bool isMouseOver, bool isMouseDragging) override
    {
        g.fillAll (col::bg);
        g.setColour (isMouseOver || isMouseDragging ? col::accentHi : col::line);
        g.fillRect (w / 2, 0, 1, h);
    }
};

class App : public JUCEApplication
{
public:
    const String getApplicationName() override { return "Korgus Player"; }
    const String getApplicationVersion() override { return KP_VERSION; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const String& commandLine) override
    {
        // kontrola, či Windows program pustí (rovnako ako v Sequenceri)
        if (commandLine.contains ("--selftest")) { quit(); return; }

        LookAndFeel::setDefaultLookAndFeel (&look);
        window = std::make_unique<Window> (commandLine);
    }

    void shutdown() override
    {
        window = nullptr;
        LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    class Window : public DocumentWindow
    {
    public:
        explicit Window (const String& cmd)
            : DocumentWindow ("Korgus Player " + String (KP_VERSION), col::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* mc = new MainComponent (cmd);
            setContentOwned (mc, true);
            setResizable (true, false);
            setResizeLimits (800, 500, 10000, 10000);

            const auto state = mc->settings().getValue ("window");
            if (state.isEmpty() || ! restoreWindowStateFromString (state))
                centreWithSize (1400, 860);
            setVisible (true);
            mc->focusGrid();
        }

        ~Window() override
        {
            if (auto* mc = dynamic_cast<MainComponent*> (getContentComponent()))
                mc->settings().setValue ("window", getWindowStateAsString());
        }

        // po aktivácii okna musí mať klávesnicu mriežka, aby šípky fungovali hneď
        void activeWindowStatusChanged() override
        {
            DocumentWindow::activeWindowStatusChanged();
            if (isActiveWindow())
                if (auto* mc = dynamic_cast<MainComponent*> (getContentComponent()))
                    if (! mc->hasKeyboardFocus (true))
                        mc->focusGrid();
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    KorgusLook look;
    std::unique_ptr<Window> window;
};
}

START_JUCE_APPLICATION (kp::App)
