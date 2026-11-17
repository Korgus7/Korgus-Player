#pragma once
#include "Common.h"

namespace kp
{
class FolderItem;

// Strom priečinkov vľavo: obľúbené priečinky a disky. Podpriečinky sa načítajú až pri otvorení.
class FolderTree : public Component
{
public:
    FolderTree();
    ~FolderTree() override;

    // Otvorí strom až k priečinku a označí ho (bez spätného volania onFolderSelected)
    void reveal (const File& folder);

    // Znova načíta podpriečinky daného priečinka (ak je v strome načítaný)
    void refreshFolder (const File& folder);

    std::function<void (const File&)> onFolderSelected;

    void resized() override;
    void paint (Graphics& g) override;

private:
    friend class FolderItem;
    void itemSelected (const File& f);
    FolderItem* findLoaded (const File& folder);

    TreeView tree;
    std::unique_ptr<TreeViewItem> root;
    bool suppress = false;
};
}
