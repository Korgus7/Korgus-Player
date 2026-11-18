#include "Transfer.h"

namespace kp
{
Transfer::Transfer (std::vector<Item> i, bool m) : Thread ("Prenos"), items (std::move (i)), move (m) {}

Transfer::~Transfer()
{
    cancelled = true;
    stopThread (10000);
}

File Transfer::rootOf (File f)
{
    while (f.getParentDirectory() != f) f = f.getParentDirectory();
    return f;
}

static int64 sizeOf (const File& f)
{
    if (! f.isDirectory()) return f.getSize();
    int64 s = 0;
    for (const auto& e : RangedDirectoryIterator (f, true, "*", File::findFiles | File::ignoreHiddenFiles))
        s += e.getFileSize();
    return s;
}

bool Transfer::copyFile (const File& from, const File& to)
{
    FileInputStream in (from);
    if (! in.openedOk()) return false;
    to.deleteFile();
    {
        FileOutputStream out (to);
        if (! out.openedOk()) return false;
        HeapBlock<char> buf (1 << 20);
        for (;;)
        {
            if (cancelled) break;
            const int n = in.read (buf, 1 << 20);
            if (n <= 0) break;
            if (! out.write (buf, (size_t) n)) { cancelled = cancelled.load(); error = tr ("Na disku nie je miesto alebo sa nedá zapisovať."); break; }
            done += n;
        }
        out.flush();
        if (cancelled || error.isNotEmpty() || out.getStatus().failed())
        {
            // nedokončená kópia preč
        }
        else
        {
            to.setLastModificationTime (from.getLastModificationTime());
            return true;
        }
    }
    to.deleteFile();
    return false;
}

bool Transfer::copyTree (const File& from, const File& to)
{
    if (! from.isDirectory()) return copyFile (from, to);
    if (! to.createDirectory()) return false;
    for (const auto& e : RangedDirectoryIterator (from, false, "*", File::findFilesAndDirectories | File::ignoreHiddenFiles))
    {
        if (cancelled) return false;
        if (! copyTree (e.getFile(), to.getChildFile (e.getFile().getFileName()))) return false;
    }
    return true;
}

void Transfer::run()
{
    int64 t = 0;
    for (auto& i : items) t += sizeOf (i.from);
    total = jmax ((int64) 1, t);
    for (size_t k = 0; k < items.size() && ! cancelled; ++k)
    {
        current = (int) k;
        auto& it = items[k];
        if (copyTree (it.from, it.to))
        {
            if (move && ! (it.from.isDirectory() ? it.from.deleteRecursively() : it.from.deleteFile()))
                error = tr ("Skopírované, ale originál sa nedal zmazať: ") + it.from.getFileName();
            completed.push_back (it);
        }
        else
        {
            if (it.to.isDirectory()) it.to.deleteRecursively();   // nedokončený priečinok
            if (! cancelled && error.isEmpty()) error = tr ("Nepodarilo sa skopírovať ") + it.from.getFileName();
        }
    }
    finished = true;
}
}
