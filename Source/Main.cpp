#include "Common.h"
#include "MainComponent.h"
#include "ExtraFormats.h"

namespace kp
{
class KorgusLook : public LookAndFeel_V4
{
public:
    KorgusLook()
    {
        setColourScheme ({ col::bg, col::panel, col::panel, col::line, col::text,
                           col::accent, col::selText, col::accentHi, col::text });
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

        // test dekódovania: --decode <súbor> <výstup.f32>  (float32 prekladané kanály)
        if (commandLine.startsWith ("--decode"))
        {
            auto args = StringArray::fromTokens (commandLine, true);
            AudioFormatManager fm;
            registerAllFormats (fm);
            const File in (args[1].unquoted()), out (args[2].unquoted());
            std::unique_ptr<AudioFormatReader> byExt (fm.createReaderFor (in));
            std::unique_ptr<AudioFormatReader> byStream (fm.createReaderFor (in.createInputStream()));
            std::cout << "podla pripony: " << (byExt ? byExt->getFormatName() : String ("-"))
                      << " | podla obsahu: " << (byStream ? byStream->getFormatName() : String ("-")) << std::endl;
            if (byExt)
            {
                std::cout << byExt->sampleRate << " Hz, " << byExt->numChannels << " k, " << byExt->bitsPerSample
                          << " bit, " << byExt->lengthInSamples << " vzoriek" << std::endl;
                AudioBuffer<float> b ((int) byExt->numChannels, (int) byExt->lengthInSamples);
                byExt->read (&b, 0, b.getNumSamples(), 0, true, true);
                FileOutputStream os (out);
                os.setPosition (0); os.truncate();
                for (int i = 0; i < b.getNumSamples(); ++i)
                    for (int c = 0; c < b.getNumChannels(); ++c) os.writeFloat (b.getSample (c, i));
            }
            quit();
            return;
        }

        {
            PropertiesFile::Options o;
            o.applicationName = "Korgus Player";
            o.filenameSuffix = ".settings";
            o.folderName = "Korgus Player";
            o.osxLibrarySubFolder = "Application Support";
            PropertiesFile pf (o);
            col::applyTheme (pf.getBoolValue ("lightTheme", false));
            // doterajší používatelia ostanú v slovenčine, noví podľa jazyka Windows
            const auto sys = SystemStats::getUserLanguage();
            const bool skDefault = pf.containsKey ("lastFolder") || sys.startsWithIgnoreCase ("sk") || sys.startsWithIgnoreCase ("cs");
            english = pf.getValue ("lang", skDefault ? "sk" : "en") == "en";
        }
        look = std::make_unique<KorgusLook>();
        LookAndFeel::setDefaultLookAndFeel (look.get());
        window = std::make_unique<Window> (commandLine);
        restartUI = [this]
        {
            MessageManager::callAsync ([this]
            {
                window = nullptr;   // uloží nastavenia
                PropertiesFile::Options o;
                o.applicationName = "Korgus Player";
                o.filenameSuffix = ".settings";
                o.folderName = "Korgus Player";
                o.osxLibrarySubFolder = "Application Support";
                PropertiesFile pf (o);
                col::applyTheme (pf.getBoolValue ("lightTheme", false));
                english = pf.getValue ("lang", english ? "en" : "sk") == "en";
                auto newLook = std::make_unique<KorgusLook>();
                LookAndFeel::setDefaultLookAndFeel (newLook.get());
                look = std::move (newLook);
                window = std::make_unique<Window> (String());
            });
        };
    }

    void shutdown() override
    {
        restartUI = nullptr;
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

        ~Window() override { saveState(); }

        void saveState()
        {
            if (stateSaved) return;
            stateSaved = true;
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

        // okno zmizne hneď; upratovanie (vlákna náhľadov, zvuk, video) prebehne už bez neho
        void closeButtonPressed() override
        {
            saveState();
            if (auto* mc = dynamic_cast<MainComponent*> (getContentComponent()))
                mc->prepareToQuit();
            setVisible (false);
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        bool stateSaved = false;
    };

    std::unique_ptr<KorgusLook> look;
    std::unique_ptr<Window> window;
};
}

START_JUCE_APPLICATION (kp::App)
