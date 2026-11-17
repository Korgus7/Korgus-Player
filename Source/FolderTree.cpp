#include "FolderTree.h"

namespace kp
{
static bool isWithin (const File& parent, const File& f)
{
    return f == parent || f.isAChildOf (parent);
}

class FolderItem : public TreeViewItem
{
public:
    FolderItem (FolderTree& o, const File& f, const String& label = {}, bool special = false)
        : owner (o), file (f), name (label.isNotEmpty() ? label : f.getFileName()), isSpecial (special)
    {
        if (name.isEmpty()) name = f.getFullPathName(); // disk, napr. "C:\"
    }

    const File& getFile() const { return file; }

    bool mightContainSubItems() override { return ! loaded || getNumSubItems() > 0; }
    String getUniqueName() const override { return (isSpecial ? "*" : "") + file.getFullPathName(); }
    int getItemHeight() const override { return 22; }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (isNowOpen) load();
    }

    void load (bool force = false)
    {
        if (loaded && ! force) return;
        loaded = true;
        clearSubItems();
        auto dirs = file.findChildFiles (File::findDirectories | File::ignoreHiddenFiles, false);
        std::sort (dirs.begin(), dirs.end(), [] (const File& a, const File& b)
                   { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        for (auto& d : dirs)
            addSubItem (new FolderItem (owner, d));
        treeHasChanged();
    }

    bool isLoaded() const { return loaded; }

    void itemSelectionChanged (bool isNowSelected) override
    {
        if (isNowSelected) owner.itemSelected (file);
    }

    void paintItem (Graphics& g, int w, int h) override
    {
        if (isSelected())
        {
            g.setColour (col::accent);
            g.fillRoundedRectangle (Rectangle<float> (0, 1, (float) w, (float) h - 2), radius);
        }
        // malý symbol priečinka / disku
        auto icon = Rectangle<float> (4, (float) h * 0.5f - 6, 15, 12);
        g.setColour (isSpecial ? col::gold.withAlpha (0.85f) : kindColour (Kind::Folder));
        if (getParentItem() != nullptr && getParentItem()->getParentItem() == nullptr && ! isSpecial)
            g.fillRoundedRectangle (icon.reduced (0, 2), 2.0f);     // disk
        else
        {
            g.fillRoundedRectangle (icon.withTrimmedTop (2), 2.0f);
            g.fillRoundedRectangle (icon.withWidth (7).withHeight (4), 1.5f);
        }
        g.setColour (isSelected() ? Colours::white : col::text);
        g.setFont (FontOptions (13.5f));
        g.drawText (name, 25, 0, w - 27, h, Justification::centredLeft, true);
    }

private:
    FolderTree& owner;
    File file;
    String name;
    bool isSpecial = false;
    bool loaded = false;
};

namespace
{
    class RootItem : public TreeViewItem
    {
    public:
        bool mightContainSubItems() override { return true; }
        String getUniqueName() const override { return "root"; }
    };
}

FolderTree::FolderTree()
{
    root = std::make_unique<RootItem>();
    tree.setRootItem (root.get());
    tree.setRootItemVisible (false);
    tree.setDefaultOpenness (false);
    tree.setIndentSize (14);
    tree.setColour (TreeView::backgroundColourId, col::panel);
    tree.setColour (TreeView::linesColourId, col::line);
    addAndMakeVisible (tree);

    struct Fav { File::SpecialLocationType t; const char* label; };
    const Fav favs[] = {
        { File::userDesktopDirectory,   "Plocha" },
        { File::userDocumentsDirectory, "Dokumenty" },
        { File::userPicturesDirectory,  "Obrázky" },
        { File::userMusicDirectory,     "Hudba" },
        { File::userMoviesDirectory,    "Videá" },
    };
    for (auto& f : favs)
    {
        auto dir = File::getSpecialLocation (f.t);
        if (dir.isDirectory())
            root->addSubItem (new FolderItem (*this, dir, String::fromUTF8 (f.label), true));
    }
    auto dl = File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads");
    if (dl.isDirectory())
        root->addSubItem (new FolderItem (*this, dl, String::fromUTF8 ("Stiahnuté"), true));

    Array<File> roots;
    File::findFileSystemRoots (roots);
    for (auto& r : roots)
        root->addSubItem (new FolderItem (*this, r));

    root->setOpen (true);
}

FolderTree::~FolderTree()
{
    tree.setRootItem (nullptr);
}

void FolderTree::resized() { tree.setBounds (getLocalBounds()); }
void FolderTree::paint (Graphics& g) { g.fillAll (col::panel); }

void FolderTree::itemSelected (const File& f)
{
    if (! suppress && onFolderSelected) onFolderSelected (f);
}

void FolderTree::reveal (const File& folder)
{
    // najdlhšia zhoda medzi koreňovými položkami (napr. Obrázky pred C:\)
    FolderItem* best = nullptr;
    for (int i = 0; i < root->getNumSubItems(); ++i)
        if (auto* it = dynamic_cast<FolderItem*> (root->getSubItem (i)))
            if (isWithin (it->getFile(), folder))
                if (best == nullptr || it->getFile().getFullPathName().length() > best->getFile().getFullPathName().length())
                    best = it;
    if (best == nullptr) return;

    FolderItem* cur = best;
    while (cur->getFile() != folder)
    {
        cur->setOpen (true);
        cur->load();
        FolderItem* next = nullptr;
        for (int i = 0; i < cur->getNumSubItems(); ++i)
            if (auto* it = dynamic_cast<FolderItem*> (cur->getSubItem (i)))
                if (isWithin (it->getFile(), folder)) { next = it; break; }
        if (next == nullptr) break;
        cur = next;
    }

    const ScopedValueSetter<bool> svs (suppress, true);
    cur->setSelected (true, true);
    tree.scrollToKeepItemVisible (cur);
}

FolderItem* FolderTree::findLoaded (const File& folder)
{
    std::function<FolderItem* (TreeViewItem*)> walk = [&] (TreeViewItem* parent) -> FolderItem*
    {
        for (int i = 0; i < parent->getNumSubItems(); ++i)
            if (auto* it = dynamic_cast<FolderItem*> (parent->getSubItem (i)))
            {
                if (it->getFile() == folder) return it;
                if (it->isLoaded() && isWithin (it->getFile(), folder))
                    if (auto* r = walk (it)) return r;
            }
        return nullptr;
    };
    return walk (root.get());
}

void FolderTree::refreshFolder (const File& folder)
{
    if (auto* it = findLoaded (folder))
        if (it->isLoaded())
        {
            const ScopedValueSetter<bool> svs (suppress, true);
            it->load (true);
        }
}
}
