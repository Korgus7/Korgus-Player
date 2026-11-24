#include "WifiTransfer.h"
#include "NetWin.h"
#include "qrcodegen.hpp"

namespace kp
{
namespace
{
    bool sendAll (StreamingSocket& s, const void* data, int n)
    {
        auto* p = (const char*) data;
        while (n > 0)
        {
            const int w = s.write (p, n);
            if (w <= 0) return false;
            p += w;
            n -= w;
        }
        return true;
    }

    bool sendText (StreamingSocket& s, const String& status, const String& type, const String& body)
    {
        const auto b = body.toUTF8();
        const int len = (int) body.getNumBytesAsUTF8();
        const String h = "HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + String (len)
                       + "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
        return sendAll (s, h.toRawUTF8(), (int) h.getNumBytesAsUTF8()) && sendAll (s, b.getAddress(), len);
    }

    String safeName (String n)
    {
        n = n.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
        n = n.removeCharacters ("\\/:*?\"<>|").trim();
        while (n.startsWithChar ('.')) n = n.substring (1);
        return n.isEmpty() ? String ("subor") : n.substring (0, 200);
    }

    String htmlEsc (const String& s)
    {
        return s.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;").replace ("\"", "&quot;");
    }

    String page (const String& token, const File& dir)
    {
        String files;
        Array<File> list;
        dir.findChildFiles (list, File::findFiles | File::ignoreHiddenFiles, false);
        list.sort();
        for (auto& f : list)
            files << "<li><a href=\"/" << token << "/f/" << URL::addEscapeChars (f.getFileName(), false) << "\">"
                  << htmlEsc (f.getFileName()) << "</a> <span>" << sizeText (f.getSize()) << "</span></li>";
        String h;
        h << "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
          << "<title>Korgus Player</title><style>"
          << "body{font-family:system-ui,sans-serif;background:#161b22;color:#d9dee6;margin:0;padding:18px}"
          << "h1{font-size:20px;margin:0 0 4px}p{color:#aab3c0;margin:4px 0 16px}"
          << "label.b{display:block;background:#2d4362;color:#fff;text-align:center;padding:18px;border-radius:3px;font-size:18px;cursor:pointer}"
          << "input{display:none}#st{margin:14px 0}.bar{height:8px;background:#263040;border-radius:3px;overflow:hidden}"
          << ".bar div{height:100%;width:0;background:#4a6a8e}ul{list-style:none;padding:0}li{padding:8px 0;border-bottom:1px solid #263040}"
          << "a{color:#9fc0e8;text-decoration:none}span{color:#7d8794;font-size:13px;margin-left:6px}h2{font-size:16px;margin-top:26px}"
          << "</style></head><body><h1>Korgus Player</h1><p>" << htmlEsc (tr ("Súbory sa uložia do: ")) << htmlEsc (dir.getFullPathName()) << "</p>"
          << "<label class=\"b\">" << htmlEsc (tr ("Vybrať súbory a poslať do PC")) << "<input id=\"f\" type=\"file\" multiple></label>"
          << "<div id=\"st\"></div><div class=\"bar\"><div id=\"pb\"></div></div>"
          << "<h2>" << htmlEsc (tr ("Súbory v PC (ťukni a stiahni)")) << "</h2><ul>" << files << "</ul>"
          << "<script>const T='/" << token << "/';const f=document.getElementById('f'),st=document.getElementById('st'),pb=document.getElementById('pb');"
          << "f.onchange=async()=>{const a=[...f.files];for(let i=0;i<a.length;i++){await new Promise(ok=>{const x=new XMLHttpRequest();"
          << "x.open('POST',T+'up?n='+encodeURIComponent(a[i].name));x.upload.onprogress=e=>{pb.style.width=(e.loaded/e.total*100)+'%'};"
          << "x.onload=x.onerror=()=>ok();st.textContent=(i+1)+' / '+a.length+': '+a[i].name;x.send(a[i]);});}"
          << "st.textContent='" << tr ("Hotovo – odoslané: ") << "'+a.length;pb.style.width='100%';setTimeout(()=>location.reload(),1500);};</script>"
          << "</body></html>";
        return h;
    }
}

WifiTransfer::WifiTransfer (const File& t) : Thread ("Wi-Fi prenos"), target (t)
{
    netwin::init();
    Random rnd;
    for (int i = 0; i < 8; ++i) token << String::charToString ((juce_wchar) ("abcdefghjkmnpqrstuvwxyz23456789"[rnd.nextInt (31)]));
    for (int p = 8765; p < 8790 && port == 0; ++p)
        if (listener.createListener (p)) port = p;
    String ip;
    for (auto& a : netwin::localIPv4())
    {
        const String s (a);
        if (s.startsWith ("192.168.") || s.startsWith ("10.") || s.startsWith ("172.")) { ip = s; break; }
        if (ip.isEmpty()) ip = s;
    }
    if (port == 0) error = tr ("Prenos sa nedá spustiť (port je obsadený).");
    else if (ip.isEmpty()) error = tr ("PC nie je pripojené do siete.");
    else
    {
        address = "http://" + ip + ":" + String (port) + "/" + token + "/";
        makeQr();
        clients = std::make_unique<ThreadPool> (ThreadPoolOptions{}.withThreadName ("Wi-Fi klient").withNumberOfThreads (4));
        startThread();
    }
    openB.setButtonText (tr ("Otvoriť priečinok"));
    openB.onClick = [this] { target.revealToUser(); };
    addAndMakeVisible (openB);
    setSize (560, 560);
}

WifiTransfer::~WifiTransfer()
{
    *alive = false;
    signalThreadShouldExit();
    listener.close();
    stopThread (3000);
    if (clients) clients->removeAllJobs (true, 3000);
}

void WifiTransfer::makeQr()
{
    try
    {
        const auto q = qrcodegen::QrCode::encodeText (address.toRawUTF8(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int n = q.getSize(), border = 3, scale = 7;
        qr = Image (Image::RGB, (n + 2 * border) * scale, (n + 2 * border) * scale, true);
        Graphics g (qr);
        g.fillAll (Colours::white);
        g.setColour (Colours::black);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (q.getModule (x, y)) g.fillRect ((x + border) * scale, (y + border) * scale, scale, scale);
    }
    catch (...) {}
}

void WifiTransfer::paint (Graphics& g)
{
    g.fillAll (col::bg);
    auto r = getLocalBounds().reduced (18);
    g.setColour (col::text);
    g.setFont (FontOptions (16.0f, Font::bold));
    g.drawText (tr ("Prenos z mobilu cez Wi-Fi"), r.removeFromTop (26), Justification::centredLeft, true);
    g.setFont (FontOptions (13.5f));
    if (error.isNotEmpty())
    {
        g.setColour (Colour (0xffe08a8a));
        g.drawText (error, r.removeFromTop (24), Justification::centredLeft, true);
        return;
    }
    g.drawFittedText (tr ("Mobil musí byť v tej istej Wi-Fi. Namier fotoaparát mobilu na kód, otvor odkaz a vyber súbory. Súbory sa uložia do: ")
                      + target.getFullPathName(), r.removeFromTop (54), Justification::topLeft, 3);
    const int side = jmin (r.getWidth(), r.getHeight() - 150, 300);
    if (qr.isValid() && side > 50)
        g.drawImage (qr, Rectangle<int> (side, side).withCentre ({ r.getCentreX(), r.getY() + side / 2 }).toFloat(), RectanglePlacement::centred);
    r.removeFromTop (side + 10);
    g.setColour (col::gold);
    g.setFont (FontOptions (15.0f, Font::bold));
    g.drawText (address, r.removeFromTop (22), Justification::centred, true);
    r.removeFromTop (6);
    g.setColour (col::text);
    g.setFont (FontOptions (13.0f));
    for (int i = jmax (0, log.size() - 4); i < log.size(); ++i)
        g.drawText (log[i], r.removeFromTop (18), Justification::centredLeft, true);
}

void WifiTransfer::resized()
{
    openB.setBounds (getLocalBounds().reduced (18).removeFromBottom (30).removeFromRight (160));
}

void WifiTransfer::addLog (const String& line)
{
    log.add (Time::getCurrentTime().toString (false, true, false, true) + "  " + line);
    repaint();
}

void WifiTransfer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<StreamingSocket> c (listener.waitForNextConnection());
        if (c == nullptr) { if (threadShouldExit()) break; Thread::sleep (50); continue; }
        auto* raw = c.release();
        clients->addJob ([this, raw] { std::unique_ptr<StreamingSocket> s (raw); handle (s.get()); });
    }
}

void WifiTransfer::handle (StreamingSocket* s)
{
    // hlavička požiadavky
    MemoryBlock head;
    char ch = 0;
    while (head.getSize() < 16384)
    {
        if (s->waitUntilReady (true, 15000) != 1) return;
        if (s->read (&ch, 1, true) != 1) return;
        head.append (&ch, 1);
        const auto n = head.getSize();
        if (n >= 4 && memcmp ((const char*) head.getData() + n - 4, "\r\n\r\n", 4) == 0) break;
    }
    const String text = head.toString();
    const String first = text.upToFirstOccurrenceOf ("\r\n", false, false);
    const String method = first.upToFirstOccurrenceOf (" ", false, false);
    const String path = first.fromFirstOccurrenceOf (" ", false, false).upToLastOccurrenceOf (" ", false, false);
    int64 length = 0;
    for (auto& l : StringArray::fromLines (text))
        if (l.startsWithIgnoreCase ("Content-Length:")) length = l.fromFirstOccurrenceOf (":", false, false).trim().getLargeIntValue();
    const String base = "/" + token + "/";
    if (! path.startsWith (base)) { sendText (*s, "404 Not Found", "text/plain", "404"); return; }
    const String rest = path.substring (base.length());

    if (method == "GET" && (rest.isEmpty() || rest == "index.html"))
    {
        sendText (*s, "200 OK", "text/html; charset=utf-8", page (token, target));
        return;
    }
    if (method == "POST" && rest.startsWith ("up?n="))
    {
        const String name = safeName (URL::removeEscapeChars (rest.fromFirstOccurrenceOf ("n=", false, false).upToFirstOccurrenceOf ("&", false, false)));
        File f = target.getChildFile (name);
        if (f.exists()) f = target.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), true);
        bool ok = true;
        {
            FileOutputStream out (f);
            if (! out.openedOk()) ok = false;
            HeapBlock<char> buf (1 << 16);
            int64 left = length;
            while (ok && left > 0)
            {
                if (s->waitUntilReady (true, 30000) != 1) { ok = false; break; }
                const int n = s->read (buf, (int) jmin ((int64) (1 << 16), left), false);
                if (n <= 0) { ok = false; break; }
                if (! out.write (buf, (size_t) n)) { ok = false; break; }
                left -= n;
            }
            out.flush();
        }
        if (! ok) f.deleteFile();
        sendText (*s, ok ? "200 OK" : "500 Error", "text/plain", ok ? "OK" : "ERR");
        std::weak_ptr<bool> life = alive;
        Component::SafePointer<WifiTransfer> sp (this);
        MessageManager::callAsync ([sp, f, ok, name, length]
        {
            if (sp == nullptr) return;
            sp->addLog ((ok ? tr ("Prijaté: ") : tr ("Nepodarilo sa prijať: ")) + name + (ok ? " (" + sizeText (length) + ")" : String()));
            if (ok && sp->onReceived) sp->onReceived (f);
        });
        return;
    }
    if (method == "GET" && rest.startsWith ("f/"))
    {
        const File f = target.getChildFile (safeName (URL::removeEscapeChars (rest.substring (2))));
        if (! f.existsAsFile() || f.getParentDirectory() != target) { sendText (*s, "404 Not Found", "text/plain", "404"); return; }
        FileInputStream in (f);
        if (! in.openedOk()) { sendText (*s, "500 Error", "text/plain", "ERR"); return; }
        const String h = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " + String (f.getSize())
                       + "\r\nContent-Disposition: attachment; filename*=UTF-8''" + URL::addEscapeChars (f.getFileName(), false)
                       + "\r\nConnection: close\r\n\r\n";
        if (! sendAll (*s, h.toRawUTF8(), (int) h.getNumBytesAsUTF8())) return;
        HeapBlock<char> buf (1 << 16);
        for (;;)
        {
            const int n = in.read (buf, 1 << 16);
            if (n <= 0 || ! sendAll (*s, buf, n)) break;
        }
        Component::SafePointer<WifiTransfer> sp (this);
        const String nm = f.getFileName();
        MessageManager::callAsync ([sp, nm] { if (sp != nullptr) sp->addLog (tr ("Stiahnuté do mobilu: ") + nm); });
        return;
    }
    sendText (*s, "404 Not Found", "text/plain", "404");
}
}
