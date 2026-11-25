// Flash (SWF) cez Ruffle (https://ruffle.rs, MIT / Apache 2.0) vo WebView2.
// Ruffle (webova verzia) je pribaleny v programe ako ZIP a podava sa cez vlastny "resource provider",
// takze nic netreba instalovat ani stahovat a SWF sa nikam neposiela.
#include "DocViewers.h"

#if KP_HAS_RUFFLE && JUCE_WEB_BROWSER
 #include "RuffleData.h"
#endif

namespace kp
{
#if KP_HAS_RUFFLE && JUCE_WEB_BROWSER
namespace
{
    std::vector<std::byte> toBytes (const void* d, size_t n)
    {
        std::vector<std::byte> v (n);
        if (n > 0) std::memcpy (v.data(), d, n);
        return v;
    }

    String mimeFor (const String& name)
    {
        if (name.endsWithIgnoreCase (".js")) return "text/javascript";
        if (name.endsWithIgnoreCase (".wasm")) return "application/wasm";
        if (name.endsWithIgnoreCase (".json")) return "application/json";
        return "application/octet-stream";
    }

    const char* pageHtml = R"(<!DOCTYPE html>
<html><head><meta charset="utf-8"><style>html,body{margin:0;height:100%;background:#15191f;overflow:hidden}#c{width:100%;height:100%}</style></head>
<body><div id="c"></div>
<script>window.RufflePlayer=window.RufflePlayer||{};window.RufflePlayer.config={publicPath:"/ruffle/",autoplay:"on",unmuteOverlay:"hidden",splashScreen:false,letterbox:"on",openUrlMode:"deny",allowNetworking:"none",showSwfDownload:false};</script>
<script src="/ruffle/ruffle.js"></script>
<script>
  const r = window.RufflePlayer.newest();
  const p = r.createPlayer();
  p.style.width = "100%"; p.style.height = "100%";
  document.getElementById("c").appendChild(p);
  const api = p.ruffle ? p.ruffle() : p;
  api.load({ url: "/movie.swf" });
</script></body></html>)";

    class FlashViewer : public DocViewer
    {
    public:
        FlashViewer()
        {
            const auto data = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Korgus Player").getChildFile ("WebView2");
            auto opts = WebBrowserComponent::Options()
                            .withBackend (WebBrowserComponent::Options::Backend::webview2)
                            .withWinWebView2Options (WebBrowserComponent::Options::WinWebView2().withUserDataFolder (data))
                            .withKeepPageLoadedWhenBrowserIsHidden()
                            .withResourceProvider ([this] (const String& path) { return serve (path); });
            web = std::make_unique<WebBrowserComponent> (opts);
            addAndMakeVisible (*web);
        }

        void load (const File& f) override
        {
            {
                const ScopedLock sl (lock);
                swf.reset();
                f.loadFileAsData (swf);
            }
            info = "Flash (SWF) · " + File::descriptionOfSizeInBytes (f.getSize()) + " · Ruffle";
            // nova adresa = nove nacitanie (Ruffle zacne od zaciatku)
            web->goToURL (WebBrowserComponent::getResourceProviderRoot() + "index.html?" + String (++counter));
            infoChanged();
        }

        String infoText() const override { return info; }
        void resized() override { web->setBounds (getLocalBounds()); }
        void paint (Graphics& g) override { g.fillAll (Colour (0xff15191f)); }

    private:
        std::optional<WebBrowserComponent::Resource> serve (const String& path)
        {
            const auto p = path.upToFirstOccurrenceOf ("?", false, false);
            if (p == "/" || p == "/index.html")
                return WebBrowserComponent::Resource { toBytes (pageHtml, std::strlen (pageHtml)), "text/html" };
            const ScopedLock sl (lock);
            if (p == "/movie.swf")
                return WebBrowserComponent::Resource { toBytes (swf.getData(), swf.getSize()), "application/x-shockwave-flash" };
            if (p.startsWith ("/ruffle/"))
            {
                auto& z = zip();
                const auto name = p.substring (8);
                auto* e = z.getEntry (name);
                for (int i = 0; e == nullptr && i < z.getNumEntries(); ++i)   // pre istotu aj "./nazov" alebo s priecinkom
                    if (z.getEntry (i)->filename.fromLastOccurrenceOf ("/", false, false) == name) e = z.getEntry (i);
                if (e != nullptr)
                    if (std::unique_ptr<InputStream> in (z.createStreamForEntry (*e)); in != nullptr)
                    {
                        MemoryBlock mb;
                        in->readIntoMemoryBlock (mb);
                        return WebBrowserComponent::Resource { toBytes (mb.getData(), mb.getSize()), mimeFor (p) };
                    }
            }
            return std::nullopt;
        }

        static ZipFile& zip()
        {
            static ZipFile z (new MemoryInputStream (RuffleData::ruffleweb_zip, (size_t) RuffleData::ruffleweb_zipSize, false), true);
            return z;
        }

        std::unique_ptr<WebBrowserComponent> web;
        CriticalSection lock;
        MemoryBlock swf;
        String info;
        int counter = 0;
    };
}

std::unique_ptr<DocViewer> createFlashViewer() { return std::make_unique<FlashViewer>(); }

#else
namespace
{
    class NoFlashViewer : public DocViewer
    {
    public:
        void load (const File& f) override { info = "Flash (SWF) · " + File::descriptionOfSizeInBytes (f.getSize()); infoChanged(); repaint(); }
        String infoText() const override { return info; }
        void paint (Graphics& g) override
        {
            g.fillAll (col::bg);
            g.setColour (col::textDim);
            g.setFont (FontOptions (14.0f));
            g.drawText (tr ("Flash (SWF) sa v tejto verzii nedá prehrať"), getLocalBounds(), Justification::centred);
        }
    private:
        String info;
    };
}

std::unique_ptr<DocViewer> createFlashViewer() { return std::make_unique<NoFlashViewer>(); }
#endif
}
