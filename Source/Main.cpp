#include "Common.h"
#include "MainComponent.h"
#include "VideoStrip.h"
#include "StartLog.h"
#include "ImageLoader.h"
#include "Archive.h"
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
    std::unique_ptr<VideoStrip> stripTest;
    std::unique_ptr<TimedCallback> stripTimer;
    int stripTicks = 0;
public:
    const String getApplicationName() override { return "Korgus Player"; }
    const String getApplicationVersion() override { return KP_VERSION; }
    // jeden Player: dalsi otvoreny subor (dvojklik v Prieskumnikovi) sa ukaze v uz spustenom okne
    bool moreThanOneInstanceAllowed() override
    {
        const auto c = getCommandLineParameters();
        return c.contains ("--selftest") || c.startsWith ("--decode") || c.contains ("--updated") || c.startsWith ("--strip") || c.startsWith ("--cut") || c.startsWith ("--vconv") || c.startsWith ("--test");
    }
    void anotherInstanceStarted (const String& cmd) override
    {
        if (window == nullptr) return;
        const auto arg = cmd.unquoted().trim();
        if (arg.isNotEmpty() && File::isAbsolutePath (arg))
            if (auto* mc = dynamic_cast<MainComponent*> (window->getContentComponent()))
                mc->openPath (File (arg));
        if (window->isMinimised()) window->setMinimised (false);
        window->toFront (true);
    }

    void initialise (const String& commandLine) override
    {
        // kontrola, či Windows program pustí (rovnako ako v Sequenceri)
        if (commandLine.contains ("--selftest")) { quit(); return; }
        if (commandLine.contains ("--updated")) Thread::sleep (1500);   // po aktualizacii pockat, kym sa stary Player zavrie

        // test filmového pásu: --strip <video> <výstup.png> [od] [do]
        if (commandLine.startsWith ("--strip"))
        {
            auto args = StringArray::fromTokens (commandLine, true);
            const File in (args[1].unquoted()), out (args[2].unquoted());
            const double d = probeVideoDuration (in);
            std::cout << "dlzka: " << d << " s" << std::endl;
            stripTest = std::make_unique<VideoStrip>();
            stripTest->setSize (900, VideoStrip::stripH);
            stripTest->setFile (in);
            stripTest->setDuration (d);
            if (args.size() > 4) stripTest->setViewForTest (args[3].getDoubleValue(), args[4].getDoubleValue());
            stripTimer = std::make_unique<TimedCallback> ([this, out]
            {
                if (stripTest->readyThumbs() < 8 && ++stripTicks < 100) return;
                std::cout << "nahlady: " << stripTest->readyThumbs() << std::endl;
                auto img = stripTest->createComponentSnapshot (stripTest->getLocalBounds());
                FileOutputStream os (out); os.setPosition (0); os.truncate();
                PNGImageFormat().writeImageToStream (img, os);
                stripTimer->stopTimer();
                quit();
            });
            stripTimer->startTimer (200);
            return;
        }
        // test ulozenia vyrezu videa: --cut <video> <od> <do>
        if (commandLine.startsWith ("--cut"))
        {
            auto args = StringArray::fromTokens (commandLine, true);
            String msg;
            const auto out = videocut::run (File (args[1].unquoted()), args[2].getDoubleValue(), args[3].getDoubleValue(),
                                            [] (float p) { std::cout << roundToInt (p * 100) << "% " << std::flush; }, msg);
            std::cout << std::endl << msg << " -> " << out.getFullPathName() << std::endl;
            quit();
            return;
        }

        // testy pri zostavení (CI): --test → náhľady, ZIP, premenovanie s diakritikou
        if (commandLine.startsWith ("--test"))
        {
            int fails = 0;
            auto check = [&fails] (const String& name, bool ok, const String& info = {})
            {
                std::cout << (ok ? "OK    " : "CHYBA ") << name.toRawUTF8() << (info.isNotEmpty() ? "  (" + info + ")" : String()).toRawUTF8() << std::endl;
                if (! ok) ++fails;
            };
            const auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("korgus-test");
            dir.deleteRecursively();
            dir.createDirectory();
            // obrázky s diakritikou v názve
            Image im (Image::RGB, 640, 480, true);
            { Graphics g (im); g.fillAll (Colours::darkslategrey); g.setColour (Colours::orange); g.fillEllipse (100, 80, 300, 300); }
            const auto png = dir.getChildFile (String::fromUTF8 ("Žltý kôň.png")), jpg = dir.getChildFile (String::fromUTF8 ("fotka čľšť.jpg"));
            { FileOutputStream o (png); PNGImageFormat().writeImageToStream (im, o); }
            { FileOutputStream o (jpg); JPEGImageFormat().writeImageToStream (im, o); }
            for (auto& f : { png, jpg })
            {
                const auto t = img::loadThumb (f, 160);
                check ("náhľad " + f.getFileName(), t.isValid() && jmax (t.getWidth(), t.getHeight()) <= 160 && t.getWidth() > 0, String (t.getWidth()) + "x" + String (t.getHeight()));
                const auto full = img::loadFull (f, 4000);
                check ("načítanie " + f.getFileName(), full.image.isValid() && full.origW == 640 && full.origH == 480, String (full.origW) + "x" + String (full.origH));
            }
            // ZIP: zbaliť, prečítať zoznam, rozbaliť a porovnať
            dir.getChildFile (String::fromUTF8 ("podpriečinok")).createDirectory();
            dir.getChildFile (String::fromUTF8 ("podpriečinok/text ä.txt")).replaceWithText (String::fromUTF8 ("Dobrý deň"));
            const auto zip = dir.getSiblingFile ("korgus-test.zip");
            zip.deleteFile();
            String err;
            Array<File> items { png, jpg, dir.getChildFile (String::fromUTF8 ("podpriečinok")) };
            check ("ZIP zbaliť", archive::createZip (items, dir, zip, nullptr, err) && zip.getSize() > 0, err);
            std::vector<ArcItem> list;
            check ("ZIP zoznam", archive::list (zip, list, err) && list.size() >= 3, String ((int) list.size()) + " položiek " + err);
            const auto out = dir.getSiblingFile ("korgus-test-out");
            out.deleteRecursively();
            check ("ZIP rozbaliť", archive::extractAll (zip, out, nullptr, err), err);
            check ("ZIP obsah", out.getChildFile (String::fromUTF8 ("podpriečinok/text ä.txt")).loadFileAsString() == String::fromUTF8 ("Dobrý deň")
                                && out.getChildFile (png.getFileName()).getSize() == png.getSize());
            // premenovanie s diakritikou a späť
            const auto renamed = dir.getChildFile (String::fromUTF8 ("Ďaľší názov – ťažký.png"));
            check ("premenovať", png.moveFileTo (renamed) && renamed.existsAsFile() && ! png.exists());
            check ("premenovať späť", renamed.moveFileTo (png) && png.existsAsFile());
            dir.deleteRecursively(); out.deleteRecursively(); zip.deleteFile();
            std::cout << (fails == 0 ? "VSETKO OK" : "CHYBY: " + String (fails)) << std::endl;
            setApplicationReturnValue (fails == 0 ? 0 : 1);
            quit();
            return;
        }

        // test prevodu videa: --vconv <vstup> <výstup> <formát> <výška> <kvalita>
        if (commandLine.startsWith ("--vconv"))
        {
            auto args = StringArray::fromTokens (commandLine, true);
            videoconv::Options o;
            o.format = args[3].getIntValue(); o.height = args[4].getIntValue(); o.quality = args[5].getIntValue();
            String msg;
            const File out (args[2].unquoted());
            const bool ok = videoconv::run (args[1].unquoted(), out, o, [] (float p) { std::cout << roundToInt (p * 100) << "% " << std::flush; }, nullptr, msg);
            std::cout << std::endl << (ok ? "OK " : "CHYBA ") << msg << " -> " << out.getFullPathName() << " " << out.getSize() << std::endl;
            quit();
            return;
        }

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

        startlog::mark (commandLine.contains ("--updated") ? "start programu (po aktualizácii, s čakaním 1,5 s)" : "start programu");
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
        startlog::mark ("nastavenia a vzhľad");
        window = std::make_unique<Window> (commandLine);
        startlog::mark ("okno hotové");
        restartUI = [this]
        {
            MessageManager::callAsync ([this]
            {
                if (window != nullptr)
                    if (auto* mc = dynamic_cast<MainComponent*> (window->getContentComponent()))
                        mc->prepareToQuit();   // zvuk a video stíšiť skôr, než sa okno zruší
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
            startlog::mark ("hlavná časť vytvorená");
            setContentOwned (mc, true);
            setResizable (true, false);
            setResizeLimits (800, 500, 10000, 10000);

            const auto state = mc->settings().getValue ("window");
            if (state.isEmpty() || ! restoreWindowStateFromString (state))
                centreWithSize (1400, 860);
            setVisible (true);
            startlog::mark ("okno zobrazené");
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
