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

    viewButton.setTooltip (String::fromUTF8 ("Prepnúť mriežku a zoznam (V)"));
    viewButton.onClick = [this] { grid.toggleMode(); grid.grabKeyboardFocus(); };
    addAndMakeVisible (viewButton);

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

    preview.onFocusBack = [this] { grid.grabKeyboardFocus(); };
    thumbs.onReady = [this] { grid.repaint(); preview.repaint(); if (fullscreen) fullscreen->repaint(); };
    audioInfo.onReady = [this] { grid.repaint(); };
    imageCache.onLoaded = [this] (const String& path)
    {
        preview.imageLoaded (path);
        if (fullscreen) fullscreen->imageLoaded (path);
    };

    tree.onFolderSelected = [this] (const File& f) { openFolder (f); };
    grid.onSelectionChanged = [this] (const File& f) { selectionChanged (f); };
    grid.onOpenFolder = [this] (const File& f) { openFolder (f); grid.grabKeyboardFocus(); };
    grid.onGoUp = [this] { goUp(); };
    grid.onActivateFile = [this] (const File& f)
    {
        const auto k = kindOf (f);
        if (k == Kind::Image || k == Kind::Svg) openFullscreen();
        else if (k == Kind::Font || k == Kind::Pdf || k == Kind::Model || k == Kind::Cad) openDocFullscreen();
        else f.startAsProcess();
    };
    grid.onModeChanged = [this] { updateViewButton(); };
    grid.onMarksChanged = [this] { updateFolderInfo(); };
    updateViewButton();

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

    status.showMessage (String::fromUTF8 ("Enter = obrázok na celú obrazovku · V = mriežka/zoznam · Šípky = listovanie · 1–9 = presun do cieľa · Ctrl+1–9 = nastaviť cieľ · Ctrl+Z = späť · Medzerník = prehrať/zastaviť"));
}

MainComponent::~MainComponent()
{
    closeFullscreen();
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
    g.drawText (grid.getFolder().getFullPathName(), top.withTrimmedLeft (52).withTrimmedRight (120), Justification::centredLeft, true);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (topH);
    upButton.setBounds (top.removeFromLeft (44).reduced (6, 5));
    viewButton.setBounds (top.removeFromRight (110).reduced (6, 5));
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
    String info = String (files) + String::fromUTF8 (" súborov · ") + String (dirs) + String::fromUTF8 (" priečinkov");
    if (const int m = grid.getNumMarked(); m > 0) info << String::fromUTF8 (" · označené: ") << m;
    status.setFolderInfo (info);
}

int MainComponent::nextImageIndex (int from, int dir) const
{
    for (int i = from + dir; i >= 0 && i < grid.getNumFiles(); i += dir)
    {
        const auto k = grid.getKindAt (i);
        if (k == Kind::Image || k == Kind::Svg) return i;
    }
    return -1;
}

int MainComponent::nextOfKind (int from, int dir, Kind k) const
{
    for (int i = from + dir; i >= 0 && i < grid.getNumFiles(); i += dir)
        if (grid.getKindAt (i) == k) return i;
    return -1;
}

void MainComponent::selectionChanged (const File& f)
{
    const int i = grid.getSelectedIndex();

    if (docFullscreen != nullptr)
    {
        const auto k = docFullscreen->getKind();
        if (f == File() || kindOf (f) != k)
        {
            int n = nextOfKind (i, 1, k);
            if (n < 0) n = nextOfKind (i, -1, k);
            if (n < 0) { closeFullscreen(); preview.show (f); return; }
            grid.selectIndex (n);
            return;
        }
        int pos = 0, total = 0;
        for (int j = 0; j < grid.getNumFiles(); ++j)
            if (grid.getKindAt (j) == k) { ++total; if (j <= i) ++pos; }
        docFullscreen->setFile (f, pos, total);
        return;   // ukážku netreba načítavať dvakrát
    }

    if (fullscreen != nullptr)
    {
        // na celej obrazovke len obrázky: po presune skoč na najbližší obrázok
        const auto k = kindOf (f);
        if (f == File() || (k != Kind::Image && k != Kind::Svg))
        {
            int n = nextImageIndex (i, 1);
            if (n < 0) n = nextImageIndex (i, -1);
            if (n < 0) { closeFullscreen(); preview.show (f); return; }
            grid.selectIndex (n);
            return;
        }
        int pos = 0, total = 0;
        for (int j = 0; j < grid.getNumFiles(); ++j)
        {
            const auto kj = grid.getKindAt (j);
            if (kj == Kind::Image || kj == Kind::Svg) { ++total; if (j <= i) ++pos; }
        }
        fullscreen->setFile (f, pos, total);
    }

    preview.show (f);

    // susedné obrázky dopredu (dva v každom smere)
    for (int dir : { 1, -1 })
    {
        int j = i;
        for (int n = 0; n < 2; ++n)
        {
            j = nextImageIndex (j, dir);
            if (j < 0) break;
            imageCache.prefetch (grid.getFileAt (j));
        }
    }
}

// ---------------------------------------------------------------- celá obrazovka

void MainComponent::openFullscreen()
{
    if (fullscreen != nullptr) return;
    const auto k = kindOf (grid.getSelectedFile());
    if (k != Kind::Image && k != Kind::Svg) return;

    fullscreen = std::make_unique<FullscreenView> (imageCache, thumbs);
    fullscreen->onStep = [this] (int dir) { stepFullscreen (dir); };
    fullscreen->onClose = [this]
    {
        // zavrieť až po skončení obsluhy klávesu
        Component::SafePointer<MainComponent> sp (this);
        MessageManager::callAsync ([sp] { if (sp != nullptr) sp->closeFullscreen(); });
    };
    fullscreen->onOtherKey = [this] (const KeyPress& key) { return handleSortKey (key); };

    fullscreen->addToDesktop (0);
    fullscreen->setVisible (true);
    Desktop::getInstance().setKioskModeComponent (fullscreen.get(), false);
    fullscreen->grabKeyboardFocus();
    selectionChanged (grid.getSelectedFile());
}

void MainComponent::openDocFullscreen()
{
    if (docFullscreen != nullptr || fullscreen != nullptr) return;
    const auto k = kindOf (grid.getSelectedFile());
    docFullscreen = std::make_unique<FullscreenDoc> (k);
    docFullscreen->onStep = [this, k] (int dir)
    {
        const int cur = grid.getSelectedIndex();
        int t = dir <= -100000 ? nextOfKind (-1, 1, k) : dir >= 100000 ? nextOfKind (grid.getNumFiles(), -1, k)
                                                                        : nextOfKind (cur, dir > 0 ? 1 : -1, k);
        if (t >= 0 && t != cur) grid.selectIndex (t);
    };
    docFullscreen->onClose = [this]
    {
        Component::SafePointer<MainComponent> sp (this);
        MessageManager::callAsync ([sp] { if (sp != nullptr) sp->closeFullscreen(); });
    };
    docFullscreen->onOtherKey = [this] (const KeyPress& key) { return handleSortKey (key); };
    docFullscreen->addToDesktop (0);
    docFullscreen->setVisible (true);
    Desktop::getInstance().setKioskModeComponent (docFullscreen.get(), false);
    docFullscreen->grabKeyboardFocus();
    selectionChanged (grid.getSelectedFile());
}

void MainComponent::closeFullscreen()
{
    if (fullscreen == nullptr && docFullscreen == nullptr) return;
    Desktop::getInstance().setKioskModeComponent (nullptr);
    fullscreen.reset();
    if (docFullscreen != nullptr)
    {
        docFullscreen.reset();
        preview.show ({});
        preview.show (grid.getSelectedFile());
    }
    if (auto* top = getTopLevelComponent())
        top->toFront (true);
    grid.grabKeyboardFocus();
}

void MainComponent::stepFullscreen (int dir)
{
    const int cur = grid.getSelectedIndex();
    int target = -1;
    if (dir <= -100000) target = nextImageIndex (-1, 1);
    else if (dir >= 100000) target = nextImageIndex (grid.getNumFiles(), -1);
    else target = nextImageIndex (cur, dir > 0 ? 1 : -1);
    if (target >= 0 && target != cur) grid.selectIndex (target);
}

void MainComponent::updateViewButton()
{
    viewButton.setButtonText (grid.getMode() == FileGrid::Mode::Grid ? String::fromUTF8 ("Mriežka")
                                                                      : String::fromUTF8 ("Zoznam"));
}

void MainComponent::message (const String& m, bool error)
{
    status.showMessage (m, error);
    if (fullscreen) fullscreen->showMessage (m, error);
    if (docFullscreen) docFullscreen->showMessage (m, error);
}

// ---------------------------------------------------------------- presun

void MainComponent::moveSelectedTo (int slot)
{
    const auto files = grid.getMarkedFiles();
    if (files.isEmpty()) return;
    const auto dir = targets[slot];
    const String n (slot + 1);

    if (dir == File())
    {
        message (String::fromUTF8 ("Cieľ ") + n + String::fromUTF8 (" nie je nastavený. Klikni dole na ") + n
                 + String::fromUTF8 (" alebo stlač Ctrl+") + n + String::fromUTF8 (" v cieľovom priečinku."), true);
        return;
    }
    if (! dir.isDirectory())
    {
        message (String::fromUTF8 ("Priečinok ") + dir.getFullPathName() + String::fromUTF8 (" neexistuje."), true);
        return;
    }

    Batch batch;
    String lastError;
    bool anyDir = false;
    for (auto& f : files)
    {
        if (f.getParentDirectory() == dir) { lastError = String::fromUTF8 ("Súbor už je v priečinku ") + dir.getFileName() + "."; continue; }
        if (f.isDirectory() && (dir == f || dir.isAChildOf (f))) { lastError = String::fromUTF8 ("Priečinok sa nedá presunúť sám do seba."); continue; }

        auto dest = dir.getChildFile (f.getFileName());
        if (dest.exists())
            dest = dir.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), true);

        preview.release (f);
        const bool wasDir = f.isDirectory();
        if (! f.moveFileTo (dest))
        {
            lastError = String::fromUTF8 ("Nepodarilo sa presunúť ") + f.getFileName() + String::fromUTF8 (" (možno je otvorený v inom programe).");
            continue;
        }
        anyDir = anyDir || wasDir;
        thumbs.forget (f);
        imageCache.forget (f);
        audioInfo.forget (f);
        batch.push_back ({ f, dest });
        grid.removeFile (f);
    }
    grid.clearMarks();

    if (batch.empty())
    {
        if (lastError.isNotEmpty()) message (lastError, true);
        return;
    }
    undoStack.push_back (batch);
    if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
    updateFolderInfo();
    if (anyDir)
    {
        tree.refreshFolder (batch.front().from.getParentDirectory());
        tree.refreshFolder (dir);
    }

    String m;
    if (batch.size() == 1)
    {
        const auto& mv = batch.front();
        m = mv.from.getFileName() + String::fromUTF8 (" → ") + dir.getFileName()
          + (mv.to.getFileName() != mv.from.getFileName() ? String::fromUTF8 (" (ako ") + mv.to.getFileName() + ")" : String());
    }
    else
        m = String ((int) batch.size()) + String::fromUTF8 (" súborov → ") + dir.getFileName();
    m << String::fromUTF8 (" · Ctrl+Z vráti");
    if (lastError.isNotEmpty()) m << String::fromUTF8 (" · niektoré sa nepresunuli");
    message (m, lastError.isNotEmpty());
}

void MainComponent::undo()
{
    if (undoStack.empty())
    {
        message (String::fromUTF8 ("Nie je čo vrátiť."));
        return;
    }
    const auto batch = undoStack.back();
    undoStack.pop_back();

    File lastBack;
    int done = 0;
    bool anyDir = false;
    for (auto it = batch.rbegin(); it != batch.rend(); ++it)
    {
        const auto& m = *it;
        if (! m.to.exists()) continue;
        auto back = m.from;
        if (back.exists())
            back = m.from.getParentDirectory().getNonexistentChildFile (m.from.getFileNameWithoutExtension(), m.from.getFileExtension(), true);
        preview.release (m.to);
        if (! m.to.moveFileTo (back)) continue;
        thumbs.forget (m.to);
        anyDir = anyDir || back.isDirectory();
        lastBack = back;
        ++done;
    }
    if (done == 0)
    {
        message (String::fromUTF8 ("Nepodarilo sa vrátiť (súbor už v cieli nie je)."), true);
        return;
    }
    if (anyDir)
    {
        tree.refreshFolder (batch.front().to.getParentDirectory());
        tree.refreshFolder (lastBack.getParentDirectory());
    }
    if (lastBack.getParentDirectory() == grid.getFolder())
    {
        grid.reload();
        grid.selectFile (batch.front().from.exists() ? batch.front().from : lastBack);
        updateFolderInfo();
    }
    else
        openFolder (lastBack.getParentDirectory(), lastBack);
    message (done == 1 ? String::fromUTF8 ("Vrátené: ") + lastBack.getFileName()
                       : String::fromUTF8 ("Vrátených ") + String (done) + String::fromUTF8 (" súborov"));
}

// ---------------------------------------------------------------- kôš a premenovanie

void MainComponent::deleteMarked()
{
    const auto files = grid.getMarkedFiles();
    if (files.isEmpty()) return;
    int done = 0;
    String failed;
    bool anyDir = false;
    for (auto& f : files)
    {
        preview.release (f);
        const bool wasDir = f.isDirectory();
        if (f.moveToTrash())
        {
            anyDir = anyDir || wasDir;
            thumbs.forget (f);
            imageCache.forget (f);
            audioInfo.forget (f);
            grid.removeFile (f);
            ++done;
        }
        else failed = f.getFileName();
    }
    grid.clearMarks();
    updateFolderInfo();
    if (anyDir) tree.refreshFolder (grid.getFolder());
    if (done == 0)
    {
        message (String::fromUTF8 ("Nepodarilo sa presunúť do koša: ") + failed, true);
        return;
    }
    message ((done == 1 ? files.getFirst().getFileName() : String (done) + String::fromUTF8 (" súborov"))
             + String::fromUTF8 (" → Kôš (obnovíš ho z Koša vo Windows)")
             + (failed.isNotEmpty() ? String::fromUTF8 (" · niektoré sa nepodarilo") : String()), failed.isNotEmpty());
}

void MainComponent::renameSelected()
{
    const auto f = grid.getSelectedFile();
    if (f == File() || renameWindow != nullptr) return;

    renameWindow = std::make_unique<AlertWindow> (String::fromUTF8 ("Premenovať"), String(), MessageBoxIconType::NoIcon, this);
    renameWindow->addTextEditor ("name", f.getFileName(), String::fromUTF8 ("Nový názov:"));
    renameWindow->addButton (String::fromUTF8 ("Premenovať"), 1, KeyPress (KeyPress::returnKey));
    renameWindow->addButton (String::fromUTF8 ("Zrušiť"), 0, KeyPress (KeyPress::escapeKey));
    // okno vnútri programu, nie samostatné – vždy dostane klávesnicu
    addAndMakeVisible (*renameWindow);
    renameWindow->setSize (jmin (520, getWidth() - 40), renameWindow->getHeight());
    if (auto* ed = renameWindow->getTextEditor ("name"))
        ed->setSize (renameWindow->getWidth() - 50, ed->getHeight());
    renameWindow->setCentrePosition (getLocalBounds().getCentre());

    Component::SafePointer<MainComponent> sp (this);
    renameWindow->enterModalState (true, ModalCallbackFunction::create ([sp, f] (int result)
    {
        if (sp == nullptr) return;
        String newName;
        if (auto* ed = sp->renameWindow->getTextEditor ("name")) newName = ed->getText().trim();
        MessageManager::callAsync ([sp] { if (sp != nullptr) { sp->renameWindow.reset(); sp->grid.grabKeyboardFocus(); } });
        if (result != 1 || newName.isEmpty() || newName == f.getFileName()) return;

        if (newName.containsAnyOf ("\\/:*?\"<>|"))
        {
            sp->message (String::fromUTF8 ("Názov nesmie obsahovať znaky \\ / : * ? \" < > |"), true);
            return;
        }
        const auto dest = f.getSiblingFile (newName);
        if (dest.exists() && ! dest.getFullPathName().equalsIgnoreCase (f.getFullPathName()))
        {
            sp->message (String::fromUTF8 ("Súbor s týmto názvom už existuje."), true);
            return;
        }
        sp->preview.release (f);
        if (! f.moveFileTo (dest))
        {
            sp->message (String::fromUTF8 ("Nepodarilo sa premenovať (možno je otvorený v inom programe)."), true);
            return;
        }
        sp->thumbs.forget (f);
        sp->imageCache.forget (f);
        sp->audioInfo.forget (f);
        if (dest.isDirectory()) sp->tree.refreshFolder (dest.getParentDirectory());
        sp->grid.reload();
        sp->grid.selectFile (dest);
        sp->message (f.getFileName() + String::fromUTF8 (" → ") + dest.getFileName());
    }), false);

    if (auto* ed = renameWindow->getTextEditor ("name"))
    {
        const int base = f.isDirectory() ? f.getFileName().length() : f.getFileNameWithoutExtension().length();
        ed->grabKeyboardFocus();
        ed->setHighlightedRegion ({ 0, base });
    }
}

// ---------------------------------------------------------------- ciele 1–9

void MainComponent::setTarget (int slot, const File& dir)
{
    targets.set (slot, dir);
    status.setTargets (targets);
    saveTargets();
    if (dir != File())
        message (String::fromUTF8 ("Cieľ ") + String (slot + 1) + " = " + dir.getFullPathName());
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

bool MainComponent::handleSortKey (const KeyPress& key)
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
    return false;
}

bool MainComponent::keyPressed (const KeyPress& key)
{
    const int k = key.getKeyCode();
    const auto mods = key.getModifiers();

    if (handleSortKey (key)) return true;
    if (k == KeyPress::spaceKey) { preview.togglePlay(); return true; }
    if (k == KeyPress::deleteKey && ! mods.isAnyModifierKeyDown()) { deleteMarked(); return true; }
    if (k == KeyPress::F2Key) { renameSelected(); return true; }
    if ((k == 'v' || k == 'V') && ! mods.isAnyModifierKeyDown()) { grid.toggleMode(); return true; }
    if (k == KeyPress::F11Key)
    {
        const auto kk = kindOf (grid.getSelectedFile());
        if (kk == Kind::Font || kk == Kind::Pdf || kk == Kind::Model || kk == Kind::Cad) openDocFullscreen();
        else openFullscreen();
        return true;
    }
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
