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

    // bublina ako vo Windows: svetlý podklad s rámikom, okraj okolo textu, tieň dá systém
    static TextLayout tipLayout (const String& text, Colour c)
    {
        AttributedString a;
        a.setJustification (Justification::centredLeft);
        a.append (text, FontOptions (13.0f), c);
        TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (a, 360.0f);
        return tl;
    }
    // bez zátvoriek na konci („Dva panely (F9)“ → „Dva panely“)
    static String tidyTip (const String& t)
    {
        auto s = t.trim();
        if (s.endsWithChar (')'))
            if (const int i = s.lastIndexOf (" ("); i > 0) s = s.substring (0, i).trimEnd();
        return s;
    }
    static Colour tipBg()     { return col::light ? Colour (0xfff9f9f9) : Colour (0xff2b2b2b); }
    static Colour tipText()   { return col::light ? Colour (0xff1a1a1a) : Colour (0xfff0f0f0); }
    static Colour tipBorder() { return col::light ? Colour (0xffc8c8c8) : Colour (0xff555555); }

    Rectangle<int> getTooltipBounds (const String& tip, Point<int> pos, Rectangle<int> area) override
    {
        const auto tl = tipLayout (tidyTip (tip), tipText());
        const int w = (int) std::ceil (tl.getWidth()) + 18, h = (int) std::ceil (tl.getHeight()) + 10;
        return Rectangle<int> (pos.x > area.getCentreX() ? pos.x - (w + 4) : pos.x + 4,
                               pos.y > area.getCentreY() ? pos.y - (h + 8) : pos.y + 22, w, h).constrainedWithin (area);
    }

    void drawTooltip (Graphics& g, const String& text, int w, int h) override
    {
        g.fillAll (tipBg());
        g.setColour (tipBorder());
        g.drawRect (0, 0, w, h, 1);
        tipLayout (tidyTip (text), tipText()).draw (g, Rectangle<float> (9.0f, 5.0f, (float) w - 18.0f, (float) h - 10.0f));
    }

    // nadpisy v menu (Vzhľad, Jazyk, Zvuk…) obyčajným písmom, nie tučným
    void drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& name) override
    {
        g.setFont (getPopupMenuFont());
        g.setColour (col::textDim);
        g.drawFittedText (name, area.getX() + 12, area.getY() + 2, area.getWidth() - 16, area.getHeight() - 4, Justification::bottomLeft, 1);
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
    // jeden Player: dalsi otvoreny subor (dvojklik v Prieskumnikovi) sa ukaze v uz spustenom okne
    bool moreThanOneInstanceAllowed() override
    {
        const auto c = getCommandLineParameters();
        return c.contains ("--selftest") || c.startsWith ("--decode") || c.contains ("--updated");
    }
    void anotherInstanceStarted (const String& cmd) override
    {
        if (window == nullptr) return;
        const auto arg = cmd.unquoted().trim();
        if (arg.isNotEmpty() && File::isAbsolutePath (arg))
            if (auto* mc = dynamic_cast<MainComponent*> (window->getContentComponent()))
            {
                const File f (arg);
                if (f.existsAsFile()) mc->openFolder (f.getParentDirectory(), f);
                else if (f.isDirectory()) mc->openFolder (f);
            }
        if (window->isMinimised()) window->setMinimised (false);
        window->toFront (true);
    }

    void initialise (const String& commandLine) override
    {
        // kontrola, či Windows program pustí (rovnako ako v Sequenceri)
        if (commandLine.contains ("--selftest")) { quit(); return; }
        if (commandLine.contains ("--updated")) Thread::sleep (1500);   // po aktualizacii pockat, kym sa stary Player zavrie

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
