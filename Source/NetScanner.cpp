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

NetScanner::NetScanner (PropertiesFile& s) : settings (s)
{
    netwin::init();
    String def = settings.getValue ("netRange");
    if (def.isEmpty())
        for (auto& a : netwin::localIPv4())
        {
            const String ip (a);
            if (ip.startsWith ("192.168.") || ip.startsWith ("10.") || ip.startsWith ("172."))
            { def = ip.upToLastOccurrenceOf (".", true, false) + "1-254"; break; }
        }
    if (def.isEmpty()) def = "192.168.0.1-254";
    range.setText (def, false);
    range.setTextToShowWhenEmpty (tr ("Napríklad 192.168.0.1-254, 192.168.0.200"), col::textDim);
    range.onReturnKey = [this] { startScan(); };
    addAndMakeVisible (range);
    filter.setTextToShowWhenEmpty (tr ("Hľadať v zozname"), col::textDim);
    filter.onTextChange = [this] { rebuildView(); };
    addAndMakeVisible (filter);
    scanB.setButtonText (tr ("Prehľadať"));
    scanB.onClick = [this] { if (pool != nullptr) stopScan(); else startScan(); };
    addAndMakeVisible (scanB);

    auto& h = table.getHeader();
    h.addColumn (tr ("Stav"), 1, 46, 40, 60);
    h.addColumn (tr ("Názov"), 2, 170, 80, 600);
    h.addColumn ("IP", 3, 120, 80, 200);
    h.addColumn (tr ("Výrobca"), 4, 170, 80, 600);
    h.addColumn (tr ("Adresa MAC"), 5, 140, 80, 200);
    h.setSortColumnId (3, true);
    h.setColour (TableHeaderComponent::backgroundColourId, col::panel);
    h.setColour (TableHeaderComponent::textColourId, col::text);
    h.setColour (TableHeaderComponent::outlineColourId, col::line);
    h.setColour (TableHeaderComponent::highlightColourId, col::panelHi);
    for (auto* sb : { &table.getVerticalScrollBar(), &table.getHorizontalScrollBar() })
    {
        sb->setColour (ScrollBar::thumbColourId, col::line.brighter (0.35f));
        sb->setColour (ScrollBar::trackColourId, col::docBg);
    }
    for (auto* ed : { &range, &filter })
    {
        ed->setJustification (Justification::centredLeft);
        ed->setIndents (8, 0);
        ed->setFont (FontOptions (14.0f));
    }
    table.setRowHeight (24);
    table.setColour (ListBox::backgroundColourId, col::docBg);
    table.setColour (ListBox::outlineColourId, col::line);
    table.setOutlineThickness (1);
    addAndMakeVisible (table);
    loadOui();
    loadNames();

    // pravá časť
    for (auto* e : { &camUser, &camPass, &camPath })
    {
        e->setJustification (Justification::centredLeft);
        e->setIndents (6, 0);
        addChildComponent (e);
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
    setSize (1200, 680);
}

NetScanner::~NetScanner()
{
    stopCamera();
    stopTimer();
    if (stopFlag) *stopFlag = true;
    if (pool) pool->removeAllJobs (true, 5000);
}

void NetScanner::paint (Graphics& g)
{
    g.fillAll (col::bg);
    auto st = getLocalBounds().removeFromBottom (26);
    g.setColour (col::panel);
    g.fillRect (st);
    g.setColour (col::text);
    g.setFont (FontOptions (13.0f));
    String s = tr ("Nájdené zariadenia: ") + String ((int) hosts.size());
    if (pool != nullptr) s << tr (" · prehľadávam ") << String (done.load()) << " / " << String (total);
    if (ouiLoading) s << tr (" · sťahujem zoznam výrobcov");
    if (newCount > 0) s << tr (" · nové zariadenia: ") << String (newCount);
    g.drawText (s, st.reduced (10, 0), Justification::centredLeft, true);

    // podrobnosti vpravo
    auto d = detailArea();
    g.setColour (col::panel);
    g.fillRect (d);
    g.setColour (col::line);
    g.drawRect (d, 1);
    if (selected < 0 || selected >= (int) hosts.size())
    {
        g.setColour (col::text);
        g.setFont (FontOptions (14.0f));
        g.drawText (tr ("Vyber zariadenie v zozname"), d, Justification::centred, true);
        return;
    }
    const auto& h = hosts[(size_t) selected];
    auto r = d.reduced (14, 10);
    g.setColour (col::text);
    g.setFont (FontOptions (17.0f, Font::bold));
    g.drawText (displayName (h), r.removeFromTop (26), Justification::centredLeft, true);
    g.setFont (FontOptions (13.5f));
    auto line = [&] (const String& a, const String& b)
    {
        auto row = r.removeFromTop (20);
        g.setColour (col::text.withAlpha (0.7f));
        g.drawText (a, row.removeFromLeft (90), Justification::centredLeft, true);
        g.setColour (col::text);
        g.drawText (b, row, Justification::centredLeft, true);
    };
    line ("IP", h.ipText);
    if (h.name.isNotEmpty() && names.count (h.key())) line (tr ("Názov v sieti"), h.name);
    line ("MAC", h.mac.isNotEmpty() ? h.mac : String ("-"));
    line (tr ("Výrobca"), h.vendor.isNotEmpty() ? h.vendor : String ("-"));
    line (tr ("Služby"), servicesText (h).isNotEmpty() ? servicesText (h) : String ("-"));
    if (h.ports.contains (554) && ! videoAvailable)
    {
        r.removeFromTop (8);
        g.setColour (col::gold);
        g.drawText (tr ("Na obraz z kamery treba prehrávač videa: ☰ → Stiahnuť prehrávač videa"), r.removeFromTop (20), Justification::centredLeft, true);
    }
}

Rectangle<int> NetScanner::detailArea() const
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromBottom (20);
    r.removeFromTop (40);
    return r.removeFromRight (jmax (380, r.getWidth() * 45 / 100));
}

void NetScanner::layoutDetails()
{
    auto d = detailArea().reduced (14, 10);
    d.removeFromTop (26 + 5 * 20 + 8);
    if (selected >= 0 && selected < (int) hosts.size() && hosts[(size_t) selected].ports.contains (554) && ! videoAvailable) d.removeFromTop (28);
    // tlačidlá akcií po riadkoch
    int x = d.getX(), y = d.getY();
    for (auto* b : actions)
    {
        const int w = jmin (d.getWidth(), b->getBestWidthForHeight (28) + 16);
        if (x + w > d.getRight()) { x = d.getX(); y += 34; }
        b->setBounds (x, y, w, 28);
        x += w + 6;
    }
    if (nameB.isVisible())
    {
        const int w = nameB.getBestWidthForHeight (28) + 16;
        if (x + w > d.getRight()) { x = d.getX(); y += 34; }
        nameB.setBounds (x, y, w, 28);
    }
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

void NetScanner::resized()
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromBottom (20);
    auto top = r.removeFromTop (30);
    scanB.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (10);
    filter.setBounds (top.removeFromRight (220));
    top.removeFromRight (10);
    range.setBounds (top);
    r.removeFromTop (10);
    r.removeFromRight (detailArea().getWidth() + 10);
    table.setBounds (r);
    layoutDetails();
}

void NetScanner::startScan()
{
    if (pool != nullptr) return;
    const auto ips = parseRange (range.getText());
    if (ips.empty()) { range.grabKeyboardFocus(); return; }
    settings.setValue ("netRange", range.getText().trim());
    stopCamera();
    hosts.clear();
    selected = -1;
    newCount = 0;
    showDetails();
    rebuildView();
    total = (int) ips.size();
    done = 0;
    stopFlag = std::make_shared<std::atomic<bool>> (false);
    pool = std::make_unique<ThreadPool> (ThreadPoolOptions{}.withThreadName ("Sken").withNumberOfThreads (48));
    scanB.setButtonText (tr ("Zastaviť"));
    Component::SafePointer<NetScanner> sp (this);
    for (auto ip : ips)
    {
        auto stop = stopFlag;
        pool->addJob ([sp, ip, stop, this]
        {
            if (*stop) return;
            Host h;
            h.ip = ip;
            h.ipText = ipText (ip);
            const bool alive = netwin::ping (ip, 700);
            std::string m;
            const bool hasMac = ! *stop && netwin::mac (ip, m);
            if (alive || hasMac)
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
                MessageManager::callAsync ([sp, h] { if (sp != nullptr) sp->addHost (h); });
            }
            ++done;
        });
    }
    startTimer (300);
    repaint();
}

void NetScanner::stopScan()
{
    if (stopFlag) *stopFlag = true;
}

void NetScanner::timerCallback()
{
    repaint (getLocalBounds().removeFromBottom (26));
    if (pool != nullptr && done.load() >= total) finishScan();
    else if (pool != nullptr && stopFlag && *stopFlag)
    {
        // zastavené: počkať, kým bežiace úlohy skončia
        if (pool->getNumJobs() == 0 || done.load() >= total) finishScan();
        else { pool->removeAllJobs (false, 0); }
    }
}

void NetScanner::finishScan()
{
    pool.reset();
    scanB.setButtonText (tr ("Prehľadať"));
    stopTimer();
    // nové zariadenia oproti predošlým skenom
    StringArray known = StringArray::fromLines (settings.getValue ("netKnown"));
    known.removeEmptyStrings();
    const bool first = known.isEmpty();
    newCount = 0;
    for (auto& h : hosts)
    {
        if (h.mac.isEmpty() || known.contains (h.mac)) continue;
        if (! first) { h.isNew = true; ++newCount; }
        known.add (h.mac);
    }
    settings.setValue ("netKnown", known.joinIntoString ("\n"));
    table.repaint();
    repaint();
}

void NetScanner::addHost (Host h)
{
    h.vendor = vendorOf (h.mac);
    hosts.push_back (h);
    rebuildView();
}

String NetScanner::serviceName (int port)
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

String NetScanner::servicesText (const Host& h) const
{
    StringArray s;
    for (int p : h.ports) s.add (serviceName (p));
    String r = s.joinIntoString (", ");
    if (! h.shares.isEmpty())
    {
        StringArray names;
        for (auto& x : h.shares) names.add (x.trimCharactersAtStart ("*"));
        r << " (" << names.joinIntoString (", ") << ")";
    }
    return r;
}

void NetScanner::rebuildView()
{
    view.clear();
    const String f = filter.getText().trim();
    for (int i = 0; i < (int) hosts.size(); ++i)
    {
        const auto& h = hosts[(size_t) i];
        if (f.isNotEmpty() && ! (displayName (h) + " " + h.name + " " + h.ipText + " " + h.vendor + " " + h.mac + " " + servicesText (h)).containsIgnoreCase (f)) continue;
        view.push_back (i);
    }
    const int c = sortCol;
    const bool up = sortUp;
    std::stable_sort (view.begin(), view.end(), [this, c, up] (int a, int b)
    {
        const auto& x = hosts[(size_t) a];
        const auto& y = hosts[(size_t) b];
        int r = 0;
        switch (c)
        {
            case 2: r = displayName (x).compareNatural (displayName (y)); break;
            case 4: r = x.vendor.compareIgnoreCase (y.vendor); break;
            case 5: r = x.mac.compare (y.mac); break;
            case 6: r = x.ports.size() - y.ports.size(); break;
            default: r = x.ip < y.ip ? -1 : (x.ip > y.ip ? 1 : 0); break;
        }
        if (r == 0) r = x.ip < y.ip ? -1 : (x.ip > y.ip ? 1 : 0);
        return up ? r < 0 : r > 0;
    });
    table.updateContent();
    for (int i = 0; i < (int) view.size(); ++i)
        if (view[(size_t) i] == selected) { table.selectRow (i, true, true); break; }
    table.repaint();
    repaint (getLocalBounds().removeFromBottom (26));
}

void NetScanner::sortOrderChanged (int col, bool forwards)
{
    sortCol = col;
    sortUp = forwards;
    rebuildView();
}

void NetScanner::paintRowBackground (Graphics& g, int, int w, int h, bool selected)
{
    if (selected) { g.setColour (col::accent); g.fillRect (0, 0, w, h); }
}

void NetScanner::paintCell (Graphics& g, int row, int c, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) view.size()) return;
    const auto& x = hosts[(size_t) view[(size_t) row]];
    g.setColour (selected ? col::selText : col::text);
    g.setFont (FontOptions (13.5f));
    if (c == 1)
    {
        g.setColour (x.isNew ? Colour (0xffe0a040) : Colour (0xff7fbf7a));
        g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre ({ w / 2.0f, h / 2.0f }));
        return;
    }
    String t = c == 2 ? displayName (x) + (x.isNew ? tr ("  (nové)") : String()) : c == 3 ? x.ipText : c == 4 ? x.vendor : c == 5 ? x.mac : servicesText (x);
    g.drawText (t, 6, 0, w - 8, h, Justification::centredLeft, true);
}

void NetScanner::hostMenu (int row)
{
    if (row < 0 || row >= (int) view.size()) return;
    const auto h = hosts[(size_t) view[(size_t) row]];
    PopupMenu m;
    m.addSectionHeader (h.name.isNotEmpty() ? h.name + "  (" + h.ipText + ")" : h.ipText);
    if (h.ports.contains (80)) m.addItem (1, tr ("Otvoriť web"));
    if (h.ports.contains (443)) m.addItem (2, tr ("Otvoriť web (HTTPS)"));
    if (h.ports.contains (8080)) m.addItem (3, tr ("Otvoriť web na porte 8080"));
    if (h.ports.contains (3389)) m.addItem (4, tr ("Vzdialená plocha"));
    if (h.ports.contains (21)) m.addItem (5, tr ("FTP v Korgus Commanderi"));
    if (h.ports.contains (554)) m.addItem (6, tr ("Kopírovať adresu kamery (rtsp://…)"));
    for (int i = 0; i < h.shares.size(); ++i)
        if (! h.shares[i].startsWithChar ('*'))
            m.addItem (100 + i, tr ("Otvoriť v Korgus Commanderi: ") + "\\\\" + h.ipText + "\\" + h.shares[i]);
    if (h.ports.contains (445)) m.addItem (7, tr ("Otvoriť v Prieskumníkovi: ") + "\\\\" + h.ipText);
    m.addSeparator();
    m.addItem (8, tr ("Kopírovať IP"));
    m.addItem (9, tr ("Kopírovať MAC"), h.mac.isNotEmpty());
    Component::SafePointer<NetScanner> sp (this);
    m.addItem (10, tr ("Pomenovať…"));
    m.showMenuAsync (PopupMenu::Options().withMousePosition(), [sp, h] (int r)
    {
        if (sp != nullptr && r != 0) sp->runAction (h, r);
    });
}

void NetScanner::runAction (const Host& h, int r)
{
    Component::SafePointer<NetScanner> sp (this);
    {
        if (r == 10) { renameHost(); return; }
        if (r == 1) URL ("http://" + h.ipText).launchInDefaultBrowser();
        if (r == 2) URL ("https://" + h.ipText).launchInDefaultBrowser();
        if (r == 3) URL ("http://" + h.ipText + ":8080").launchInDefaultBrowser();
       #if JUCE_WINDOWS
        if (r == 4) File::getSpecialLocation (File::windowsSystemDirectory).getChildFile ("mstsc.exe").startAsProcess ("/v:" + h.ipText);
        if (r == 7) File::getSpecialLocation (File::windowsSystemDirectory).getParentDirectory().getChildFile ("explorer.exe").startAsProcess ("\\\\" + h.ipText);
       #endif
        if (r == 5 && sp->openFtp) sp->openFtp (h.ipText);
        if (r == 6) SystemClipboard::copyTextToClipboard ("rtsp://" + h.ipText + ":554/");
        if (r == 8) SystemClipboard::copyTextToClipboard (h.ipText);
        if (r == 9) SystemClipboard::copyTextToClipboard (h.mac);
        if (r >= 100 && r - 100 < h.shares.size() && sp->openFolder)
            sp->openFolder (File ("\\\\" + h.ipText + "\\" + h.shares[r - 100]));
    }
}

void NetScanner::selectedRowsChanged (int row)
{
    const int i = row >= 0 && row < (int) view.size() ? view[(size_t) row] : -1;
    if (i == selected) return;
    stopCamera();
    selected = i;
    showDetails();
}

String NetScanner::displayName (const Host& h) const
{
    auto it = names.find (h.key());
    return it != names.end() ? it->second : h.name;
}

void NetScanner::showDetails()
{
    actions.clear();
    const bool has = selected >= 0 && selected < (int) hosts.size();
    nameB.setVisible (has);
    bool cam = false;
    if (has)
    {
        const auto& h = hosts[(size_t) selected];
        auto add = [this, h] (int id, const String& text)
        {
            auto* b = actions.add (new TextButton (text));
            b->onClick = [this, h, id] { runAction (h, id); };
            addAndMakeVisible (b);
        };
        if (h.ports.contains (80)) add (1, tr ("Otvoriť web"));
        if (h.ports.contains (443)) add (2, "HTTPS");
        if (h.ports.contains (8080)) add (3, tr ("Web 8080"));
        if (h.ports.contains (3389)) add (4, tr ("Vzdialená plocha"));
        if (h.ports.contains (21)) add (5, "FTP");
        for (int i = 0; i < h.shares.size(); ++i)
            if (! h.shares[i].startsWithChar ('*')) add (100 + i, "\\" + h.shares[i]);
        if (h.ports.contains (445)) add (7, tr ("Prieskumník"));
        add (8, tr ("Kopírovať IP"));
        cam = h.ports.contains (554) && videoAvailable;
        if (cam)
        {
            const String saved = ftpstore::unprotectText (settings.getValue ("cam_" + camKey()));
            const auto parts = StringArray::fromLines (saved);
            camUser.setText (parts[0], false);
            camPass.setText (parts[1], false);
            camPath.setText (parts.size() > 2 && parts[2].isNotEmpty() ? parts[2] : defaultCamPath (h.vendor), false);
        }
    }
    for (auto* c : { (Component*) &camUser, (Component*) &camPass, (Component*) &camPath, (Component*) &camB }) c->setVisible (cam);
    layoutDetails();
    repaint();
}

String NetScanner::camKey() const
{
    if (selected < 0 || selected >= (int) hosts.size()) return {};
    return hosts[(size_t) selected].key().replaceCharacters (":. ", "___");
}

String NetScanner::defaultCamPath (const String& v)
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

void NetScanner::startCamera()
{
    if (selected < 0 || selected >= (int) hosts.size() || ! makeVideo) return;
    const auto& h = hosts[(size_t) selected];
    settings.setValue ("cam_" + camKey(), ftpstore::protectText (camUser.getText() + "\n" + camPass.getText() + "\n" + camPath.getText()));
    String auth;
    if (camUser.getText().isNotEmpty())
        auth = URL::addEscapeChars (camUser.getText(), true) + ":" + URL::addEscapeChars (camPass.getText(), true) + "@";
    String path = camPath.getText().trim();
    if (! path.startsWithChar ('/')) path = "/" + path;
    if (video == nullptr) { video.reset (makeVideo()); if (video != nullptr) addAndMakeVisible (*video); }
    if (video == nullptr) return;
    video->setVisible (true);
    layoutDetails();
    if (playUrl) playUrl (video.get(), "rtsp://" + auth + h.ipText + ":554" + path);
    camOn = true;
    camB.setButtonText (tr ("Zastaviť"));
}

void NetScanner::stopCamera()
{
    if (video != nullptr)
    {
        if (stopVideo) stopVideo (video.get());
        video->setVisible (false);
    }
    camOn = false;
    camB.setButtonText (tr ("Zobraziť kameru"));
}

void NetScanner::loadNames()
{
    names.clear();
    if (auto x = settings.getXmlValue ("netNames"))
        for (auto* e : x->getChildWithTagNameIterator ("n"))
            names[e->getStringAttribute ("k")] = e->getStringAttribute ("name");
}

void NetScanner::saveNames()
{
    XmlElement x ("names");
    for (auto& [k, v] : names)
    {
        auto* e = x.createNewChildElement ("n");
        e->setAttribute ("k", k);
        e->setAttribute ("name", v);
    }
    settings.setValue ("netNames", &x);
}

void NetScanner::renameHost()
{
    if (selected < 0 || selected >= (int) hosts.size() || ask != nullptr) return;
    const auto key = hosts[(size_t) selected].key();
    ask = std::make_unique<AlertWindow> (tr ("Pomenovať zariadenie"), hosts[(size_t) selected].ipText, MessageBoxIconType::NoIcon, this);
    ask->addTextEditor ("n", displayName (hosts[(size_t) selected]), tr ("Názov (napr. Kamera vchod):"));
    ask->addButton (tr ("Uložiť"), 1, KeyPress (KeyPress::returnKey));
    ask->addButton (tr ("Zrušiť"), 0, KeyPress (KeyPress::escapeKey));
    addAndMakeVisible (*ask);
    ask->setCentrePosition (getLocalBounds().getCentre());
    Component::SafePointer<NetScanner> sp (this);
    ask->enterModalState (true, ModalCallbackFunction::create ([sp, key] (int r)
    {
        if (sp == nullptr || sp->ask == nullptr) return;
        const String n = sp->ask->getTextEditorContents ("n").trim();
        auto* w = sp->ask.release();
        w->setVisible (false);
        MessageManager::callAsync ([w] { delete w; });
        if (r != 1) return;
        if (n.isEmpty()) sp->names.erase (key); else sp->names[key] = n;
        sp->saveNames();
        sp->rebuildView();
        sp->repaint();
    }));
}

// zoznam výrobcov (z Wiresharku), stiahne sa raz a uloží vedľa nastavení
void NetScanner::loadOui()
{
    const auto f = settings.getFile().getSiblingFile ("vyrobcovia-mac.txt");
    auto parse = [] (const File& file, std::map<String, String>& out)
    {
        for (auto& line : StringArray::fromLines (file.loadFileAsString()))
        {
            if (line.startsWithChar ('#') || line.length() < 10 || line[8] != '\t') continue;
            const String key = line.substring (0, 8).toUpperCase();
            StringArray parts;
            parts.addTokens (line, "\t", {});
            out[key] = parts.size() > 2 && parts[2].trim().isNotEmpty() ? parts[2].trim() : parts[1].trim();
        }
    };
    if (f.existsAsFile() && f.getSize() > 100000)
    {
        parse (f, oui);
        return;
    }
    ouiLoading = true;
    Component::SafePointer<NetScanner> sp (this);
    Thread::launch ([sp, f, parse]
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
        MessageManager::callAsync ([sp, m]
        {
            if (sp == nullptr) return;
            sp->ouiLoading = false;
            sp->oui = m;
            for (auto& h : sp->hosts) h.vendor = sp->vendorOf (h.mac);
            sp->rebuildView();
        });
    });
}

String NetScanner::vendorOf (const String& mac) const
{
    if (mac.length() < 8) return {};
    auto it = oui.find (mac.substring (0, 8).toUpperCase());
    return it != oui.end() ? it->second : String();
}
}
