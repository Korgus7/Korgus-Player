#include "Archive.h"

namespace kp::archive
{
namespace
{
    File tarExe()
    {
       #if JUCE_WINDOWS
        return File::getSpecialLocation (File::windowsSystemDirectory).getChildFile ("tar.exe");
       #else
        return File ("/usr/bin/bsdtar");
       #endif
    }

    bool under (const String& path, const String& p) { return path == p || path.startsWith (p + "/"); }

    void collect (const File& f, const String& stored, std::vector<std::pair<File, String>>& out)
    {
        if (f.isDirectory())
        {
            for (const auto& e : RangedDirectoryIterator (f, false, "*", File::findFilesAndDirectories))
                collect (e.getFile(), stored + "/" + e.getFile().getFileName(), out);
        }
        else out.push_back ({ f, stored });
    }

    // bsdtar -tv: "-rw-r--r--  0 user group  1234 Nov 23 18:00 cesta/súbor"
    bool parseTarLine (const String& line, ArcItem& it)
    {
        StringArray t;
        t.addTokens (line, " ", {});
        t.removeEmptyStrings();
        if (t.size() < 9) return false;
        it.dir = t[0].startsWithChar ('d');
        it.size = t[4].getLargeIntValue();
        int idx = line.indexOf (t[7]);
        idx = line.indexOf (idx, " ");
        if (idx < 0) return false;
        it.path = line.substring (idx + 1).trimCharactersAtEnd ("/").trim();
        const StringArray months { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        const int mon = months.indexOf (t[5]);
        if (mon >= 0)
        {
            const bool hasYear = ! t[7].contains (":");
            const int year = hasYear ? t[7].getIntValue() : Time::getCurrentTime().getYear();
            const int hh = hasYear ? 0 : t[7].upToFirstOccurrenceOf (":", false, false).getIntValue();
            const int mm = hasYear ? 0 : t[7].fromFirstOccurrenceOf (":", false, false).getIntValue();
            it.mod = Time (year, mon, t[6].getIntValue(), hh, mm);
        }
        return it.path.isNotEmpty();
    }

    bool runTar (const StringArray& args, String& out, String& error)
    {
        const auto tar = tarExe();
        if (! tar.existsAsFile()) { error = tr ("Tento archív otvorí len novší Windows (chýba tar.exe)."); return false; }
        ChildProcess cp;
        StringArray a { tar.getFullPathName() };
        a.addArray (args);
        if (! cp.start (a)) { error = tr ("tar.exe sa nedá spustiť."); return false; }
        out = cp.readAllProcessOutput();
        cp.waitForProcessToFinish (600000);
        if (cp.getExitCode() != 0) { error = tr ("Archív sa nedá prečítať: ") + out.trim().fromLastOccurrenceOf ("\n", false, false); return false; }
        return true;
    }

    // ZIP: pridá existujúce položky (okrem vynechaných) a nové súbory, zapíše cez dočasný súbor
    bool rebuildZip (const File& zip, const StringArray& skip, const std::vector<std::pair<File, String>>& add, TaskJob* job, String& error)
    {
        std::unique_ptr<ZipFile> src;
        if (zip.existsAsFile()) src = std::make_unique<ZipFile> (zip);
        ZipFile::Builder b;
        StringArray added;
        for (auto& [f, p] : add) added.add (p);
        if (src != nullptr)
            for (int i = 0; i < src->getNumEntries(); ++i)
            {
                const auto* e = src->getEntry (i);
                const String name = e->filename.replaceCharacter ('\\', '/').trimCharactersAtEnd ("/");
                bool drop = added.contains (name);
                for (auto& s : skip) if (under (name, s)) drop = true;
                if (drop || e->filename.endsWithChar ('/')) continue;
                if (auto* in = src->createStreamForEntry (i))
                    b.addEntry (std::unique_ptr<InputStream> (in), 6, name, e->fileTime);
            }
        for (auto& [f, p] : add) b.addFile (f, 6, p);
        const auto tmp = zip.getSiblingFile (zip.getFileNameWithoutExtension() + ".kc-tmp");
        tmp.deleteFile();
        {
            FileOutputStream out (tmp);
            if (! out.openedOk()) { error = tr ("Do priečinka sa nedá zapisovať."); return false; }
            double prog = 0.0;
            if (! b.writeToStream (out, job != nullptr ? &prog : nullptr)) { error = tr ("Archív sa nepodarilo zapísať."); out.flush(); tmp.deleteFile(); return false; }
            out.flush();
            if (out.getStatus().failed()) { tmp.deleteFile(); error = tr ("Na disku nie je miesto."); return false; }
        }
        src.reset();
        if (! tmp.moveFileTo (zip)) { tmp.deleteFile(); error = tr ("Archív sa nedá prepísať (možno je otvorený)."); return false; }
        return true;
    }
}

bool isZip (const File& f) { return f.hasFileExtension ("zip"); }

bool isArchive (const File& f)
{
    if (f.isDirectory()) return false;
    const auto n = f.getFileName().toLowerCase();
    return n.endsWith (".zip") || n.endsWith (".rar") || n.endsWith (".7z") || n.endsWith (".tar")
        || n.endsWith (".tgz") || n.endsWith (".tar.gz") || n.endsWith (".tar.bz2") || n.endsWith (".tar.xz");
}

bool list (const File& arc, std::vector<ArcItem>& out, String& error)
{
    out.clear();
    std::map<String, size_t> index;
    auto addItem = [&] (ArcItem it)
    {
        // chýbajúce nadradené priečinky
        String p = it.path;
        while (p.contains ("/"))
        {
            p = p.upToLastOccurrenceOf ("/", false, false);
            if (index.count (p) == 0) { index[p] = out.size(); out.push_back ({ p, true, 0, it.mod }); }
        }
        if (auto f = index.find (it.path); f != index.end()) { if (! it.dir) out[f->second] = it; return; }
        index[it.path] = out.size();
        out.push_back (it);
    };
    if (isZip (arc))
    {
        ZipFile z (arc);
        if (z.getNumEntries() == 0 && arc.getSize() > 22) { error = tr ("ZIP sa nedá prečítať."); return false; }
        for (int i = 0; i < z.getNumEntries(); ++i)
        {
            const auto* e = z.getEntry (i);
            ArcItem it;
            it.dir = e->filename.endsWithChar ('/') || e->filename.endsWithChar ('\\');
            it.path = e->filename.replaceCharacter ('\\', '/').trimCharactersAtEnd ("/");
            it.size = (int64) e->uncompressedSize;
            it.mod = e->fileTime;
            if (it.path.isNotEmpty()) addItem (it);
        }
        return true;
    }
    String text;
    if (! runTar ({ "-tvf", arc.getFullPathName() }, text, error)) return false;
    for (auto& line : StringArray::fromLines (text))
    {
        ArcItem it;
        if (parseTarLine (line, it)) addItem (it);
    }
    return true;
}

bool extract (const File& arc, const StringArray& paths, const String& baseDir, const File& dest, TaskJob* job, String& error)
{
    const String base = baseDir.isEmpty() ? String() : baseDir + "/";
    if (isZip (arc))
    {
        ZipFile z (arc);
        Array<int> todo;
        int64 total = 0, done = 0;
        for (int i = 0; i < z.getNumEntries(); ++i)
        {
            const String name = z.getEntry (i)->filename.replaceCharacter ('\\', '/');
            for (auto& p : paths)
                if (under (name.trimCharactersAtEnd ("/"), p)) { todo.add (i); total += (int64) z.getEntry (i)->uncompressedSize; break; }
        }
        for (int i : todo)
        {
            if (job != nullptr && job->cancelled) return false;
            const auto* e = z.getEntry (i);
            const String name = e->filename.replaceCharacter ('\\', '/');
            const String rel = name.startsWith (base) ? name.substring (base.length()) : name;
            if (rel.contains ("..")) continue;   // bezpečnosť: nič mimo cieľa
            const auto target = dest.getChildFile (rel);
            if (name.endsWithChar ('/')) { target.createDirectory(); continue; }
            target.getParentDirectory().createDirectory();
            std::unique_ptr<InputStream> in (z.createStreamForEntry (i));
            if (in == nullptr) { error = tr ("Poškodená položka: ") + rel; return false; }
            target.deleteFile();
            FileOutputStream out (target);
            if (! out.openedOk()) { error = tr ("Do cieľa sa nedá zapisovať: ") + rel; return false; }
            out.writeFromInputStream (*in, -1);
            out.flush();
            target.setLastModificationTime (e->fileTime);
            done += (int64) e->uncompressedSize;
            if (job != nullptr) job->progress = total > 0 ? (double) done / (double) total : 1.0;
        }
        return true;
    }
    // ostatné cez tar.exe: do dočasného priečinka v cieli (rovnaký disk), potom presunúť
    const auto tmp = dest.getNonexistentChildFile (".kc-rozbal", {}, false);
    if (! tmp.createDirectory()) { error = tr ("Do cieľa sa nedá zapisovať."); return false; }
    StringArray args { "-xf", arc.getFullPathName(), "-C", tmp.getFullPathName() };
    args.addArray (paths);
    String out;
    bool ok = runTar (args, out, error);
    if (ok)
        for (auto& p : paths)
        {
            const auto from = tmp.getChildFile (p);
            const String rel = p.startsWith (base) ? p.substring (base.length()) : p;
            const auto to = dest.getChildFile (rel);
            if (to.exists()) { if (to.isDirectory()) to.deleteRecursively(); else to.deleteFile(); }
            if (from.exists() && ! from.moveFileTo (to)) { error = tr ("Nepodarilo sa presunúť ") + rel; ok = false; }
        }
    tmp.deleteRecursively();
    if (job != nullptr) job->progress = 1.0;
    return ok;
}

bool extractAll (const File& arc, const File& dest, TaskJob* job, String& error)
{
    if (isZip (arc))
    {
        std::vector<ArcItem> items;
        if (! list (arc, items, error)) return false;
        StringArray top;
        for (auto& i : items) if (! i.path.contains ("/")) top.add (i.path);
        return extract (arc, top, {}, dest, job, error);
    }
    String out;
    const bool ok = runTar ({ "-xf", arc.getFullPathName(), "-C", dest.getFullPathName() }, out, error);
    if (job != nullptr) job->progress = 1.0;
    return ok;
}

bool createZip (const Array<File>& files, const File& baseFolder, const File& zip, TaskJob* job, String& error)
{
    std::vector<std::pair<File, String>> add;
    for (auto& f : files) collect (f, f.getRelativePathFrom (baseFolder).replaceCharacter ('\\', '/'), add);
    zip.deleteFile();
    return rebuildZip (zip, {}, add, job, error);
}

bool addToZip (const File& zip, const String& arcDir, const Array<File>& files, TaskJob* job, String& error)
{
    std::vector<std::pair<File, String>> add;
    const String prefix = arcDir.isEmpty() ? String() : arcDir + "/";
    for (auto& f : files) collect (f, prefix + f.getFileName(), add);
    return rebuildZip (zip, {}, add, job, error);
}

bool deleteFromZip (const File& zip, const StringArray& paths, String& error)
{
    return rebuildZip (zip, paths, {}, nullptr, error);
}
}
