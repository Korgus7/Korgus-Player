#include "NetScanner.h"
#include "NetWin.h"

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
    h.addColumn (tr ("Názov"), 2, 200, 80, 600);
    h.addColumn ("IP", 3, 120, 80, 200);
    h.addColumn (tr ("Výrobca"), 4, 210, 80, 600);
    h.addColumn (tr ("Adresa MAC"), 5, 140, 80, 200);
    h.addColumn (tr ("Služby"), 6, 300, 80, 1200);
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
    setSize (1000, 620);
}

NetScanner::~NetScanner()
{
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
    g.drawText (s, st.reduced (10, 0), Justification::centredLeft, true);
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
    table.setBounds (r);
}

void NetScanner::startScan()
{
    if (pool != nullptr) return;
    const auto ips = parseRange (range.getText());
    if (ips.empty()) { range.grabKeyboardFocus(); return; }
    settings.setValue ("netRange", range.getText().trim());
    hosts.clear();
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
    if (pool != nullptr && done.load() >= total)
    {
        pool.reset();
        scanB.setButtonText (tr ("Prehľadať"));
        stopTimer();
        repaint();
    }
    else if (pool != nullptr && stopFlag && *stopFlag)
    {
        // zastavené: počkať, kým bežiace úlohy skončia
        if (pool->getNumJobs() == 0 || done.load() >= total) { pool.reset(); scanB.setButtonText (tr ("Prehľadať")); stopTimer(); repaint(); }
        else { pool->removeAllJobs (false, 0); }
    }
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
        if (f.isNotEmpty() && ! (h.name + " " + h.ipText + " " + h.vendor + " " + h.mac + " " + servicesText (h)).containsIgnoreCase (f)) continue;
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
            case 2: r = x.name.compareNatural (y.name); break;
            case 4: r = x.vendor.compareIgnoreCase (y.vendor); break;
            case 5: r = x.mac.compare (y.mac); break;
            case 6: r = x.ports.size() - y.ports.size(); break;
            default: r = x.ip < y.ip ? -1 : (x.ip > y.ip ? 1 : 0); break;
        }
        if (r == 0) r = x.ip < y.ip ? -1 : (x.ip > y.ip ? 1 : 0);
        return up ? r < 0 : r > 0;
    });
    table.updateContent();
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
        g.setColour (Colour (0xff7fbf7a));
        g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre ({ w / 2.0f, h / 2.0f }));
        return;
    }
    String t = c == 2 ? x.name : c == 3 ? x.ipText : c == 4 ? x.vendor : c == 5 ? x.mac : servicesText (x);
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
    m.showMenuAsync (PopupMenu::Options().withMousePosition(), [sp, h] (int r)
    {
        if (sp == nullptr || r == 0) return;
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
    });
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
