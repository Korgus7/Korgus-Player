#include "MainComponent.h"

namespace kp
{
static constexpr int topH = 36, bottomH = 34;

MainComponent::MainComponent (const String& commandLine)
{
    PropertiesFile::Options o;
    o.applicationName = "Korgus Player";
    o.filenameSuffix = ".settings";
    o.folderName = "Korgus Player";
    o.osxLibrarySubFolder = "Application Support";
    props.setStorageParameters (o);

    setWantsKeyboardFocus (true);

    upButton.setButtonText (String::fromUTF8 ("↑"));
    upButton.setTooltip (String::fromUTF8 ("O priečinok vyššie (Backspace)"));
    upButton.onClick = [this] { goUp(); };
    addAndMakeVisible (upButton);

    addAndMakeVisible (tree);
    addAndMakeVisible (bar1);
    addAndMakeVisible (grid);
    addAndMakeVisible (bar2);
    addAndMakeVisible (preview);
    addAndMakeVisible (status);

    layout.setItemLayout (0, 150, 700, -0.20);
    layout.setItemLayout (1, 5, 5, 5);
    layout.setItemLayout (2, 220, -0.9, -0.45);
    layout.setItemLayout (3, 5, 5, 5);
    layout.setItemLayout (4, 220, -0.9, -0.35);

    thumbs.onReady = [this] { grid.repaint(); preview.repaint(); };

    tree.onFolderSelected = [this] (const File& f) { openFolder (f); };
    grid.onSelectionChanged = [this] (const File& f) { selectionChanged (f); };
    grid.onOpenFolder = [this] (const File& f) { openFolder (f); grid.grabKeyboardFocus(); };
    grid.onGoUp = [this] { goUp(); };
    grid.onOpenExternally = [] (const File& f) { f.startAsProcess(); };

    status.onSlotClicked = [this] (int i) { chooseTarget (i); };
    status.onSlotCleared = [this] (int i) { setTarget (i, {}); };

    // cieľové priečinky 1–9
    targets.resize (9);
    for (int i = 0; i < 9; ++i)
    {
        const auto p = settings().getValue ("target" + String (i + 1));
        if (p.isNotEmpty()) targets.set (i, File (p));
    }
    status.setTargets (targets);

    // čo otvoriť: súbor/priečinok z príkazového riadka, inak posledný priečinok
    File start;
    const auto arg = commandLine.unquoted().trim();
    if (arg.isNotEmpty() && File::isAbsolutePath (arg)) start = File (arg);
    if (! start.exists()) start = File (settings().getValue ("lastFolder"));
    if (! start.exists()) start = File::getSpecialLocation (File::userPicturesDirectory);

    setSize (1400, 860);
    if (start.existsAsFile()) openFolder (start.getParentDirectory(), start);
    else openFolder (start, File (settings().getValue ("lastFile")));

    status.showMessage (String::fromUTF8 ("Šípky = listovanie · 1–9 = presun do cieľa · Ctrl+1–9 = nastaviť cieľ · Ctrl+Z = späť · Medzerník = prehrať/zastaviť"));
}

MainComponent::~MainComponent()
{
    settings().setValue ("lastFolder", grid.getFolder().getFullPathName());
    settings().setValue ("lastFile", grid.getSelectedFile().getFullPathName());
    props.saveIfNeeded();
}

void MainComponent::paint (Graphics& g)
{
    g.fillAll (col::bg);
    auto top = getLocalBounds().removeFromTop (topH);
    g.setColour (col::panel);
    g.fillRect (top);
    g.setColour (col::line);
    g.fillRect (top.removeFromBottom (1));
    g.setColour (col::text);
    g.setFont (FontOptions (14.0f));
    g.drawText (grid.getFolder().getFullPathName(), top.withTrimmedLeft (52).withTrimmedRight (10), Justification::centredLeft, true);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (topH);
    upButton.setBounds (top.removeFromLeft (44).reduced (6, 5));
    status.setBounds (r.removeFromBottom (bottomH));
    Component* comps[] = { &tree, &bar1, &grid, &bar2, &preview };
    layout.layOutComponents (comps, 5, r.getX(), r.getY(), r.getWidth(), r.getHeight(), false, true);
}

// ---------------------------------------------------------------- navigácia

void MainComponent::openFolder (const File& folder, const File& toSelect)
{
    if (! folder.isDirectory()) return;
    grid.setFolder (folder, toSelect);
    tree.reveal (folder);
    settings().setValue ("lastFolder", folder.getFullPathName());
    updateFolderInfo();
    repaint (0, 0, getWidth(), topH);
}

void MainComponent::goUp()
{
    const auto cur = grid.getFolder();
    const auto parent = cur.getParentDirectory();
    if (parent != cur) openFolder (parent, cur);
}

void MainComponent::updateFolderInfo()
{
    int files = 0, dirs = 0;
    for (int i = 0; i < grid.getNumFiles(); ++i)
        (grid.getFileAt (i).isDirectory() ? dirs : files)++;
    status.setFolderInfo (String (files) + String::fromUTF8 (" súborov · ") + String (dirs) + String::fromUTF8 (" priečinkov"));
}

void MainComponent::selectionChanged (const File& f)
{
    preview.show (f);
    const int i = grid.getSelectedIndex();
    Array<File> around;
    for (int d : { 1, -1, 2, -2 })
    {
        auto n = grid.getFileAt (i + d);
        if (n != File()) around.add (n);
    }
    preview.prefetch (around);
}

// ---------------------------------------------------------------- presun

void MainComponent::moveSelectedTo (int slot)
{
    const auto f = grid.getSelectedFile();
    if (f == File()) return;
    const auto dir = targets[slot];
    const String n (slot + 1);

    if (dir == File())
    {
        status.showMessage (String::fromUTF8 ("Cieľ ") + n + String::fromUTF8 (" nie je nastavený. Klikni dole na ") + n
                            + String::fromUTF8 (" alebo stlač Ctrl+") + n + String::fromUTF8 (" v cieľovom priečinku."), true);
        return;
    }
    if (! dir.isDirectory())
    {
        status.showMessage (String::fromUTF8 ("Priečinok ") + dir.getFullPathName() + String::fromUTF8 (" neexistuje."), true);
        return;
    }
    if (f.getParentDirectory() == dir)
    {
        status.showMessage (String::fromUTF8 ("Súbor už je v priečinku ") + dir.getFileName() + ".", true);
        return;
    }
    if (f.isDirectory() && (dir == f || dir.isAChildOf (f)))
    {
        status.showMessage (String::fromUTF8 ("Priečinok sa nedá presunúť sám do seba."), true);
        return;
    }

    auto dest = dir.getChildFile (f.getFileName());
    if (dest.exists())
        dest = dir.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), true);

    preview.release (f);
    if (! f.moveFileTo (dest))
    {
        status.showMessage (String::fromUTF8 ("Nepodarilo sa presunúť ") + f.getFileName()
                            + String::fromUTF8 (" (možno je otvorený v inom programe)."), true);
        return;
    }

    thumbs.forget (f);
    undoStack.push_back ({ f, dest });
    if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
    grid.removeFile (f);
    updateFolderInfo();
    if (f.isDirectory() || dest.isDirectory())
    {
        tree.refreshFolder (f.getParentDirectory());
        tree.refreshFolder (dir);
    }
    status.showMessage (f.getFileName() + String::fromUTF8 (" → ") + dir.getFileName()
                        + (dest.getFileName() != f.getFileName() ? String::fromUTF8 (" (ako ") + dest.getFileName() + ")" : String())
                        + String::fromUTF8 (" · Ctrl+Z vráti"));
}

void MainComponent::undo()
{
    if (undoStack.empty())
    {
        status.showMessage (String::fromUTF8 ("Nie je čo vrátiť."));
        return;
    }
    const auto m = undoStack.back();
    undoStack.pop_back();

    if (! m.to.exists())
    {
        status.showMessage (String::fromUTF8 ("Súbor ") + m.to.getFileName() + String::fromUTF8 (" už v cieli nie je."), true);
        return;
    }
    auto back = m.from;
    if (back.exists())
        back = m.from.getParentDirectory().getNonexistentChildFile (m.from.getFileNameWithoutExtension(), m.from.getFileExtension(), true);

    preview.release (m.to);
    if (! m.to.moveFileTo (back))
    {
        status.showMessage (String::fromUTF8 ("Nepodarilo sa vrátiť ") + m.to.getFileName() + ".", true);
        return;
    }
    thumbs.forget (m.to);
    if (back.isDirectory())
    {
        tree.refreshFolder (m.to.getParentDirectory());
        tree.refreshFolder (back.getParentDirectory());
    }
    if (back.getParentDirectory() == grid.getFolder())
    {
        grid.reload();
        grid.selectFile (back);
        updateFolderInfo();
    }
    else
        openFolder (back.getParentDirectory(), back);
    status.showMessage (String::fromUTF8 ("Vrátené: ") + back.getFileName());
}

// ---------------------------------------------------------------- ciele 1–9

void MainComponent::setTarget (int slot, const File& dir)
{
    targets.set (slot, dir);
    status.setTargets (targets);
    saveTargets();
    if (dir != File())
        status.showMessage (String::fromUTF8 ("Cieľ ") + String (slot + 1) + " = " + dir.getFullPathName());
}

void MainComponent::saveTargets()
{
    for (int i = 0; i < 9; ++i)
        settings().setValue ("target" + String (i + 1), targets[i].getFullPathName());
    props.saveIfNeeded();
}

void MainComponent::chooseTarget (int slot)
{
    const auto start = targets[slot] != File() ? targets[slot] : grid.getFolder();
    chooser = std::make_unique<FileChooser> (String::fromUTF8 ("Cieľový priečinok ") + String (slot + 1), start);
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                          [this, slot] (const FileChooser& fc)
                          {
                              const auto r = fc.getResult();
                              if (r.isDirectory()) setTarget (slot, r);
                          });
}

// ---------------------------------------------------------------- klávesy

bool MainComponent::keyPressed (const KeyPress& key)
{
    const int k = key.getKeyCode();
    const auto mods = key.getModifiers();

    int digit = -1;
    if (k >= '1' && k <= '9') digit = k - '1';
    else if (k >= KeyPress::numberPad1 && k <= KeyPress::numberPad9) digit = k - KeyPress::numberPad1;

    if (digit >= 0 && ! mods.isAltDown())
    {
        if (mods.isCtrlDown()) setTarget (digit, grid.getFolder());
        else moveSelectedTo (digit);
        return true;
    }
    if (mods.isCtrlDown() && (k == 'z' || k == 'Z')) { undo(); return true; }
    if (k == KeyPress::spaceKey) { preview.togglePlay(); return true; }
    if (k == KeyPress::F5Key)
    {
        tree.refreshFolder (grid.getFolder());
        grid.reload();
        updateFolderInfo();
        return true;
    }
    if (k == KeyPress::backspaceKey || (mods.isAltDown() && k == KeyPress::upKey)) { goUp(); return true; }
    return false;
}
}
