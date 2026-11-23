#include "Ftp.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <wininet.h>
 #include <wincrypt.h>
#endif

namespace kp
{
namespace
{
#if JUCE_WINDOWS
    // názvy na serveri: UTF-8 (OPTS UTF8 ON), inak kódová stránka Windows
    String decodeName (const char* p)
    {
        if (CharPointer_UTF8::isValidString (p, (int) strlen (p))) return String::fromUTF8 (p);
        wchar_t w[1024] = {};
        MultiByteToWideChar (CP_ACP, 0, p, -1, w, 1023);
        return String (w);
    }
    Time fromFileTime (const FILETIME& ft)
    {
        const int64 t = ((int64) ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        return t > 116444736000000000LL ? Time ((t - 116444736000000000LL) / 10000) : Time();
    }
#endif

    String protect (const String& s)
    {
       #if JUCE_WINDOWS
        DATA_BLOB in { (DWORD) s.getNumBytesAsUTF8(), (BYTE*) const_cast<char*> (s.toRawUTF8()) }, out {};
        if (CryptProtectData (&in, L"Korgus FTP", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        {
            MemoryBlock mb (out.pbData, out.cbData);
            LocalFree (out.pbData);
            return "d:" + mb.toBase64Encoding();
        }
       #endif
        return "p:" + MemoryBlock (s.toRawUTF8(), s.getNumBytesAsUTF8()).toBase64Encoding();
    }

    String unprotect (const String& s)
    {
        MemoryBlock mb;
        if (! mb.fromBase64Encoding (s.substring (2))) return {};
        if (s.startsWith ("p:")) return mb.toString();
       #if JUCE_WINDOWS
        if (s.startsWith ("d:"))
        {
            DATA_BLOB in { (DWORD) mb.getSize(), (BYTE*) mb.getData() }, out {};
            if (CryptUnprotectData (&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
            {
                const String r = String::fromUTF8 ((const char*) out.pbData, (int) out.cbData);
                LocalFree (out.pbData);
                return r;
            }
        }
       #endif
        return {};
    }

    std::unique_ptr<XmlElement> toXml (const std::vector<FtpConn>& v, bool plainPasswords)
    {
        auto x = std::make_unique<XmlElement> ("kcftp");
        for (auto& c : v)
        {
            auto* e = x->createNewChildElement ("conn");
            e->setAttribute ("name", c.name);
            e->setAttribute ("host", c.host);
            e->setAttribute ("port", c.port);
            e->setAttribute ("user", c.user);
            e->setAttribute ("pass", plainPasswords ? c.pass : protect (c.pass));
            e->setAttribute ("dir", c.dir);
            e->setAttribute ("passive", c.passive);
            e->setAttribute ("tls", c.tls);
        }
        return x;
    }

    std::vector<FtpConn> fromXml (const XmlElement& x, bool plainPasswords)
    {
        std::vector<FtpConn> v;
        for (auto* e : x.getChildWithTagNameIterator ("conn"))
        {
            FtpConn c;
            c.name = e->getStringAttribute ("name");
            c.host = e->getStringAttribute ("host");
            c.port = e->getIntAttribute ("port", 21);
            c.user = e->getStringAttribute ("user");
            c.pass = plainPasswords ? e->getStringAttribute ("pass") : unprotect (e->getStringAttribute ("pass"));
            c.dir = e->getStringAttribute ("dir");
            c.passive = e->getBoolAttribute ("passive", true);
            c.tls = e->getBoolAttribute ("tls", false);
            v.push_back (c);
        }
        return v;
    }

    // výpis LIST: unix „drwxr-xr-x 2 u g 4096 Nov 23 18:00 názov“ alebo DOS „11-23-26 06:00PM <DIR> názov“
    bool parseList (const String& line, FtpEntry& e)
    {
        StringArray t;
        t.addTokens (line, " ", {});
        t.removeEmptyStrings();
        if (t.size() >= 9 && String ("-dlbcps").containsChar (t[0][0]))
        {
            e.dir = t[0][0] == 'd';
            e.size = t[4].getLargeIntValue();
            int idx = 0;
            for (int i = 0; i < 8; ++i) { idx = line.indexOf (idx, t[i]) + t[i].length(); }
            e.name = line.substring (idx).trimStart();
            if (t[0][0] == 'l') e.name = e.name.upToFirstOccurrenceOf (" -> ", false, false);
            const StringArray months { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
            const int mon = months.indexOf (t[5], true);
            if (mon >= 0)
            {
                const bool hasYear = ! t[7].contains (":");
                int year = hasYear ? t[7].getIntValue() : Time::getCurrentTime().getYear();
                const int hh = hasYear ? 0 : t[7].upToFirstOccurrenceOf (":", false, false).getIntValue();
                const int mm = hasYear ? 0 : t[7].fromFirstOccurrenceOf (":", false, false).getIntValue();
                Time tm (year, mon, t[6].getIntValue(), hh, mm);
                if (! hasYear && tm > Time::getCurrentTime() + RelativeTime::days (2)) tm = Time (year - 1, mon, t[6].getIntValue(), hh, mm);
                e.mod = tm;
            }
            return e.name.isNotEmpty();
        }
        if (t.size() >= 4 && t[0].containsChar ('-') && t[1].containsAnyOf ("AP"))
        {
            e.dir = t[2] == "<DIR>";
            e.size = e.dir ? 0 : t[2].getLargeIntValue();
            int idx = 0;
            for (int i = 0; i < 3; ++i) { idx = line.indexOf (idx, t[i]) + t[i].length(); }
            e.name = line.substring (idx).trimStart();
            return e.name.isNotEmpty();
        }
        return false;
    }

    File curlExe()
    {
       #if JUCE_WINDOWS
        return File::getSpecialLocation (File::windowsSystemDirectory).getChildFile ("curl.exe");
       #else
        return File ("/usr/bin/curl");
       #endif
    }

    String curlError (int code, const String& out)
    {
        switch (code)
        {
            case 67: return tr ("Nesprávne meno alebo heslo.");
            case 6:  return tr ("Server sa nenašiel – skontroluj adresu.");
            case 7:  return tr ("Na server sa nedá pripojiť.");
            case 28: return tr ("Server neodpovedá (vypršal čas).");
            case 64: return tr ("Server nepodporuje SSL/TLS – vypni toto políčko v pripojení.");
            case 35: case 60: case 51: case 58: case 77: case 83:
                return tr ("Certifikát servera nesedí (SSL/TLS). Skús ako server zadať adresu, na ktorú je certifikát vydaný.");
            case 9:  return tr ("Priečinok na serveri sa nedá otvoriť.");
            case 78: return tr ("Súbor na serveri neexistuje.");
            case 25: return tr ("Na server sa nedá zapisovať.");
            default: break;
        }
        const auto last = out.trim().fromLastOccurrenceOf ("\n", false, false).trim();
        return (last.isNotEmpty() ? last : tr ("Chyba curl ") + String (code));
    }

    BlowFish cipher (const String& password)
    {
        SHA256 h (password.toRawUTF8(), password.getNumBytesAsUTF8());
        const auto key = h.getRawData();
        return BlowFish (key.getData(), (int) key.getSize());
    }
}

// ================================================================ úložisko

namespace ftpstore
{
    std::vector<FtpConn> load (PropertiesFile& s)
    {
        if (auto x = s.getXmlValue ("kcFtp")) return fromXml (*x, false);
        return {};
    }

    void save (PropertiesFile& s, const std::vector<FtpConn>& v)
    {
        s.setValue ("kcFtp", toXml (v, false).get());
        s.saveIfNeeded();
    }

    bool exportTo (const File& f, const std::vector<FtpConn>& v, const String& password)
    {
        const auto xml = toXml (v, true)->toString();
        MemoryBlock mb (xml.toRawUTF8(), xml.getNumBytesAsUTF8());
        cipher (password).encrypt (mb);
        return f.replaceWithText ("KCFTP1\n" + mb.toBase64Encoding());
    }

    bool importFrom (const File& f, const String& password, std::vector<FtpConn>& out)
    {
        const auto text = f.loadFileAsString();
        if (! text.startsWith ("KCFTP1")) return false;
        MemoryBlock mb;
        if (! mb.fromBase64Encoding (text.fromFirstOccurrenceOf ("\n", false, false).trim())) return false;
        cipher (password).decrypt (mb);
        const auto xmlText = mb.toString();
        if (! xmlText.startsWith ("<?xml") && ! xmlText.startsWith ("<kcftp")) return false;
        auto x = parseXML (xmlText);
        if (x == nullptr || ! x->hasTagName ("kcftp")) return false;
        out = fromXml (*x, true);
        return true;
    }
}

// ================================================================ FtpSession

FtpSession::~FtpSession() { disconnect(); }

String FtpSession::parent (const String& dir)
{
    auto d = dir.trimCharactersAtEnd ("/");
    if (d.isEmpty()) return "/";
    const int i = d.lastIndexOfChar ('/');
    return i <= 0 ? String ("/") : d.substring (0, i);
}

String FtpSession::lastError() const
{
   #if JUCE_WINDOWS
    const DWORD e = GetLastError();
    switch (e)
    {
        case ERROR_INTERNET_INCORRECT_PASSWORD:
        case ERROR_INTERNET_LOGIN_FAILURE:       return tr ("Nesprávne meno alebo heslo.");
        case ERROR_INTERNET_NAME_NOT_RESOLVED:   return tr ("Server sa nenašiel – skontroluj adresu.");
        case ERROR_INTERNET_CANNOT_CONNECT:      return tr ("Na server sa nedá pripojiť.");
        case ERROR_INTERNET_TIMEOUT:             return tr ("Server neodpovedá (vypršal čas).");
        case ERROR_INTERNET_CONNECTION_ABORTED:
        case ERROR_INTERNET_CONNECTION_RESET:    return tr ("Spojenie sa prerušilo.");
        default: break;
    }
    if (e == ERROR_INTERNET_EXTENDED_ERROR)
    {
        DWORD code = 0, len = 0;
        InternetGetLastResponseInfoA (&code, nullptr, &len);
        HeapBlock<char> buf (len + 1, true);
        if (InternetGetLastResponseInfoA (&code, buf, &len)) return String::fromUTF8 (buf).trim();
    }
    return tr ("Chyba FTP ") + String ((int) e);
   #else
    return tr ("FTP funguje len vo Windows.");
   #endif
}

String FtpSession::curlUrl (const String& path, bool dir) const
{
    // cesta je relatívna k priečinku po prihlásení („/“ = tam, kde sa server otvorí)
    StringArray parts;
    parts.addTokens (path, "/", {});
    parts.removeEmptyStrings();
    String u = "ftp://" + cfg.host.trim() + ":" + String (cfg.port > 0 ? cfg.port : 21) + "/";
    for (int i = 0; i < parts.size(); ++i)
        u << URL::addEscapeChars (parts[i], false) << (i < parts.size() - 1 || dir ? "/" : "");
    return u;
}

bool FtpSession::curlRun (const StringArray& extra, const String& url, String& out, String& error, std::atomic<bool>* cancel,
                          const File& watchFile, std::atomic<int64>* done)
{
    const auto exe = curlExe();
    if (! exe.existsAsFile()) { error = tr ("SSL/TLS potrebuje curl.exe (je vo Windows 10 a 11)."); return false; }
    // meno a heslo cez dočasný súbor (nie v príkazovom riadku)
    const auto cfgFile = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("kc-ftp", ".cfg", false);
    auto esc = [] (const String& x) { return x.replace ("\\", "\\\\").replace ("\"", "\\\""); };
    cfgFile.replaceWithText ("user = \"" + esc (cfg.user + ":" + cfg.pass) + "\"\n");
    StringArray a { exe.getFullPathName(), "-sS", "--ssl-reqd", "--connect-timeout", "20", "-K", cfgFile.getFullPathName() };
   #if JUCE_WINDOWS
    a.add ("--ssl-no-revoke");
   #endif
    if (! cfg.passive) { a.add ("-P"); a.add ("-"); }
    a.addArray (extra);
    a.add (url);
    ChildProcess cp;
    if (! cp.start (a)) { cfgFile.deleteFile(); error = tr ("curl.exe sa nedá spustiť."); return false; }
    const int64 base = done != nullptr ? done->load() : 0;
    MemoryOutputStream mo;
    char buf[8192];
    while (cp.isRunning())
    {
        if (cancel != nullptr && *cancel) { cp.kill(); cfgFile.deleteFile(); return false; }
        const int n = cp.readProcessOutput (buf, sizeof (buf));
        if (n > 0) mo.write (buf, (size_t) n);
        else Thread::sleep (50);
        if (done != nullptr && watchFile != File()) *done = base + watchFile.getSize();
    }
    for (;;) { const int n = cp.readProcessOutput (buf, sizeof (buf)); if (n <= 0) break; mo.write (buf, (size_t) n); }
    cfgFile.deleteFile();
    out = mo.toUTF8();
    const int code = (int) cp.getExitCode();
    if (code != 0) { error = curlError (code, out); return false; }
    return true;
}

bool FtpSession::curlQuote (const StringArray& cmds)
{
    StringArray extra;
    for (auto& c : cmds) { extra.add ("-Q"); extra.add (c); }
    extra.add ("--list-only");
    String out, err;
    return curlRun (extra, curlUrl ("/", true), out, err);
}

bool FtpSession::connect (const FtpConn& c, String& error)
{
    const ScopedLock sl (lock);
    if (c.tls)
    {
        cfg = c;
        home = "/";
        String out;
        if (! curlRun ({ "--list-only" }, curlUrl ("/", true), out, error)) return false;
        conn = this;   // označenie „pripojené“ (curl sa pripája pri každej operácii)
        return true;
    }
   #if JUCE_WINDOWS
    if (conn != nullptr) InternetCloseHandle ((HINTERNET) conn);
    if (inet != nullptr) InternetCloseHandle ((HINTERNET) inet);
    conn = inet = nullptr;
    inet = InternetOpenW (L"Korgus Player", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (inet == nullptr) { error = lastError(); return false; }
    DWORD timeout = 20000;
    InternetSetOptionW ((HINTERNET) inet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof (timeout));
    InternetSetOptionW ((HINTERNET) inet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof (timeout));
    conn = InternetConnectW ((HINTERNET) inet, c.host.trim().toWideCharPointer(), (INTERNET_PORT) (c.port > 0 ? c.port : 21),
                             c.user.toWideCharPointer(), c.pass.toWideCharPointer(), INTERNET_SERVICE_FTP,
                             c.passive ? INTERNET_FLAG_PASSIVE : 0, 0);
    if (conn == nullptr)
    {
        error = lastError();
        InternetCloseHandle ((HINTERNET) inet);
        inet = nullptr;
        return false;
    }
    FtpCommandA ((HINTERNET) conn, FALSE, FTP_TRANSFER_TYPE_ASCII, "OPTS UTF8 ON", 0, nullptr);
    char buf[1024] = {};
    DWORD n = sizeof (buf);
    home = FtpGetCurrentDirectoryA ((HINTERNET) conn, buf, &n) ? decodeName (buf) : String ("/");
    cfg = c;
    return true;
   #else
    ignoreUnused (c);
    error = lastError();
    return false;
   #endif
}

void FtpSession::disconnect()
{
    const ScopedLock sl (lock);
    if (cfg.tls) { conn = inet = nullptr; return; }
   #if JUCE_WINDOWS
    if (conn != nullptr) InternetCloseHandle ((HINTERNET) conn);
    if (inet != nullptr) InternetCloseHandle ((HINTERNET) inet);
   #endif
    conn = inet = nullptr;
}

bool FtpSession::list (const String& dir, std::vector<FtpEntry>& out, String& error)
{
    const ScopedLock sl (lock);
    out.clear();
    if (cfg.tls)
    {
        String text;
        if (! curlRun ({}, curlUrl (dir, true), text, error)) return false;
        for (auto& line : StringArray::fromLines (text))
        {
            FtpEntry e;
            if (parseList (line, e) && e.name != "." && e.name != "..") out.push_back (e);
        }
        return true;
    }
   #if JUCE_WINDOWS
    if (conn == nullptr) { error = tr ("Spojenie je zatvorené."); return false; }
    if (! FtpSetCurrentDirectoryA ((HINTERNET) conn, dir.toRawUTF8())) { error = lastError(); return false; }
    WIN32_FIND_DATAA fd {};
    HINTERNET h = FtpFindFirstFileA ((HINTERNET) conn, nullptr, &fd, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (h == nullptr)
    {
        if (GetLastError() == ERROR_NO_MORE_FILES) return true;
        error = lastError();
        return false;
    }
    do
    {
        const String name = decodeName (fd.cFileName);
        if (name == "." || name == ".." || name.isEmpty()) continue;
        FtpEntry e;
        e.name = name;
        e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size = ((int64) fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        e.mod = fromFileTime (fd.ftLastWriteTime);
        out.push_back (e);
    } while (InternetFindNextFileA (h, &fd));
    InternetCloseHandle (h);
    return true;
   #else
    ignoreUnused (dir);
    error = lastError();
    return false;
   #endif
}

bool FtpSession::download (const String& remote, const File& local, std::atomic<bool>& cancel, std::atomic<int64>& done, String& error)
{
    const ScopedLock sl (lock);
    if (cfg.tls)
    {
        String out;
        local.deleteFile();
        const bool ok = curlRun ({ "-o", local.getFullPathName() }, curlUrl (remote, false), out, error, &cancel, local, &done);
        if (! ok) local.deleteFile();
        return ok;
    }
   #if JUCE_WINDOWS
    HINTERNET f = FtpOpenFileA ((HINTERNET) conn, remote.toRawUTF8(), GENERIC_READ, FTP_TRANSFER_TYPE_BINARY | INTERNET_FLAG_RELOAD, 0);
    if (f == nullptr) { error = lastError(); return false; }
    local.deleteFile();
    bool ok = true;
    {
        FileOutputStream out (local);
        if (! out.openedOk()) { InternetCloseHandle (f); error = tr ("Do cieľa sa nedá zapisovať: ") + local.getFullPathName(); return false; }
        HeapBlock<char> buf (1 << 16);
        for (;;)
        {
            if (cancel) { ok = false; break; }
            DWORD n = 0;
            if (! InternetReadFile (f, buf, 1 << 16, &n)) { error = lastError(); ok = false; break; }
            if (n == 0) break;
            if (! out.write (buf, n)) { error = tr ("Na disku nie je miesto alebo sa nedá zapisovať."); ok = false; break; }
            done += n;
        }
        out.flush();
    }
    InternetCloseHandle (f);
    if (! ok) local.deleteFile();
    return ok;
   #else
    ignoreUnused (remote, local, cancel, done);
    error = lastError();
    return false;
   #endif
}

bool FtpSession::upload (const File& local, const String& remote, std::atomic<bool>& cancel, std::atomic<int64>& done, String& error)
{
    const ScopedLock sl (lock);
    if (cfg.tls)
    {
        String out;
        const bool ok = curlRun ({ "-T", local.getFullPathName() }, curlUrl (remote, false), out, error, &cancel);
        if (ok) done += local.getSize();
        return ok;
    }
   #if JUCE_WINDOWS
    FileInputStream in (local);
    if (! in.openedOk()) { error = tr ("Súbor sa nedá otvoriť: ") + local.getFileName(); return false; }
    HINTERNET f = FtpOpenFileA ((HINTERNET) conn, remote.toRawUTF8(), GENERIC_WRITE, FTP_TRANSFER_TYPE_BINARY, 0);
    if (f == nullptr) { error = lastError(); return false; }
    bool ok = true;
    HeapBlock<char> buf (1 << 16);
    for (;;)
    {
        if (cancel) { ok = false; break; }
        const int n = in.read (buf, 1 << 16);
        if (n <= 0) break;
        DWORD w = 0;
        if (! InternetWriteFile (f, buf, (DWORD) n, &w) || w != (DWORD) n) { error = lastError(); ok = false; break; }
        done += n;
    }
    if (! InternetCloseHandle (f) && ok) { error = lastError(); ok = false; }
    if (! ok) FtpDeleteFileA ((HINTERNET) conn, remote.toRawUTF8());   // nedokončený súbor preč
    return ok;
   #else
    ignoreUnused (local, remote, cancel, done);
    error = lastError();
    return false;
   #endif
}

static String rel (const String& p) { return p.trimCharactersAtStart ("/"); }
#if JUCE_WINDOWS
 #define KC_WININET(expr) (conn != nullptr && (expr))
#else
 #define KC_WININET(expr) false
#endif
bool FtpSession::removeFile (const String& r)
{
    const ScopedLock sl (lock);
    if (cfg.tls) return curlQuote ({ "DELE " + rel (r) });
    return KC_WININET (FtpDeleteFileA ((HINTERNET) conn, r.toRawUTF8()));
}
bool FtpSession::removeDir (const String& r)
{
    const ScopedLock sl (lock);
    if (cfg.tls) return curlQuote ({ "RMD " + rel (r) });
    return KC_WININET (FtpRemoveDirectoryA ((HINTERNET) conn, r.toRawUTF8()));
}
bool FtpSession::makeDir (const String& r)
{
    const ScopedLock sl (lock);
    if (cfg.tls) return curlQuote ({ "MKD " + rel (r) });
    return KC_WININET (FtpCreateDirectoryA ((HINTERNET) conn, r.toRawUTF8()));
}
bool FtpSession::rename (const String& a, const String& b)
{
    const ScopedLock sl (lock);
    if (cfg.tls) return curlQuote ({ "RNFR " + rel (a), "RNTO " + rel (b) });
    return KC_WININET (FtpRenameFileA ((HINTERNET) conn, a.toRawUTF8(), b.toRawUTF8()));
}
#undef KC_WININET

// ================================================================ FtpJob

FtpJob::FtpJob (std::shared_ptr<FtpSession> s, Kind k, std::vector<Item> i, bool m)
    : Thread ("FTP"), session (std::move (s)), what (k), items (std::move (i)), move (m) {}

FtpJob::~FtpJob()
{
    cancelled = true;
    stopThread (20000);
}

void FtpJob::planUpload (const File& local, const String& remote, std::vector<Op>& ops)
{
    if (local.isDirectory())
    {
        ops.push_back ({ local, remote, true, 0 });
        for (const auto& e : RangedDirectoryIterator (local, false, "*", File::findFilesAndDirectories))
            planUpload (e.getFile(), FtpSession::join (remote, e.getFile().getFileName()), ops);
    }
    else ops.push_back ({ local, remote, false, local.getSize() });
}

void FtpJob::planDownload (const String& remote, const File& local, std::vector<Op>& ops)
{
    ops.push_back ({ local, remote, true, 0 });
    std::vector<FtpEntry> list;
    String err;
    if (! session->list (remote, list, err)) { error = err; return; }
    for (auto& e : list)
    {
        if (cancelled) return;
        const auto r = FtpSession::join (remote, e.name);
        if (e.dir) planDownload (r, local.getChildFile (e.name), ops);
        else ops.push_back ({ local.getChildFile (e.name), r, false, e.size });
    }
}

void FtpJob::planDelete (const String& remote, bool dir, std::vector<Op>& ops)
{
    if (dir)
    {
        std::vector<FtpEntry> list;
        String err;
        if (session->list (remote, list, err))
            for (auto& e : list)
                planDelete (FtpSession::join (remote, e.name), e.dir, ops);
    }
    ops.push_back ({ File(), remote, dir, 1 });   // priečinok až po obsahu
}

void FtpJob::run()
{
    std::vector<Op> ops;
    for (auto& it : items)
    {
        if (what == Kind::Upload) planUpload (it.local, it.remote, ops);
        else if (what == Kind::Download)
        {
            if (it.dir) planDownload (it.remote, it.local, ops);
            else ops.push_back ({ it.local, it.remote, false, it.size });
        }
        else planDelete (it.remote, it.dir, ops);
    }
    int64 t = 0;
    int files = 0;
    for (auto& o : ops) { if (o.size > 0) t += o.size; if (! o.dir || what == Kind::Delete) ++files; }
    total = jmax ((int64) 1, t);
    numFiles = files;

    int idx = 0;
    for (auto& o : ops)
    {
        if (cancelled || error.isNotEmpty()) break;
        String err;
        if (what == Kind::Upload)
        {
            if (o.dir) session->makeDir (o.remote);   // už môže existovať
            else
            {
                current = idx++;
                if (! session->upload (o.local, o.remote, cancelled, done, err)) { if (! cancelled) error = err + " (" + o.local.getFileName() + ")"; break; }
                ++completed;
            }
        }
        else if (what == Kind::Download)
        {
            if (o.dir) o.local.createDirectory();
            else
            {
                current = idx++;
                if (! session->download (o.remote, o.local, cancelled, done, err)) { if (! cancelled) error = err + " (" + o.remote.fromLastOccurrenceOf ("/", false, false) + ")"; break; }
                ++completed;
            }
        }
        else
        {
            current = idx++;
            const bool ok = o.dir ? session->removeDir (o.remote) : session->removeFile (o.remote);
            if (! ok) { error = tr ("Nepodarilo sa zmazať ") + o.remote.fromLastOccurrenceOf ("/", false, false); break; }
            done += 1;
            ++completed;
        }
    }

    // presun: originály preč až keď všetko prešlo
    if (move && ! cancelled && error.isEmpty())
    {
        if (what == Kind::Upload)
        {
            for (auto& it : items)
                if (! it.local.moveToTrash()) error = tr ("Nahrané, ale originál sa nedal presunúť do Koša: ") + it.local.getFileName();
        }
        else if (what == Kind::Download)
        {
            std::vector<Op> del;
            for (auto& it : items) planDelete (it.remote, it.dir, del);
            for (auto& o : del)
                if (! (o.dir ? session->removeDir (o.remote) : session->removeFile (o.remote)))
                    error = tr ("Stiahnuté, ale na serveri sa nedalo zmazať: ") + o.remote.fromLastOccurrenceOf ("/", false, false);
        }
    }
    finished = true;
}

// ================================================================ okno pripojení

namespace
{
    // úprava jedného pripojenia
    class ConnEditor : public Component
    {
    public:
        ConnEditor (const FtpConn& c, std::function<void (const FtpConn*)> done) : finish (std::move (done))
        {
            const char* labels[] = { "Názov", "Server", "Port", "Používateľ", "Heslo", "Vzdialený priečinok" };
            const String values[] = { c.name, c.host, String (c.port), c.user, c.pass, c.dir };
            for (int i = 0; i < 6; ++i)
            {
                auto* l = lbl.add (new Label ({}, tr (labels[i])));
                l->setColour (Label::textColourId, col::text);
                addAndMakeVisible (l);
                auto* e = ed.add (new TextEditor());
                e->setText (values[i], false);
                if (i == 4) e->setPasswordCharacter ((juce_wchar) 0x2022);
                if (i == 2) e->setInputRestrictions (5, "0123456789");
                addAndMakeVisible (e);
            }
            passive.setButtonText (tr ("Pasívny režim"));
            passive.setToggleState (c.passive, dontSendNotification);
            addAndMakeVisible (passive);
            tls.setButtonText (tr ("SSL/TLS (FTPS)"));
            tls.setToggleState (c.tls, dontSendNotification);
            addAndMakeVisible (tls);
            save.setButtonText (tr ("Uložiť"));
            cancel.setButtonText (tr ("Zrušiť"));
            save.onClick = [this]
            {
                FtpConn r;
                r.name = ed[0]->getText().trim();
                r.host = ed[1]->getText().trim();
                r.port = jlimit (1, 65535, ed[2]->getText().getIntValue() > 0 ? ed[2]->getText().getIntValue() : 21);
                r.user = ed[3]->getText().trim();
                r.pass = ed[4]->getText();
                r.dir = ed[5]->getText().trim();
                r.passive = passive.getToggleState();
                r.tls = tls.getToggleState();
                if (r.host.isEmpty()) { ed[1]->grabKeyboardFocus(); return; }
                if (r.name.isEmpty()) r.name = r.host;
                finish (&r);
            };
            cancel.onClick = [this] { finish (nullptr); };
            save.addShortcut (KeyPress (KeyPress::returnKey));
            cancel.addShortcut (KeyPress (KeyPress::escapeKey));
            addAndMakeVisible (save);
            addAndMakeVisible (cancel);
        }
        void paint (Graphics& g) override
        {
            g.fillAll (col::panel);
            g.setColour (col::text);
            g.setFont (FontOptions (15.0f, Font::bold));
            g.drawText (tr ("FTP pripojenie"), getLocalBounds().removeFromTop (36).reduced (14, 0), Justification::centredLeft);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (14, 0).withTrimmedTop (40);
            for (int i = 0; i < 6; ++i)
            {
                auto row = r.removeFromTop (30);
                lbl[i]->setBounds (row.removeFromLeft (150));
                ed[i]->setBounds (row.reduced (0, 3));
            }
            passive.setBounds (r.removeFromTop (30).withTrimmedLeft (150));
            tls.setBounds (r.removeFromTop (30).withTrimmedLeft (150));
            auto b = r.removeFromBottom (44).reduced (0, 7);
            cancel.setBounds (b.removeFromRight (110));
            b.removeFromRight (8);
            save.setBounds (b.removeFromRight (110));
        }
        void visibilityChanged() override { if (isShowing()) ed[0]->grabKeyboardFocus(); }

    private:
        OwnedArray<Label> lbl;
        OwnedArray<TextEditor> ed;
        ToggleButton passive, tls;
        TextButton save, cancel;
        std::function<void (const FtpConn*)> finish;
    };
}

FtpManager::FtpManager (PropertiesFile& s) : settings (s)
{
    conns = ftpstore::load (settings);
    setWantsKeyboardFocus (true);
    list.setRowHeight (26);
    list.setColour (ListBox::backgroundColourId, col::docBg);
    list.setColour (ListBox::outlineColourId, col::line);
    list.setOutlineThickness (1);
    addAndMakeVisible (list);
    struct B { TextButton* b; const char* t; };
    for (auto x : { B { &connectB, "Pripojiť" }, B { &newB, "Nové…" }, B { &editB, "Upraviť…" }, B { &delB, "Odstrániť" },
                    B { &exportB, "Export…" }, B { &importB, "Import…" }, B { &closeB, "Zavrieť" } })
    {
        x.b->setButtonText (tr (x.t));
        addAndMakeVisible (x.b);
    }
    connectB.onClick = [this] { doConnect(); };
    newB.onClick = [this] { edit (-1); };
    editB.onClick = [this] { edit (list.getSelectedRow()); };
    delB.onClick = [this] { remove(); };
    exportB.onClick = [this] { doExport(); };
    importB.onClick = [this] { doImport(); };
    closeB.onClick = [this] { if (onClose) onClose(); };
    if (! conns.empty()) list.selectRow (0);
}

void FtpManager::paint (Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::line);
    g.drawRect (getLocalBounds(), 1);
    g.setColour (col::text);
    g.setFont (FontOptions (15.0f, Font::bold));
    g.drawText (tr ("FTP pripojenia"), getLocalBounds().removeFromTop (38).reduced (14, 0), Justification::centredLeft);
    if (conns.empty())
    {
        g.setFont (FontOptions (13.5f));
        g.drawText (tr ("Zatiaľ žiadne pripojenie – klikni na Nové…"), list.getBounds().reduced (10), Justification::centredTop, true);
    }
}

void FtpManager::resized()
{
    auto r = getLocalBounds().reduced (14).withTrimmedTop (28);
    auto side = r.removeFromRight (130);
    r.removeFromRight (10);
    list.setBounds (r);
    for (auto* b : { &connectB, &newB, &editB, &delB, &exportB, &importB })
    {
        b->setBounds (side.removeFromTop (30));
        side.removeFromTop (6);
    }
    closeB.setBounds (side.removeFromBottom (30));
    if (editor != nullptr) editor->setBounds (getLocalBounds().reduced (1));
}

bool FtpManager::keyPressed (const KeyPress& key)
{
    if (key.isKeyCode (KeyPress::escapeKey)) { if (onClose) onClose(); return true; }
    return false;
}

void FtpManager::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) conns.size()) return;
    if (selected) { g.setColour (col::accent); g.fillRect (0, 0, w, h); }
    const auto& c = conns[(size_t) row];
    g.setColour (selected ? col::selText : col::text);
    g.setFont (FontOptions (13.5f));
    g.drawText (c.name, 8, 0, w / 2 - 8, h, Justification::centredLeft, true);
    g.drawText (c.user.isNotEmpty() ? c.user + "@" + c.host : c.host, w / 2, 0, w / 2 - 8, h, Justification::centredLeft, true);
}

void FtpManager::doConnect()
{
    if (conns.empty()) { edit (-1); return; }
    int i = list.getSelectedRow();
    if (i < 0 || i >= (int) conns.size()) i = 0;
    const FtpConn c = conns[(size_t) i];
    if (onConnect) onConnect (c);
}

void FtpManager::store()
{
    ftpstore::save (settings, conns);
    list.updateContent();
    repaint();
}

void FtpManager::edit (int index)
{
    FtpConn c;
    if (index >= 0 && index < (int) conns.size()) c = conns[(size_t) index];
    else index = -1;
    Component::SafePointer<FtpManager> sp (this);
    editor = std::make_unique<ConnEditor> (c, [sp, index] (const FtpConn* r)
    {
        if (sp == nullptr) return;
        if (r != nullptr)
        {
            if (index >= 0) sp->conns[(size_t) index] = *r; else sp->conns.push_back (*r);
            sp->store();
            sp->list.selectRow (index >= 0 ? index : (int) sp->conns.size() - 1);
        }
        MessageManager::callAsync ([sp] { if (sp != nullptr) { sp->editor.reset(); sp->list.grabKeyboardFocus(); } });
    });
    addAndMakeVisible (*editor);
    resized();
}

void FtpManager::remove()
{
    const int i = list.getSelectedRow();
    if (i < 0 || i >= (int) conns.size()) return;
    conns.erase (conns.begin() + i);
    store();
    list.selectRow (jmin (i, (int) conns.size() - 1));
}

void FtpManager::askPassword (const String& title, std::function<void (String)> done)
{
    askWin = std::make_unique<AlertWindow> (title, tr ("Heslo k súboru s pripojeniami:"), MessageBoxIconType::NoIcon, this);
    askWin->addTextEditor ("p", {}, {}, true);
    askWin->addButton (tr ("OK"), 1, KeyPress (KeyPress::returnKey));
    askWin->addButton (tr ("Zrušiť"), 0, KeyPress (KeyPress::escapeKey));
    addAndMakeVisible (*askWin);
    askWin->setCentrePosition (getLocalBounds().getCentre());
    Component::SafePointer<FtpManager> sp (this);
    askWin->enterModalState (true, ModalCallbackFunction::create ([sp, done] (int r)
    {
        if (sp == nullptr || sp->askWin == nullptr) return;
        const String p = sp->askWin->getTextEditorContents ("p");
        auto* w = sp->askWin.release();
        w->setVisible (false);
        MessageManager::callAsync ([w] { delete w; });
        if (r == 1 && p.isNotEmpty()) done (p);
    }));
}

void FtpManager::doExport()
{
    if (conns.empty()) return;
    chooser = std::make_unique<FileChooser> (tr ("Export FTP pripojení"),
                                             File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Korgus FTP.kcftp"), "*.kcftp");
    Component::SafePointer<FtpManager> sp (this);
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                          [sp] (const FileChooser& fc)
    {
        if (sp == nullptr) return;
        auto f = fc.getResult();
        if (f == File()) return;
        if (! f.hasFileExtension ("kcftp")) f = f.withFileExtension ("kcftp");
        sp->askPassword (tr ("Export"), [sp, f] (String pass)
        {
            if (sp == nullptr) return;
            const bool ok = ftpstore::exportTo (f, sp->conns, pass);
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::NoIcon, tr ("Export"),
                                              ok ? tr ("Pripojenia sú uložené do ") + f.getFileName() : tr ("Súbor sa nepodarilo zapísať."));
        });
    });
}

void FtpManager::doImport()
{
    chooser = std::make_unique<FileChooser> (tr ("Import FTP pripojení"), File::getSpecialLocation (File::userDocumentsDirectory), "*.kcftp");
    Component::SafePointer<FtpManager> sp (this);
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [sp] (const FileChooser& fc)
    {
        if (sp == nullptr) return;
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        sp->askPassword (tr ("Import"), [sp, f] (String pass)
        {
            if (sp == nullptr) return;
            std::vector<FtpConn> in;
            if (! ftpstore::importFrom (f, pass, in))
            {
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::NoIcon, tr ("Import"), tr ("Nesprávne heslo alebo poškodený súbor."));
                return;
            }
            int added = 0, replaced = 0;
            for (auto& c : in)
            {
                auto it = std::find_if (sp->conns.begin(), sp->conns.end(), [&] (const FtpConn& x) { return x.name == c.name; });
                if (it != sp->conns.end()) { *it = c; ++replaced; }
                else { sp->conns.push_back (c); ++added; }
            }
            sp->store();
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::NoIcon, tr ("Import"),
                                              tr ("Pridané: ") + String (added) + tr (" · nahradené (rovnaký názov): ") + String (replaced));
        });
    });
}
}
