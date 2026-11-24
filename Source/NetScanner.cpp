#include "NetScanner.h"
#include "NetWin.h"
#include "Ftp.h"

namespace kp
{
namespace
{
    bool parseIp (const String& s, uint32& out)
    {
        StringArray p;
        p.addTokens (s.trim(), ".", {});
        if (p.size() != 4) return false;
        uint32 v = 0;
        for (auto& x : p)
        {
            if (! x.containsOnly ("0123456789") || x.isEmpty()) return false;
            const int n = x.getIntValue();
            if (n < 0 || n > 255) return false;
            v = (v << 8) | (uint32) n;
        }
        out = v;
        return true;
    }

    String ipText (uint32 ip)
    {
        return String ((ip >> 24) & 255) + "." + String ((ip >> 16) & 255) + "." + String ((ip >> 8) & 255) + "." + String (ip & 255);
    }

    // „192.168.0.1-254“, „192.168.0.1-192.168.0.50“, viac oddelených čiarkou
    std::vector<uint32> parseRange (const String& text)
    {
        std::vector<uint32> v;
        StringArray tok;
        tok.addTokens (text, ",; ", {});
        tok.removeEmptyStrings();
        for (auto& t : tok)
        {
            uint32 a = 0, b = 0;
            if (t.containsChar ('-'))
            {
                const String l = t.upToFirstOccurrenceOf ("-", false, false), r = t.fromFirstOccurrenceOf ("-", false, false);
                if (! parseIp (l, a)) continue;
                if (r.containsChar ('.')) { if (! parseIp (r, b)) continue; }
                else b = (a & 0xffffff00u) | (uint32) jlimit (0, 255, r.getIntValue());
            }
            else if (parseIp (t, a)) b = a;
            else continue;
            if (b < a) std::swap (a, b);
            for (uint32 x = a; x <= b && v.size() < 4096; ++x) v.push_back (x);
        }
        return v;
    }

    const int kPorts[] = { 80, 443, 8080, 21, 22, 3389, 445, 554, 9100 };
}

// ================================================================ NetEngine

NetEngine::NetEngine (PropertiesFile& s) : settings (s)
{
    netwin::init();
    loadOui();
    loadNames();
}

NetEngine::~NetEngine()
{
    *alive = false;
    stopTimer();
    if (stopFlag) *stopFlag = true;
    if (pool) pool->removeAllJobs (true, 5000);
}

String NetEngine::range() const
{
    String def = settings.getValue ("netRange");
    if (def.isNotEmpty()) return def;
    for (auto& a : netwin::localIPv4())
    {
        const String ip (a);
        if (ip.startsWith ("192.168.") || ip.startsWith ("10.") || ip.startsWith ("172."))
            return ip.upToLastOccurrenceOf (".", true, false) + "1-254";
    }
    return "192.168.0.1-254";
}

void NetEngine::setRange (const String& r) { settings.setValue ("netRange", r.trim()); }

void NetEngine::start (const String& r)
{
    if (pool != nullptr) return;
    const auto ips = parseRange (r);
    if (ips.empty()) return;
    setRange (r);
    hosts.clear();
    numNew = 0;
    total = (int) ips.size();
    done = 0;
    stopFlag = std::make_shared<std::atomic<bool>> (false);
    pool = std::make_unique<ThreadPool> (ThreadPoolOptions{}.withThreadName ("Sken").withNumberOfThreads (48));
    std::weak_ptr<bool> life = alive;
    for (auto ip : ips)
    {
        auto stop = stopFlag;
        pool->addJob ([this, ip, stop, life]
        {
            if (*stop) { ++done; return; }
            NetHost h;
            h.ip = ip;
            h.ipText = ipText (ip);
            const bool up = netwin::ping (ip, 700);
            std::string m;
            const bool hasMac = ! *stop && netwin::mac (ip, m);
            if (up || hasMac)
            {
                h.mac = String (m);
                h.name = String::fromUTF8 (netwin::hostName (ip).c_str());
                for (int port : kPorts)
                {
                    if (*stop) break;
                    StreamingSocket s;
                    if (s.connect (h.ipText, port, 350)) h.ports.add (port);
                }
                if (h.ports.contains (445))
                    for (auto& sh : netwin::shares (ip)) h.shares.add (String::fromUTF8 (sh.c_str()));
                MessageManager::callAsync ([this, h, life] { auto l = life.lock(); if (l != nullptr && *l) addHost (h); });
            }
            ++done;
        });
    }
    startTimer (300);
    if (onChange) onChange();
}

void NetEngine::stop()
{
    if (stopFlag) *stopFlag = true;
    if (pool != nullptr) pool->removeAllJobs (false, 0);
}

void NetEngine::timerCallback()
{
    if (pool == nullptr) { stopTimer(); return; }
    if (done.load() >= total || (stopFlag && *stopFlag && pool->getNumJobs() == 0)) finish();
    else if (onChange) onChange();
}

void NetEngine::finish()
{
    pool.reset();
    stopTimer();
    StringArray known = StringArray::fromLines (settings.getValue ("netKnown"));
    known.removeEmptyStrings();
    const bool first = known.isEmpty();
    numNew = 0;
    for (auto& h : hosts)
    {
        if (h.mac.isEmpty() || known.contains (h.mac)) continue;
        if (! first) { h.isNew = true; ++numNew; }
        known.add (h.mac);
    }
    settings.setValue ("netKnown", known.joinIntoString ("\n"));
    if (onChange) onChange();
}

void NetEngine::addHost (NetHost h)
{
    h.vendor = vendorOf (h.mac);
    hosts.push_back (h);
    if (onChange) onChange();
}

String NetEngine::serviceName (int port)
{
    switch (port)
    {
        case 80:   return tr ("Web");
        case 443:  return "HTTPS";
        case 8080: return tr ("Web 8080");
        case 21:   return "FTP";
        case 22:   return "SSH";
        case 3389: return tr ("Vzdialená plocha");
        case 445:  return tr ("Zdieľanie");
        case 554:  return tr ("Kamera (RTSP)");
        case 9100: return tr ("Tlačiareň");
        default:   return String (port);
    }
}

String NetEngine::servicesText (const NetHost& h) const
{
    StringArray s;
    for (int p : h.ports) s.add (serviceName (p));
    return s.joinIntoString (", ");
}

String NetEngine::displayName (const NetHost& h) const
{
    auto it = names.find (h.key());
    return it != names.end() ? it->second : h.name;
}

void NetEngine::setName (const NetHost& h, const String& n)
{
    if (n.trim().isEmpty()) names.erase (h.key()); else names[h.key()] = n.trim();
    XmlElement x ("names");
    for (auto& [k, v] : names)
    {
        auto* e = x.createNewChildElement ("n");
        e->setAttribute ("k", k);
        e->setAttribute ("name", v);
    }
    settings.setValue ("netNames", &x);
    if (onChange) onChange();
}

void NetEngine::loadNames()
{
    names.clear();
    if (auto x = settings.getXmlValue ("netNames"))
        for (auto* e : x->getChildWithTagNameIterator ("n"))
            names[e->getStringAttribute ("k")] = e->getStringAttribute ("name");
}

String NetEngine::defaultCamPath (const String& v)
{
    const auto x = v.toLowerCase();
    if (x.contains ("hikvision")) return "/Streaming/Channels/102";
    if (x.contains ("dahua")) return "/cam/realmonitor?channel=1&subtype=1";
    if (x.contains ("uniview")) return "/media/video2";
    if (x.contains ("reolink")) return "/h264Preview_01_sub";
    if (x.contains ("axis")) return "/axis-media/media.amp";
    if (x.contains ("tp-link") || x.contains ("tplink")) return "/stream2";
    if (x.contains ("ezviz")) return "/h264/ch1/sub/av_stream";
    return "/";
}

// zoznam výrobcov (z Wiresharku), stiahne sa raz a uloží vedľa nastavení
void NetEngine::loadOui()
{
    const auto f = settings.getFile().getSiblingFile ("vyrobcovia-mac.txt");
    auto parse = [] (const File& file, std::map<String, String>& out)
    {
        for (auto& line : StringArray::fromLines (file.loadFileAsString()))
        {
            if (line.startsWithChar ('#') || line.length() < 10 || line[8] != '\t') continue;
            StringArray parts;
            parts.addTokens (line, "\t", {});
            out[line.substring (0, 8).toUpperCase()] = parts.size() > 2 && parts[2].trim().isNotEmpty() ? parts[2].trim() : parts[1].trim();
        }
    };
    if (f.existsAsFile() && f.getSize() > 100000) { parse (f, oui); return; }
    ouiLoading = true;
    std::weak_ptr<bool> life = alive;
    Thread::launch ([this, f, parse, life]
    {
        std::map<String, String> m;
        auto in = URL ("https://www.wireshark.org/download/automated/data/manuf")
                      .createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress).withConnectionTimeoutMs (15000));
        if (in != nullptr)
        {
            MemoryBlock mb;
            in->readIntoMemoryBlock (mb);
            if (mb.getSize() > 100000) { f.replaceWithData (mb.getData(), mb.getSize()); parse (f, m); }
        }
        MessageManager::callAsync ([this, m, life]
        {
            auto l = life.lock();
            if (l == nullptr || ! *l) return;
            ouiLoading = false;
            oui = m;
            for (auto& h : hosts) h.vendor = vendorOf (h.mac);
            if (onChange) onChange();
        });
    });
}

String NetEngine::vendorOf (const String& mac) const
{
    if (mac.length() < 8) return {};
    auto it = oui.find (mac.substring (0, 8).toUpperCase());
    return it != oui.end() ? it->second : String();
}

// ================================================================ NetDetail

NetDetail::NetDetail (NetEngine& e) : net (e)
{
    for (auto* ed : { &camUser, &camPass, &camPath })
    {
        ed->setJustification (Justification::centredLeft);
        ed->setIndents (6, 0);
        addChildComponent (ed);
    }
    camUser.setTextToShowWhenEmpty (tr ("Meno"), col::textDim);
    camPass.setTextToShowWhenEmpty (tr ("Heslo"), col::textDim);
    camPass.setPasswordCharacter ((juce_wchar) 0x2022);
    camPath.setTextToShowWhenEmpty (tr ("Cesta, napr. /Streaming/Channels/102"), col::textDim);
    camB.setButtonText (tr ("Zobraziť kameru"));
    camB.onClick = [this] { if (camOn) stopCamera(); else startCamera(); };
    addChildComponent (camB);
    nameB.setButtonText (tr ("Pomenovať…"));
    nameB.onClick = [this] { renameHost(); };
    addChildComponent (nameB);
}

NetDetail::~NetDetail() { stopCamera(); }

void NetDetail::setHost (int index)
{
    if (index == host && index >= 0) { repaint(); return; }
    stopCamera();
    host = index;
    rebuild();
}

void NetDetail::rebuild()
{
    actions.clear();
    const auto* h = cur();
    nameB.setVisible (h != nullptr);
    bool cam = false;
    if (h != nullptr)
    {
        auto add = [this] (int id, const String& text)
        {
            auto* b = actions.add (new TextButton (text));
            b->onClick = [this, id] { runAction (id); };
            addAndMakeVisible (b);
        };
        if (h->ports.contains (80)) add (1, tr ("Otvoriť web"));
        if (h->ports.contains (443)) add (2, "HTTPS");
        if (h->ports.contains (8080)) add (3, tr ("Web 8080"));
        if (h->ports.contains (3389)) add (4, tr ("Vzdialená plocha"));
        if (h->ports.contains (21)) add (5, "FTP");
        for (int i = 0; i < h->shares.size(); ++i)
            if (! h->shares[i].startsWithChar ('*')) add (100 + i, "\\\\" + h->shares[i]);
        if (h->ports.contains (445)) add (7, tr ("Prieskumník"));
        add (8, tr ("Kopírovať IP"));
        cam = h->ports.contains (554) && videoAvailable;
        if (cam)
        {
            const auto parts = StringArray::fromLines (ftpstore::unprotectText (net.settings.getValue ("cam_" + camKey())));
            camUser.setText (parts[0], false);
            camPass.setText (parts[1], false);
            camPath.setText (parts.size() > 2 && parts[2].isNotEmpty() ? parts[2] : NetEngine::defaultCamPath (h->vendor), false);
        }
    }
    for (auto* c : { (Component*) &camUser, (Component*) &camPass, (Component*) &camPath, (Component*) &camB }) c->setVisible (cam);
    resized();
    repaint();
}

void NetDetail::paint (Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::line);
    g.drawRect (getLocalBounds(), 1);
    const auto* h = cur();
    if (h == nullptr)
    {
        g.setColour (col::text);
        g.setFont (FontOptions (14.0f));
        g.drawText (net.isScanning() ? tr ("Prehľadávam sieť…") : tr ("Vyber zariadenie v zozname"), getLocalBounds(), Justification::centred, true);
        return;
    }
    auto r = getLocalBounds().reduced (14, 10);
    g.setColour (col::text);
    g.setFont (FontOptions (17.0f, Font::bold));
    const String dn = net.displayName (*h);
    g.drawText (dn.isNotEmpty() ? dn : h->ipText, r.removeFromTop (26), Justification::centredLeft, true);
    g.setFont (FontOptions (13.5f));
    auto line = [&] (const String& a, const String& b)
    {
        auto row = r.removeFromTop (20);
        g.setColour (col::text.withAlpha (0.7f));
        g.drawText (a, row.removeFromLeft (100), Justification::centredLeft, true);
        g.setColour (col::text);
        g.drawText (b, row, Justification::centredLeft, true);
    };
    line ("IP", h->ipText);
    line (tr ("Názov v sieti"), h->name.isNotEmpty() ? h->name : String ("-"));
    line ("MAC", h->mac.isNotEmpty() ? h->mac : String ("-"));
    line (tr ("Výrobca"), h->vendor.isNotEmpty() ? h->vendor : String ("-"));
    line (tr ("Služby"), net.servicesText (*h).isNotEmpty() ? net.servicesText (*h) : String ("-"));
    if (h->ports.contains (554) && ! videoAvailable)
    {
        r.removeFromTop (8);
        g.setColour (col::gold);
        g.drawText (tr ("Na obraz z kamery treba prehrávač videa: ☰ → Stiahnuť prehrávač videa"), r.removeFromTop (20), Justification::centredLeft, true);
    }
}

void NetDetail::resized()
{
    auto d = getLocalBounds().reduced (14, 10);
    d.removeFromTop (26 + 5 * 20 + 8);
    if (const auto* h = cur(); h != nullptr && h->ports.contains (554) && ! videoAvailable) d.removeFromTop (28);
    int x = d.getX(), y = d.getY();
    auto place = [&] (Component& b, int w)
    {
        w = jmin (d.getWidth(), w);
        if (x + w > d.getRight()) { x = d.getX(); y += 34; }
        b.setBounds (x, y, w, 28);
        x += w + 6;
    };
    for (auto* b : actions) place (*b, b->getBestWidthForHeight (28) + 16);
    if (nameB.isVisible()) place (nameB, nameB.getBestWidthForHeight (28) + 16);
    y += 40;
    if (camB.isVisible())
    {
        auto row = Rectangle<int> (d.getX(), y, d.getWidth(), 28);
        camUser.setBounds (row.removeFromLeft (row.getWidth() / 4).withTrimmedRight (6));
        camPass.setBounds (row.removeFromLeft (row.getWidth() / 3).withTrimmedRight (6));
        camB.setBounds (row.removeFromRight (140));
        camPath.setBounds (row.withTrimmedRight (6));
        y += 36;
        if (video != nullptr) video->setBounds (d.getX(), y, d.getWidth(), jmax (60, d.getBottom() - y));
    }
}

void NetDetail::runAction (int r)
{
    const auto* hp = cur();
    if (hp == nullptr) return;
    const NetHost h = *hp;
    if (r == 1) URL ("http://" + h.ipText).launchInDefaultBrowser();
    if (r == 2) URL ("https://" + h.ipText).launchInDefaultBrowser();
    if (r == 3) URL ("http://" + h.ipText + ":8080").launchInDefaultBrowser();
   #if JUCE_WINDOWS
    if (r == 4) File::getSpecialLocation (File::windowsSystemDirectory).getChildFile ("mstsc.exe").startAsProcess ("/v:" + h.ipText);
    if (r == 7) File::getSpecialLocation (File::windowsSystemDirectory).getParentDirectory().getChildFile ("explorer.exe").startAsProcess ("\\\\" + h.ipText);
   #endif
    if (r == 5 && openFtp) openFtp (h.ipText);
    if (r == 6) SystemClipboard::copyTextToClipboard ("rtsp://" + h.ipText + ":554/");
    if (r == 8) SystemClipboard::copyTextToClipboard (h.ipText);
    if (r == 9) SystemClipboard::copyTextToClipboard (h.mac);
    if (r >= 100 && r - 100 < h.shares.size() && openFolder) openFolder (File ("\\\\" + h.ipText + "\\" + h.shares[r - 100]));
}

String NetDetail::camKey() const
{
    const auto* h = cur();
    return h != nullptr ? h->key().replaceCharacters (":. ", "___") : String();
}

void NetDetail::startCamera()
{
    const auto* h = cur();
    if (h == nullptr || ! makeVideo) return;
    net.settings.setValue ("cam_" + camKey(), ftpstore::protectText (camUser.getText() + "\n" + camPass.getText() + "\n" + camPath.getText()));
    String auth;
    if (camUser.getText().isNotEmpty())
        auth = URL::addEscapeChars (camUser.getText(), true) + ":" + URL::addEscapeChars (camPass.getText(), true) + "@";
    String path = camPath.getText().trim();
    if (! path.startsWithChar ('/')) path = "/" + path;
    if (video == nullptr) { video.reset (makeVideo()); if (video != nullptr) addAndMakeVisible (*video); }
    if (video == nullptr) return;
    video->setVisible (true);
    resized();
    if (playUrl) playUrl (video.get(), "rtsp://" + auth + h->ipText + ":554" + path);
    camOn = true;
    camB.setButtonText (tr ("Zastaviť"));
}

void NetDetail::stopCamera()
{
    if (video != nullptr)
    {
        if (stopVideo) stopVideo (video.get());
        video->setVisible (false);
    }
    camOn = false;
    camB.setButtonText (tr ("Zobraziť kameru"));
}

void NetDetail::renameHost()
{
    const auto* h = cur();
    if (h == nullptr || ask != nullptr) return;
    const NetHost copy = *h;
    ask = std::make_unique<AlertWindow> (tr ("Pomenovať zariadenie"), copy.ipText, MessageBoxIconType::NoIcon, this);
    ask->addTextEditor ("n", net.displayName (copy), tr ("Názov (napr. Kamera vchod):"));
    ask->addButton (tr ("Uložiť"), 1, KeyPress (KeyPress::returnKey));
    ask->addButton (tr ("Zrušiť"), 0, KeyPress (KeyPress::escapeKey));
    addAndMakeVisible (*ask);
    ask->setCentrePosition (getLocalBounds().getCentre());
    Component::SafePointer<NetDetail> sp (this);
    ask->enterModalState (true, ModalCallbackFunction::create ([sp, copy] (int r)
    {
        if (sp == nullptr || sp->ask == nullptr) return;
        const String n = sp->ask->getTextEditorContents ("n");
        auto* w = sp->ask.release();
        w->setVisible (false);
        MessageManager::callAsync ([w] { delete w; });
        if (r == 1) sp->net.setName (copy, n);
        sp->repaint();
    }));
}
}
