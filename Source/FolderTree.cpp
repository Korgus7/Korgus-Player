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
    FolderItem (FolderTree& o, const File& f, const String& label = {}, bool special = false, bool favourite = false)
        : owner (o), file (f), name (label.isNotEmpty() ? label : f.getFileName()), isSpecial (special), isFavourite (favourite)
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

    void itemClicked (const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) owner.showItemMenu (file);
    }

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
        if (isFavourite)
        {
            g.setColour (col::gold);
            g.setFont (FontOptions (14.0f));
            g.drawText (tr ("★"), icon.toNearestInt().expanded (2), Justification::centred, false);
            g.setColour (isSelected() ? col::selText : col::text);
            g.setFont (FontOptions (13.5f));
            g.drawText (name, 25, 0, w - 27, h, Justification::centredLeft, true);
            return;
        }
        g.setColour (isSpecial ? col::gold.withAlpha (0.85f) : kindColour (Kind::Folder));
        if (getParentItem() != nullptr && getParentItem()->getParentItem() == nullptr && ! isSpecial)
            g.fillRoundedRectangle (icon.reduced (0, 2), 2.0f);     // disk
        else
        {
            g.fillRoundedRectangle (icon.withTrimmedTop (2), 2.0f);
            g.fillRoundedRectangle (icon.withWidth (7).withHeight (4), 1.5f);
        }
        g.setColour (isSelected() ? col::selText : col::text);
        g.setFont (FontOptions (13.5f));
        g.drawText (name, 25, 0, w - 27, h, Justification::centredLeft, true);
    }

private:
    FolderTree& owner;
    File file;
    String name;
    bool isSpecial = false, isFavourite = false;
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

    rebuildRoot();
}

void FolderTree::rebuildRoot()
{
    root->clearSubItems();
    for (auto& f : favorites)
        if (f.isDirectory())
            root->addSubItem (new FolderItem (*this, f, f.getFileName().isNotEmpty() ? f.getFileName() : f.getFullPathName(), true, true));

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
            root->addSubItem (new FolderItem (*this, dir, tr (f.label), true));
    }
    auto dl = File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads");
    if (dl.isDirectory())
        root->addSubItem (new FolderItem (*this, dl, tr ("Stiahnuté"), true));

    Array<File> roots;
    File::findFileSystemRoots (roots);
    for (auto& r : roots)
        root->addSubItem (new FolderItem (*this, r));

    root->setOpen (true);
}

void FolderTree::setFavorites (const Array<File>& favs)
{
    favorites = favs;
    rebuildRoot();
}

void FolderTree::showItemMenu (const File& f)
{
    PopupMenu m;
    const bool fav = favorites.contains (f);
    m.addItem (1, fav ? tr ("Odobrať z obľúbených") : tr ("Pridať do obľúbených ★"));
    if (fav && favorites.indexOf (f) > 0) m.addItem (2, tr ("Posunúť vyššie"));
    m.addItem (3, tr ("Ukázať v Prieskumníkovi"));
    Component::SafePointer<FolderTree> sp (this);
    m.showMenuAsync (PopupMenu::Options(), [sp, f, fav] (int r)
    {
        if (sp == nullptr) return;
        if (r == 1)
        {
            if (fav) sp->favorites.removeFirstMatchingValue (f);
            else sp->favorites.add (f);
        }
        else if (r == 2)
        {
            const int i = sp->favorites.indexOf (f);
            if (i > 0) sp->favorites.swap (i, i - 1);
        }
        else if (r == 3) { f.revealToUser(); return; }
        else return;
        // prestavať až po zavretí ponuky (položka stromu sa maže)
        MessageManager::callAsync ([sp]
        {
            if (sp == nullptr) return;
            sp->rebuildRoot();
            if (sp->onFavoritesChanged) sp->onFavoritesChanged (sp->favorites);
        });
    });
}

FolderTree::~FolderTree()
{
    tree.setRootItem (nullptr);
}

void FolderTree::resized() { tree.setBounds (getLocalBounds().withTrimmedLeft (10).withTrimmedTop (6)); }
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
