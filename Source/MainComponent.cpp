#include "MainComponent.h"
#include "ImageEdit.h"
#include "WinImaging.h"
#include "PhotoInfo.h"

namespace kp
{
namespace { bool explorerIntegrated(); void setExplorerIntegration (bool on); }
static constexpr int topH = 36, bottomH = 34, playerH = 42;

MainComponent::MainComponent (const String& commandLine)
{
    PropertiesFile::Options o;
    o.applicationName = "Korgus Player";
    o.filenameSuffix = ".settings";
    o.folderName = "Korgus Player";
    o.osxLibrarySubFolder = "Application Support";
    props.setStorageParameters (o);

    setWantsKeyboardFocus (true);

    upButton.setButtonText (tr ("↑"));
    upButton.setTooltip (tr ("O priečinok vyššie (Backspace)"));
    upButton.onClick = [this] { goUp(); };
    addAndMakeVisible (upButton);

    viewButton.setTooltip (tr ("Prepnúť mriežku a zoznam (V)"));
    viewButton.onClick = [this] { grid.toggleMode(); grid.grabKeyboardFocus(); };
    addAndMakeVisible (viewButton);
    settingsButton.setButtonText (String::fromUTF8 ("☰"));
    settingsButton.setTooltip (tr ("Nastavenia"));
    settingsButton.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible (settingsButton);

    addAndMakeVisible (tree);
    addAndMakeVisible (bar1);
    addAndMakeVisible (grid);
    addAndMakeVisible (bar2);
    addAndMakeVisible (preview);
    addAndMakeVisible (status);
    addAndMakeVisible (playerBar);

    searchBox.setTextToShowWhenEmpty (tr ("Hľadať (Ctrl+F)"), col::textDim);
    searchBox.setColour (TextEditor::backgroundColourId, col::panelHi);
    searchBox.setColour (TextEditor::outlineColourId, col::line);
    searchBox.setColour (TextEditor::focusedOutlineColourId, col::accentHi);
    searchBox.setColour (TextEditor::textColourId, col::text);
    searchBox.setIndents (8, 4);
    searchBox.onTextChange = [this] { grid.setFilter (searchBox.getText().trim(), typeBox.getSelectedItemIndex()); updateFolderInfo(); };
    searchBox.onEscapeKey = [this] { searchBox.clear(); grid.setFilter ({}, typeBox.getSelectedItemIndex()); grid.grabKeyboardFocus(); };
    searchBox.onReturnKey = [this] { grid.grabKeyboardFocus(); };
    addAndMakeVisible (searchBox);

    typeBox.addItemList ({ tr ("Všetko"), tr ("Obrázky"), tr ("Zvuk"), tr ("Video"),
                           tr ("Dokumenty"), tr ("3D a CAD"), tr ("Priečinky"),
                           tr ("S hodnotením ★"), tr ("So štítkom") }, 1);
    typeBox.setSelectedItemIndex (0, dontSendNotification);
    typeBox.setTooltip (tr ("Zobraziť len vybraný typ súborov"));
    typeBox.setWantsKeyboardFocus (false);
    typeBox.onChange = [this] { grid.setFilter (searchBox.getText().trim(), typeBox.getSelectedItemIndex()); updateFolderInfo(); grid.grabKeyboardFocus(); };
    addAndMakeVisible (typeBox);

    sortBox.addItemList ({ tr ("Názov A–Z"), tr ("Názov Z–A"), tr ("Najnovšie"),
                           tr ("Najstaršie"), tr ("Dátum fotenia"), tr ("Najväčšie"),
                           tr ("Typ súboru") }, 1);
    sortBox.setTooltip (tr ("Triedenie"));
    sortBox.setWantsKeyboardFocus (false);
    sortBox.setSelectedItemIndex (jlimit (0, 6, settings().getIntValue ("sort", 0)), dontSendNotification);
    sortBox.onChange = [this] { settings().setValue ("sort", sortBox.getSelectedItemIndex()); applySortBox(); grid.grabKeyboardFocus(); };
    addAndMakeVisible (sortBox);
    grid.onSortChanged = [this] { sortBox.setSelectedItemIndex (-1, dontSendNotification); sortBox.setText (tr ("Vlastné"), dontSendNotification); };

    slideshowButton.background = true;
    slideshowButton.onClick = [this] { startSlideshow(); };
    addAndMakeVisible (slideshowButton);

    // prehrávač: nastavenia, sledovanie skladby
    player.repeat = (AudioPlayer::Repeat) jlimit (0, 2, settings().getIntValue ("repeat", 1));
    player.shuffle = settings().getBoolValue ("shuffle", false);
    player.setVolume ((float) settings().getDoubleValue ("volume", 0.85));
    playerBar.update();
    playerBar.onSettingsChanged = [this]
    {

        settings().setValue ("repeat", (int) player.repeat);
        settings().setValue ("shuffle", player.shuffle);
        settings().setValue ("volume", player.getVolume());
    };
    playerBar.onInfo = [this] (const String& m) { message (m); };
    playerBar.onShowTrack = [this] (const File& f)
    {
        if (f.getParentDirectory() != grid.getFolder()) openFolder (f.getParentDirectory(), f);
        else grid.selectFile (f);
        grid.grabKeyboardFocus();
    };
    player.onStateChanged = [this] { playerBar.update(); preview.playerChanged(); };
    player.onTrackChanged = [this] (const File& prev, const File& now)
    {
        // výber ide so skladbou, ak naň používateľ práve pozerá
        if (prev != File() && grid.getSelectedFile() == prev && now.getParentDirectory() == grid.getFolder() && fullscreen == nullptr)
            grid.selectFile (now);
    };

    layout.setItemLayout (0, 150, 700, -0.20);
    layout.setItemLayout (1, 5, 5, 5);
    layout.setItemLayout (2, 220, -0.9, -0.45);
    layout.setItemLayout (3, 5, 5, 5);
    layout.setItemLayout (4, 220, -0.9, -0.35);

    preview.onFocusBack = [this] { grid.grabKeyboardFocus(); };
    preview.onRotate = [this] (bool cw) { rotateMarked (cw); grid.grabKeyboardFocus(); };
    preview.onResize = [this] { resizeDialog(); };
    preview.onEdit = [this] { openEditor(); };
    preview.onVideoFullscreen = [this] { openVideoFullscreen(); };
    preview.onSnapshot = [this] (const File& out)
    {
        if (! out.existsAsFile()) { message (tr ("Snímku sa nepodarilo uložiť."), true); return; }
        const auto keep = grid.getSelectedFile();
        grid.reload();
        grid.selectFile (keep);
        message (tr ("Snímka uložená: ") + out.getFileName());
    };
    thumbs.onReady = [this] { grid.repaint(); preview.repaint(); if (fullscreen) fullscreen->repaint(); };
    audioInfo.onReady = [this] { grid.repaint(); };
    imageCache.onLoaded = [this] (const String& path)
    {
        preview.imageLoaded (path);
        if (fullscreen) fullscreen->imageLoaded (path);
    };

    tree.onFolderSelected = [this] (const File& f) { openFolder (f); };
    {
        Array<File> favs;
        for (auto& p : StringArray::fromLines (settings().getValue ("favorites")))
            if (p.isNotEmpty()) favs.add (File (p));
        tree.setFavorites (favs);
    }
    tree.onFavoritesChanged = [this] (const Array<File>& favs)
    {
        StringArray paths;
        for (auto& f : favs) paths.add (f.getFullPathName());
        settings().setValue ("favorites", paths.joinIntoString ("\n"));
        message (tr ("Obľúbené priečinky uložené"));
        tree.reveal (grid.getFolder());
    };
    grid.onSelectionChanged = [this] (const File& f) { selectionChanged (f); };
    grid.onOpenFolder = [this] (const File& f) { openFolder (f); grid.grabKeyboardFocus(); };
    grid.onGoUp = [this] { goUp(); };
    grid.onActivateFile = [this] (const File& f)
    {
        const auto k = kindOf (f);
        if (k == Kind::Image || k == Kind::Svg) openFullscreen();
        else if (isDocKind (k) && k != Kind::App) openDocFullscreen();
        else if (k == Kind::Video && VideoView::isAvailable()) openVideoFullscreen();
        else f.startAsProcess();
    };
    grid.onModeChanged = [this] { updateViewButton(); };
    grid.onMarksChanged = [this] { updateFolderInfo(); };
    grid.setMetaStore (&meta);
    preview.setMetaStore (&meta);
    preview.onRating = [this] (int r) { setRatingMarked (r); };
    preview.onLabel = [this] (int l) { setLabelMarked (l); };
    grid.onContextMenu = [this] (Point<int> p) { showContextMenu (p); };
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

    applySortBox();
    if (explorerIntegrated()) setExplorerIntegration (true);   // obnoviť cestu k exe (mohol sa premenovať/presunúť)
    setSize (1400, 860);
    if (start.existsAsFile()) openFolder (start.getParentDirectory(), start);
    else openFolder (start, File (settings().getValue ("lastFile")));

    status.showMessage (tr ("Enter = obrázok na celú obrazovku · V = mriežka/zoznam · Šípky = listovanie · 1–9 = presun do cieľa · Ctrl+1–9 = nastaviť cieľ · Ctrl+Z = späť · Medzerník = prehrať/zastaviť"));
}

MainComponent::~MainComponent()
{
    *alive = false;
    editPool.removeAllJobs (true, 10000);
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
    g.drawText (grid.getFolder().getFullPathName(), top.withTrimmedLeft (52).withTrimmedRight (260 + 294 + jlimit (140, 260, getWidth() / 6)), Justification::centredLeft, true);
}

void MainComponent::resized()
{
    if (editor) editor->setBounds (getLocalBounds());
    auto r = getLocalBounds();
    auto top = r.removeFromTop (topH);
    upButton.setBounds (top.removeFromLeft (44).reduced (6, 5));
    settingsButton.setBounds (top.removeFromRight (44).reduced (6, 5));
    viewButton.setBounds (top.removeFromRight (110).reduced (4, 5));
    slideshowButton.setBounds (top.removeFromRight (130).reduced (6, 5));
    sortBox.setBounds (top.removeFromRight (130).reduced (4, 5));
    typeBox.setBounds (top.removeFromRight (120).reduced (4, 5));
    searchBox.setBounds (top.removeFromRight (jlimit (140, 260, getWidth() / 6)).reduced (4, 5));
    status.setBounds (r.removeFromBottom (bottomH));
    playerBar.setBounds (r.removeFromBottom (playerH));
    Component* comps[] = { &tree, &bar1, &grid, &bar2, &preview };
    layout.layOutComponents (comps, 5, r.getX(), r.getY(), r.getWidth(), r.getHeight(), false, true);
}

// ---------------------------------------------------------------- navigácia

void MainComponent::openFolder (const File& folder, const File& toSelect)
{
    if (! folder.isDirectory()) return;
    if (folder != grid.getFolder()) searchBox.clear();
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
    String info = String (files) + tr (" súborov · ") + String (dirs) + tr (" priečinkov");
    if (const int m = grid.getNumMarked(); m > 0) info << tr (" · označené: ") << m;
    if (grid.isFiltered()) info << tr (" · filter (z ") << grid.getNumAll() << ")";
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

Array<File> MainComponent::audioPlaylist() const
{
    Array<File> list;
    for (int i = 0; i < grid.getNumFiles(); ++i)
        if (grid.getKindAt (i) == Kind::Audio) list.add (grid.getFileAt (i));
    return list;
}

void MainComponent::startSlideshow()
{
    int i = grid.getSelectedIndex();
    const auto k = grid.getKindAt (i);
    if (k != Kind::Image && k != Kind::Svg)
    {
        i = nextImageIndex (-1, 1);
        if (i < 0) { message (tr ("V priečinku nie sú obrázky na prezentáciu."), true); return; }
        grid.selectIndex (i);
    }
    openFullscreen();
    if (fullscreen) fullscreen->setSlideshow (true);
}

static String batchName (const String& pattern, const File& f, int number, int digits)
{
    Time t = f.getLastModificationTime();
    const auto pi = readPhotoInfo (f);
    if (pi.hasTaken) t = pi.taken;
    String n = pattern;
    n = n.replace ("{nazov}", f.getFileNameWithoutExtension())
         .replace ("{cislo}", String (number).paddedLeft ('0', digits))
         .replace ("{datum}", t.formatted ("%Y-%m-%d"))
         .replace ("{cas}", t.formatted ("%H-%M-%S"))
         .replace ("{priecinok}", f.getParentDirectory().getFileName());
    n = n.removeCharacters ("\\/:*?\"<>|").trim();
    return (n.isEmpty() ? f.getFileNameWithoutExtension() : n) + f.getFileExtension().toLowerCase();
}

void MainComponent::batchRenameDialog()
{
    const auto files = grid.getMarkedFiles();
    if (files.isEmpty() || batchWindow != nullptr) return;
    batchWindow = std::make_unique<AlertWindow> (tr ("Hromadné premenovanie: ") + String (files.size()) + tr (" súborov"),
                                                 tr ("Značky: {datum} {cas} {cislo} {nazov} {priecinok}"), MessageBoxIconType::NoIcon, this);
    batchWindow->addTextEditor ("pattern", settings().getValue ("batchPattern", "{datum}_{cislo}"), tr ("Vzor názvu"));
    batchWindow->addTextEditor ("start", "1", tr ("Číslovať od"));
    batchPreview = std::make_unique<Label>();
    batchPreview->setSize (420, 60);
    batchPreview->setColour (Label::textColourId, col::gold);
    batchWindow->addCustomComponent (batchPreview.get());
    batchWindow->addButton (tr ("Premenovať"), 1, KeyPress (KeyPress::returnKey));
    batchWindow->addButton (tr ("Zrušiť"), 0, KeyPress (KeyPress::escapeKey));

    auto refresh = [this, files]
    {
        if (batchWindow == nullptr) return;
        const auto pattern = batchWindow->getTextEditorContents ("pattern");
        const int start = batchWindow->getTextEditorContents ("start").getIntValue();
        const int digits = jmax (2, String (start + files.size()).length());
        String p = tr ("Ukážka:\n");
        for (int i = 0; i < jmin (3, files.size()); ++i) p << batchName (pattern, files[i], start + i, digits) << "\n";
        batchPreview->setText (p.trimEnd(), dontSendNotification);
    };
    for (auto name : { "pattern", "start" })
        if (auto* ed = batchWindow->getTextEditor (name)) ed->onTextChange = refresh;
    refresh();

    addAndMakeVisible (*batchWindow);
    batchWindow->setCentrePosition (getLocalBounds().getCentre());
    Component::SafePointer<MainComponent> sp (this);
    batchWindow->enterModalState (true, ModalCallbackFunction::create ([sp, files] (int result)
    {
        if (sp == nullptr) return;
        const auto pattern = sp->batchWindow->getTextEditorContents ("pattern");
        const int start = sp->batchWindow->getTextEditorContents ("start").getIntValue();
        MessageManager::callAsync ([sp] { if (sp != nullptr) { sp->batchWindow.reset(); sp->batchPreview.reset(); sp->grid.grabKeyboardFocus(); } });
        if (result != 1 || pattern.trim().isEmpty()) return;
        sp->settings().setValue ("batchPattern", pattern);

        const int digits = jmax (2, String (start + files.size()).length());
        // dva kroky (cez dočasné mená), aby sa mená nepobili
        Batch batch;
        std::vector<std::pair<File, File>> temp;
        for (int i = 0; i < files.size(); ++i)
        {
            const auto& f = files.getReference (i);
            sp->preview.release (f);
            const auto t = f.getSiblingFile ("~korgus-" + String (i) + "-" + String (Random::getSystemRandom().nextInt (1000000)) + f.getFileExtension());
            if (f.moveFileTo (t)) temp.push_back ({ f, t });
        }
        int done = 0;
        for (size_t i = 0; i < temp.size(); ++i)
        {
            const auto& [orig, t] = temp[i];
            auto name = batchName (pattern, orig, start + (int) i, digits);
            // dátum treba čítať z dočasného súboru (pôvodný už neexistuje)
            name = batchName (pattern, t, start + (int) i, digits).replace (t.getFileNameWithoutExtension(), orig.getFileNameWithoutExtension());
            auto dest = orig.getSiblingFile (name);
            if (dest.exists()) dest = orig.getParentDirectory().getNonexistentChildFile (File::createLegalFileName (dest.getFileNameWithoutExtension()), dest.getFileExtension(), true);
            if (t.moveFileTo (dest))
            {
                batch.push_back ({ orig, dest });
                sp->meta.moved (orig, dest);
                sp->thumbs.forget (orig);
                ++done;
            }
            else t.moveFileTo (orig);
        }
        if (! batch.empty()) sp->undoStack.push_back (batch);
        sp->grid.reload();
        sp->updateFolderInfo();
        sp->message (tr ("Premenované: ") + String (done) + tr (" · Ctrl+Z vráti"), done < files.size());
    }), false);
}

namespace
{
    const char* explorerKeys[] = { "Directory\\shell\\KorgusPlayer", "Directory\\Background\\shell\\KorgusPlayer", "*\\shell\\KorgusPlayer" };

    bool explorerIntegrated()
    {
       #if JUCE_WINDOWS
        return WindowsRegistry::keyExists (String ("HKEY_CURRENT_USER\\Software\\Classes\\") + explorerKeys[0]);
       #else
        return false;
       #endif
    }

    void setExplorerIntegration (bool on)
    {
       #if JUCE_WINDOWS
        const auto exe = File::getSpecialLocation (File::currentExecutableFile).getFullPathName();
        for (int i = 0; i < 3; ++i)
        {
            const String key = String ("HKEY_CURRENT_USER\\Software\\Classes\\") + explorerKeys[i];
            if (on)
            {
                WindowsRegistry::setValue (key + "\\", tr ("Otvoriť v Korgus Player"));
                WindowsRegistry::setValue (key + "\\Icon", "\"" + exe + "\",0");
                WindowsRegistry::setValue (key + "\\command\\", "\"" + exe + "\" \"" + (i == 1 ? "%V" : "%1") + "\"");
            }
            else
            {
                WindowsRegistry::deleteKey (key + "\\command");
                WindowsRegistry::deleteKey (key);
            }
        }
       #else
        ignoreUnused (on);
       #endif
    }
}

void MainComponent::showSettingsMenu()
{
    PopupMenu m;
    m.addSectionHeader (tr ("Vzhľad"));
    m.addItem (1, tr ("Tmavá téma"), true, ! col::light);
    m.addItem (2, tr ("Svetlá téma"), true, col::light);
    m.addSectionHeader (tr ("Jazyk / Language"));
    m.addItem (3, "Slovensky", true, ! english);
    m.addItem (4, "English", true, english);
    m.addSectionHeader (tr ("Windows"));
    m.addItem (5, tr ("„Otvoriť v Korgus Player“ v Prieskumníkovi (pravý klik)"), true, explorerIntegrated());
    m.addSeparator();
    m.addItem (9, tr ("O programe Korgus Player"));
    Component::SafePointer<MainComponent> sp (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&settingsButton), [sp] (int r)
    {
        if (sp == nullptr || r == 0) return;
        auto& s = sp->settings();
        if (r == 1 || r == 2)
        {
            if ((r == 2) == col::light) return;
            s.setValue ("lightTheme", r == 2);
            s.saveIfNeeded();
            if (restartUI) restartUI();
        }
        else if (r == 3 || r == 4)
        {
            if ((r == 4) == english) return;
            s.setValue ("lang", r == 4 ? "en" : "sk");
            s.saveIfNeeded();
            if (restartUI) restartUI();
        }
        else if (r == 5)
        {
            const bool on = ! explorerIntegrated();
            setExplorerIntegration (on);
            sp->message (on ? tr ("Pridané do Prieskumníka: pravý klik na priečinok alebo súbor → Otvoriť v Korgus Player")
                            : tr ("Odstránené z Prieskumníka"));
        }
        else if (r == 9)
        {
            sp->message ("Korgus Player " + JUCEApplication::getInstance()->getApplicationVersion() + tr (" · zadarmo · cami.sk"));
        }
    });
}

void MainComponent::openEditor()
{
    const auto f = grid.getSelectedFile();
    if (! edit::canResize (f)) { message (tr ("Upravovať sa dajú obrázky (JPG, PNG, …)."), true); return; }
    closeFullscreen();
    editor = std::make_unique<ImageEditor> (f);
    Component::SafePointer<MainComponent> sp (this);
    editor->onClose = [sp] { MessageManager::callAsync ([sp] { if (sp != nullptr) { sp->editor.reset(); sp->grid.grabKeyboardFocus(); } }); };
    editor->onMessage = [sp] (const String& m, bool err) { if (sp != nullptr) sp->message (m, err); };
    editor->onSaved = [sp] (const File& saved, bool overwritten)
    {
        if (sp == nullptr) return;
        if (overwritten) sp->fileChanged (saved);
        else { sp->grid.reload(); sp->grid.selectFile (saved); sp->updateFolderInfo(); }
        sp->message (tr ("Uložené: ") + saved.getFileName());
        MessageManager::callAsync ([sp] { if (sp != nullptr) { sp->editor.reset(); sp->grid.grabKeyboardFocus(); } });
    };
    addAndMakeVisible (*editor);
    editor->setBounds (getLocalBounds());
    editor->grabKeyboardFocus();
}

void MainComponent::findDuplicates()
{
    // rovnaká veľkosť → rovnaký obsah (SHA-256); ponechá sa prvý súbor zo skupiny
    Array<File> files;
    for (int i = 0; i < grid.getNumFiles(); ++i)
        if (! grid.getFileAt (i).isDirectory()) files.add (grid.getFileAt (i));
    message (tr ("Hľadám duplikáty v ") + String (files.size()) + tr (" súboroch…"));
    auto aliveFlag = alive;
    editPool.addJob ([this, aliveFlag, files]
    {
        std::map<int64, Array<File>> bySize;
        for (auto& f : files) bySize[f.getSize()].add (f);
        Array<File> dups;
        int groups = 0;
        int64 bytes = 0;
        for (auto& [size, list] : bySize)
        {
            if (list.size() < 2 || size == 0) continue;
            std::map<String, Array<File>> byHash;
            for (auto& f : list)
            {
                FileInputStream in (f);
                if (in.openedOk()) byHash[SHA256 (in).toHexString()].add (f);
            }
            for (auto& [h, same] : byHash)
                if (same.size() > 1)
                {
                    ++groups;
                    for (int i = 1; i < same.size(); ++i) { dups.add (same[i]); bytes += size; }
                }
        }
        MessageManager::callAsync ([this, aliveFlag, dups, groups, bytes]
        {
            if (! *aliveFlag) return;
            if (dups.isEmpty()) { message (tr ("Žiadne duplikáty – všetky súbory sú rôzne.")); return; }
            grid.markFiles (dups);
            message (String (dups.size()) + tr (" duplikátov v ") + String (groups) + tr (" skupinách (") + sizeText (bytes)
                     + tr (") je označených – originál ostal neoznačený. Del ich presunie do koša."));
        });
    });
}

void MainComponent::pickSlideshowMusic()
{
    chooser = std::make_unique<FileChooser> (tr ("Priečinok s hudbou pre prezentáciu"),
                                             File (settings().getValue ("slideshowMusic", File::getSpecialLocation (File::userMusicDirectory).getFullPathName())));
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this] (const FileChooser& fc)
    {
        const auto dir = fc.getResult();
        if (! dir.isDirectory()) return;
        settings().setValue ("slideshowMusic", dir.getFullPathName());
        Array<File> list;
        for (const auto& de : RangedDirectoryIterator (dir, false, "*", File::findFiles))
            if (kindOf (de.getFile()) == Kind::Audio) list.add (de.getFile());
        list.sort();
        if (list.isEmpty()) { message (tr ("V priečinku nie je hudba."), true); return; }
        player.play (player.shuffle ? list[Random::getSystemRandom().nextInt (list.size())] : list[0], list);
        message (tr ("Hudba: ") + dir.getFileName() + " (" + String (list.size()) + tr (" skladieb)"));
    });
}

void MainComponent::setRatingMarked (int stars)
{
    const auto files = grid.getMarkedFiles();
    for (auto& f : files) meta.setRating (f, stars);
    grid.repaint();
    preview.repaint();
    message (stars > 0 ? tr ("Hodnotenie: ") + String::repeatedString (tr ("★"), stars)
                       : tr ("Hodnotenie zrušené"));
}

void MainComponent::setLabelMarked (int label)
{
    for (auto& f : grid.getMarkedFiles()) meta.setLabel (f, label);
    grid.repaint();
    preview.repaint();
    message (tr ("Štítok: ") + MetaStore::labelName (label));
}

void MainComponent::applySortBox()
{
    using S = FileGrid::SortBy;
    switch (sortBox.getSelectedItemIndex())
    {
        case 1: grid.setSortExplicit (S::Name, false); break;
        case 2: grid.setSortExplicit (S::Date, false); break;
        case 3: grid.setSortExplicit (S::Date, true); break;
        case 4: grid.setSortExplicit (S::Taken, true); break;
        case 5: grid.setSortExplicit (S::Size, false); break;
        case 6: grid.setSortExplicit (S::Type, true); break;
        default: grid.setSortExplicit (S::Name, true); break;
    }
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

    // zvuk: prehrať (zoznam = zvuky v priečinku); tá istá skladba sa nespúšťa odznova
    if (kindOf (f) == Kind::Audio && f != player.getCurrent())
        player.play (f, audioPlaylist());

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
    fullscreen->onRotate = [this] (bool cw) { rotateMarked (cw); };
    fullscreen->onPickMusic = [this] { pickSlideshowMusic(); };

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

void MainComponent::openVideoFullscreen()
{
    if (videoFull != nullptr) return;
    const auto f = grid.getSelectedFile();
    const double pos = preview.getVideoPosition();
    preview.show ({});   // zastaví video v náhľade
    videoFull = std::make_unique<VideoFullscreen> (f, pos);
    Component::SafePointer<MainComponent> sp (this);
    videoFull->onClose = [sp] { MessageManager::callAsync ([sp] { if (sp != nullptr) sp->closeFullscreen(); }); };
    videoFull->onSnapshot = [sp] (const File& out)
    {
        if (sp == nullptr) return;
        sp->message (out.existsAsFile() ? tr ("Snímka uložená: ") + out.getFileName()
                                        : tr ("Snímku sa nepodarilo uložiť."), ! out.existsAsFile());
    };
    videoFull->addToDesktop (0);
    videoFull->setVisible (true);
    Desktop::getInstance().setKioskModeComponent (videoFull.get(), false);
    videoFull->start_();
}

void MainComponent::closeFullscreen()
{
    if (videoFull != nullptr)
    {
        Desktop::getInstance().setKioskModeComponent (nullptr);
        videoFull.reset();
        preview.show (grid.getSelectedFile());
        if (auto* top = getTopLevelComponent()) top->toFront (true);
        grid.grabKeyboardFocus();
        grid.reload();
        return;
    }
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
    else if (dir == 0)
    {
        // náhodný obrázok (prezentácia)
        Array<int> imgs;
        for (int i = 0; i < grid.getNumFiles(); ++i)
            if (i != cur && (grid.getKindAt (i) == Kind::Image || grid.getKindAt (i) == Kind::Svg)) imgs.add (i);
        if (! imgs.isEmpty()) target = imgs[Random::getSystemRandom().nextInt (imgs.size())];
    }
    else
    {
        target = nextImageIndex (cur, dir > 0 ? 1 : -1);
        // prezentácia na konci pokračuje od začiatku
        if (target < 0 && dir > 0 && fullscreen != nullptr && fullscreen->isSlideshow()) target = nextImageIndex (-1, 1);
    }
    if (target >= 0 && target != cur) grid.selectIndex (target);
}

void MainComponent::updateViewButton()
{
    viewButton.setButtonText (grid.getMode() == FileGrid::Mode::Grid ? tr ("Mriežka")
                                                                      : tr ("Zoznam"));
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
        message (tr ("Cieľ ") + n + tr (" nie je nastavený. Klikni dole na ") + n
                 + tr (" alebo stlač Ctrl+") + n + tr (" v cieľovom priečinku."), true);
        return;
    }
    if (! dir.isDirectory())
    {
        message (tr ("Priečinok ") + dir.getFullPathName() + tr (" neexistuje."), true);
        return;
    }

    Batch batch;
    String lastError;
    bool anyDir = false;
    for (auto& f : files)
    {
        if (f.getParentDirectory() == dir) { lastError = tr ("Súbor už je v priečinku ") + dir.getFileName() + "."; continue; }
        if (f.isDirectory() && (dir == f || dir.isAChildOf (f))) { lastError = tr ("Priečinok sa nedá presunúť sám do seba."); continue; }

        auto dest = dir.getChildFile (f.getFileName());
        if (dest.exists())
            dest = dir.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), true);

        preview.release (f);
        const bool wasDir = f.isDirectory();
        if (! f.moveFileTo (dest))
        {
            lastError = tr ("Nepodarilo sa presunúť ") + f.getFileName() + tr (" (možno je otvorený v inom programe).");
            continue;
        }
        anyDir = anyDir || wasDir;
        thumbs.forget (f);
        imageCache.forget (f);
        audioInfo.forget (f);
        meta.moved (f, dest);
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
        m = mv.from.getFileName() + tr (" → ") + dir.getFileName()
          + (mv.to.getFileName() != mv.from.getFileName() ? tr (" (ako ") + mv.to.getFileName() + ")" : String());
    }
    else
        m = String ((int) batch.size()) + tr (" súborov → ") + dir.getFileName();
    m << tr (" · Ctrl+Z vráti");
    if (lastError.isNotEmpty()) m << tr (" · niektoré sa nepresunuli");
    message (m, lastError.isNotEmpty());
}

void MainComponent::undo()
{
    if (undoStack.empty())
    {
        message (tr ("Nie je čo vrátiť."));
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
        meta.moved (m.to, back);
        anyDir = anyDir || back.isDirectory();
        lastBack = back;
        ++done;
    }
    if (done == 0)
    {
        message (tr ("Nepodarilo sa vrátiť (súbor už v cieli nie je)."), true);
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
    message (done == 1 ? tr ("Vrátené: ") + lastBack.getFileName()
                       : tr ("Vrátených ") + String (done) + tr (" súborov"));
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
        message (tr ("Nepodarilo sa presunúť do koša: ") + failed, true);
        return;
    }
    message ((done == 1 ? files.getFirst().getFileName() : String (done) + tr (" súborov"))
             + tr (" → Kôš (obnovíš ho z Koša vo Windows)")
             + (failed.isNotEmpty() ? tr (" · niektoré sa nepodarilo") : String()), failed.isNotEmpty());
}

void MainComponent::renameSelected()
{
    const auto f = grid.getSelectedFile();
    if (f == File() || renameWindow != nullptr) return;

    renameWindow = std::make_unique<AlertWindow> (tr ("Premenovať"), String(), MessageBoxIconType::NoIcon, this);
    renameWindow->addTextEditor ("name", f.getFileName(), tr ("Nový názov:"));
    renameWindow->addButton (tr ("Premenovať"), 1, KeyPress (KeyPress::returnKey));
    renameWindow->addButton (tr ("Zrušiť"), 0, KeyPress (KeyPress::escapeKey));
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
            sp->message (tr ("Názov nesmie obsahovať znaky \\ / : * ? \" < > |"), true);
            return;
        }
        const auto dest = f.getSiblingFile (newName);
        if (dest.exists() && ! dest.getFullPathName().equalsIgnoreCase (f.getFullPathName()))
        {
            sp->message (tr ("Súbor s týmto názvom už existuje."), true);
            return;
        }
        sp->preview.release (f);
        if (! f.moveFileTo (dest))
        {
            sp->message (tr ("Nepodarilo sa premenovať (možno je otvorený v inom programe)."), true);
            return;
        }
        sp->thumbs.forget (f);
        sp->imageCache.forget (f);
        sp->audioInfo.forget (f);
        sp->meta.moved (f, dest);
        if (dest.isDirectory()) sp->tree.refreshFolder (dest.getParentDirectory());
        sp->grid.reload();
        sp->grid.selectFile (dest);
        sp->message (f.getFileName() + tr (" → ") + dest.getFileName());
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
        message (tr ("Cieľ ") + String (slot + 1) + " = " + dir.getFullPathName());
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
    chooser = std::make_unique<FileChooser> (tr ("Cieľový priečinok ") + String (slot + 1), start);
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                          [this, slot] (const FileChooser& fc)
                          {
                              const auto r = fc.getResult();
                              if (r.isDirectory()) setTarget (slot, r);
                          });
}


// ---------------------------------------------------------------- úpravy obrázkov

void MainComponent::fileChanged (const File& f)
{
    thumbs.forget (f);
    imageCache.forget (f);
    if (grid.getSelectedFile() == f)
    {
        if (fullscreen) fullscreen->refresh();
        preview.reload();
    }
    grid.repaint();
}

void MainComponent::rotateMarked (bool clockwise)
{
    Array<File> files;
    for (auto& f : grid.getMarkedFiles())
        if (edit::canRotate (f)) files.add (f);
    if (files.isEmpty())
    {
        message (tr ("Otáčať sa dajú zatiaľ len obrázky JPG a PNG."), true);
        return;
    }
    for (auto& f : files) preview.release (f);
    auto aliveFlag = alive;
    editPool.addJob ([this, aliveFlag, files, clockwise]
    {
        int ok = 0;
        String err;
        for (auto& f : files)
        {
            String e;
            if (edit::rotate (f, clockwise, e))
            {
                ++ok;
                MessageManager::callAsync ([this, aliveFlag, f] { if (*aliveFlag) fileChanged (f); });
            }
            else err = e;
        }
        MessageManager::callAsync ([this, aliveFlag, ok, err, clockwise, n = files.size()]
        {
            if (! *aliveFlag) return;
            if (ok == 0) { message (err, true); return; }
            message ((clockwise ? tr ("Otočené doprava") : tr ("Otočené doľava"))
                     + (n > 1 ? tr (": ") + String (ok) + tr (" obrázkov") : String())
                     + (err.isNotEmpty() ? tr (" · niektoré sa nepodarilo: ") + err : String()), err.isNotEmpty());
        });
    });
}

void MainComponent::resizeDialog()
{
    Array<File> files;
    for (auto& f : grid.getMarkedFiles())
        if (edit::canResize (f)) files.add (f);
    if (files.isEmpty() || resizeWindow != nullptr)
    {
        if (files.isEmpty()) message (tr ("Označ obrázky, ktorým chceš zmeniť veľkosť."), true);
        return;
    }

    const String title = files.size() == 1 ? tr ("Zmeniť veľkosť: ") + files[0].getFileName()
                                           : tr ("Zmeniť veľkosť: ") + String (files.size()) + tr (" obrázkov");
    resizeWindow = std::make_unique<AlertWindow> (title, String(), MessageBoxIconType::NoIcon, this);
    const StringArray sizes { "3840 px", "2560 px", "1920 px", "1600 px", "1280 px", "1024 px", "800 px", "640 px", "50 %", "25 %" };
    resizeWindow->addComboBox ("size", sizes, tr ("Dlhšia strana"));
    resizeWindow->addComboBox ("mode", { tr ("Kópia vedľa originálu (foto_1920.jpg)"),
                                         tr ("Kópia do podpriečinka „zmensene“"),
                                         tr ("Kópia do priečinka, ktorý vyberiem…"),
                                         tr ("Prepísať originál") },
                               tr ("Uloženie"));
    if (auto* c = resizeWindow->getComboBoxComponent ("size")) c->setSelectedItemIndex (jlimit (0, sizes.size() - 1, settings().getIntValue ("resizeSize", 2)));
    if (auto* c = resizeWindow->getComboBoxComponent ("mode")) c->setSelectedItemIndex (settings().getIntValue ("resizeMode", 0));
    resizeWindow->addButton (tr ("Zmeniť"), 1, KeyPress (KeyPress::returnKey));
    resizeWindow->addButton (tr ("Zrušiť"), 0, KeyPress (KeyPress::escapeKey));
    addAndMakeVisible (*resizeWindow);
    resizeWindow->setCentrePosition (getLocalBounds().getCentre());

    Component::SafePointer<MainComponent> sp (this);
    resizeWindow->enterModalState (true, ModalCallbackFunction::create ([sp, files] (int result)
    {
        if (sp == nullptr) return;
        int sizeIdx = 2, modeIdx = 0;
        if (auto* c = sp->resizeWindow->getComboBoxComponent ("size")) sizeIdx = c->getSelectedItemIndex();
        if (auto* c = sp->resizeWindow->getComboBoxComponent ("mode")) modeIdx = c->getSelectedItemIndex();
        MessageManager::callAsync ([sp] { if (sp != nullptr) { sp->resizeWindow.reset(); sp->grid.grabKeyboardFocus(); } });
        if (result != 1) return;
        sp->settings().setValue ("resizeSize", sizeIdx);
        sp->settings().setValue ("resizeMode", modeIdx);

        static const int px[] = { 3840, 2560, 1920, 1600, 1280, 1024, 800, 640, 0, 0 };
        const int longSide = px[jlimit (0, 9, sizeIdx)];
        const int percent = sizeIdx == 8 ? 50 : (sizeIdx == 9 ? 25 : 0);

        auto run = [sp, files, longSide, percent, modeIdx] (const File& targetDir)
        {
            if (sp == nullptr) return;
            const bool overwrite = modeIdx == 3;
            for (auto& f : files) sp->preview.release (f);
            sp->message (tr ("Mením veľkosť…"));
            auto aliveFlag = sp->alive;
            auto* self = sp.getComponent();
            sp->editPool.addJob ([self, aliveFlag, files, longSide, percent, overwrite, targetDir]
            {
                int ok = 0;
                String err;
                for (int i = 0; i < files.size(); ++i)
                {
                    const auto& f = files.getReference (i);
                    const bool canOverwrite = overwrite && f.hasFileExtension ("jpg;jpeg;jpe;jfif;png");
                    if (! canOverwrite && targetDir != File()) targetDir.createDirectory();
                    const auto dst = canOverwrite ? f : edit::resizedName (f, longSide, percent, targetDir);
                    String e;
                    if (edit::resize (f, dst, longSide, percent, e)) ++ok;
                    else err = e;
                    MessageManager::callAsync ([self, aliveFlag, i, n = files.size(), dst, canOverwrite]
                    {
                        if (! *aliveFlag) return;
                        self->message (tr ("Mením veľkosť ") + String (i + 1) + " / " + String (n) + tr ("…"));
                        if (canOverwrite) self->fileChanged (dst);
                    });
                }
                MessageManager::callAsync ([self, aliveFlag, ok, err, overwrite, targetDir]
                {
                    if (! *aliveFlag) return;
                    if (! overwrite) { self->grid.reload(); self->updateFolderInfo(); }
                    if (targetDir != File()) self->tree.refreshFolder (targetDir.getParentDirectory());
                    if (ok == 0) { self->message (err, true); return; }
                    self->message (tr ("Hotovo: ") + String (ok) + tr (ok == 1 ? " obrázok" : (ok < 5 ? " obrázky" : " obrázkov"))
                                   + (overwrite ? String() : (targetDir != File() ? tr (" → ") + targetDir.getFullPathName()
                                                                                  : tr (" uložené ako kópie")))
                                   + (err.isNotEmpty() ? tr (" · chyba: ") + err : String()), err.isNotEmpty());
                });
            });
        };

        if (modeIdx == 1) run (files[0].getParentDirectory().getChildFile ("zmensene"));
        else if (modeIdx == 2)
        {
            const File last (sp->settings().getValue ("resizeFolder"));
            sp->chooser = std::make_unique<FileChooser> (tr ("Kam uložiť zmenšené obrázky"),
                                                         last.isDirectory() ? last : files[0].getParentDirectory());
            sp->chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                                      [sp, run] (const FileChooser& fc)
                                      {
                                          const auto dir = fc.getResult();
                                          if (sp == nullptr || ! dir.isDirectory()) return;
                                          sp->settings().setValue ("resizeFolder", dir.getFullPathName());
                                          run (dir);
                                      });
        }
        else run ({});
    }), false);
}

void MainComponent::showContextMenu (Point<int> screenPos)
{
    const auto files = grid.getMarkedFiles();
    if (files.isEmpty()) return;
    const auto f = files.getFirst();
    bool anyRotate = false, anyResize = false;
    for (auto& x : files) { anyRotate = anyRotate || edit::canRotate (x); anyResize = anyResize || edit::canResize (x); }

    PopupMenu m;
    if (files.size() > 1) m.addSectionHeader (String (files.size()) + tr (" označených súborov"));
    bool anyAudio = false;
    for (auto& x : files) anyAudio = anyAudio || kindOf (x) == Kind::Audio;
    if (anyAudio)
    {
        m.addItem (20, tr ("Pridať do fronty\tQ"));
        m.addSeparator();
    }
    m.addItem (1, tr ("Otočiť doprava\tR"), anyRotate);
    m.addItem (2, tr ("Otočiť doľava\tL"), anyRotate);
    m.addItem (3, tr ("Zmeniť veľkosť…\tCtrl+R"), anyResize);
    m.addItem (11, tr ("Upraviť (orezať, jas, kontrast)…\tE"), files.size() == 1 && anyResize);
    m.addSeparator();
    m.addItem (4, tr ("Premenovať\tF2"), files.size() == 1);
    m.addItem (9, tr ("Hromadne premenovať…\tCtrl+F2"));
    m.addItem (5, tr ("Do koša\tDel"));
    m.addSeparator();
    PopupMenu stars, labels;
    for (int i = 5; i >= 1; --i) stars.addItem (200 + i, String::repeatedString (tr ("★"), i) + tr ("\tAlt+") + String (i));
    stars.addItem (200, tr ("Bez hodnotenia\tAlt+0"));
    for (int i = 1; i <= MetaStore::numLabels; ++i)
    {
        Image dot (Image::ARGB, 14, 14, true);
        { Graphics dg (dot); dg.setColour (MetaStore::labelColour (i)); dg.fillEllipse (1, 1, 12, 12); }
        labels.addItem (PopupMenu::Item (MetaStore::labelName (i)).setID (300 + i).setImage (std::make_unique<DrawableImage> (dot)));
    }
    labels.addItem (300, MetaStore::labelName (0));
    m.addSubMenu (tr ("Hodnotenie"), stars);
    m.addSubMenu (tr ("Farebný štítok"), labels);
    m.addSeparator();
    m.addItem (6, tr ("Otvoriť v predvolenom programe"), files.size() == 1 && ! f.isDirectory());
    PopupMenu with;
    const auto apps = files.size() == 1 && ! f.isDirectory() ? win::openWithApps (f) : StringArray();
    for (int i = 0; i < apps.size(); ++i) with.addItem (100 + i, apps[i]);
    if (! apps.isEmpty()) m.addSubMenu (tr ("Otvoriť v"), with);
    m.addItem (7, tr ("Ukázať v priečinku"), files.size() == 1);
    m.addItem (8, tr ("Kopírovať cestu"));
    m.addSeparator();
    m.addItem (10, tr ("Nájsť duplikáty v priečinku"));

    Component::SafePointer<MainComponent> sp (this);
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }), [sp, files, f] (int r)
    {
        if (sp == nullptr || r == 0) return;
        if (r == 1) sp->rotateMarked (true);
        else if (r == 2) sp->rotateMarked (false);
        else if (r == 3) sp->resizeDialog();
        else if (r == 4) sp->renameSelected();
        else if (r == 9) sp->batchRenameDialog();
        else if (r == 10) sp->findDuplicates();
        else if (r == 11) sp->openEditor();
        else if (r == 5) sp->deleteMarked();
        else if (r == 6) f.startAsProcess();
        else if (r == 7) f.revealToUser();
        else if (r == 8)
        {
            StringArray paths;
            for (auto& x : files) paths.add (x.getFullPathName());
            SystemClipboard::copyTextToClipboard (paths.joinIntoString ("\n"));
            sp->message (tr ("Cesta skopírovaná do schránky"));
        }
        else if (r == 20)
        {
            int n = 0;
            for (auto& x : files) if (kindOf (x) == Kind::Audio) { sp->player.enqueue (x); ++n; }
            sp->message (tr ("Pridané do fronty: ") + String (n));
        }
        else if (r >= 300 && r <= 300 + MetaStore::numLabels) sp->setLabelMarked (r - 300);
        else if (r >= 200 && r <= 205) sp->setRatingMarked (r - 200);
        else if (r >= 100) win::openWithApp (f, r - 100);
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

    if (digit >= 0 && mods.isAltDown() && digit <= 4) { setRatingMarked (digit + 1); return true; }
    if ((k == '0' || k == KeyPress::numberPad0) && mods.isAltDown()) { setRatingMarked (0); return true; }
    if (digit >= 0 && ! mods.isAltDown())
    {
        if (mods.isCtrlDown()) setTarget (digit, grid.getFolder());
        else moveSelectedTo (digit);
        return true;
    }
    if (mods.isCtrlDown() && (k == 'z' || k == 'Z')) { undo(); return true; }
    // úpravy obrázkov: R / L otočenie, Ctrl+R zmena veľkosti
    if ((k == 'r' || k == 'R') && mods.isCtrlDown() && ! mods.isAltDown()) { resizeDialog(); return true; }
    if ((k == 'r' || k == 'R') && ! mods.isAnyModifierKeyDown()) { rotateMarked (true); return true; }
    if ((k == 'l' || k == 'L') && ! mods.isAnyModifierKeyDown()) { rotateMarked (false); return true; }
    if ((k == 'e' || k == 'E') && ! mods.isAnyModifierKeyDown()) { openEditor(); return true; }
    if ((k == 'q' || k == 'Q') && ! mods.isAnyModifierKeyDown())
    {
        int n = 0;
        for (auto& x : grid.getMarkedFiles()) if (kindOf (x) == Kind::Audio) { player.enqueue (x); ++n; }
        message (n > 0 ? tr ("Pridané do fronty: ") + String (n) : tr ("Do fronty sa dajú pridať len zvukové súbory."), n == 0);
        return true;
    }
    return false;
}

bool MainComponent::keyPressed (const KeyPress& key)
{
    const int k = key.getKeyCode();
    const auto mods = key.getModifiers();

    if (handleSortKey (key)) return true;
    if (k == KeyPress::spaceKey)
    {
        const auto sk = kindOf (grid.getSelectedFile());
        if (sk == Kind::Midi || sk == Kind::Video || (sk == Kind::Audio && grid.getSelectedFile() != player.getCurrent()))
            preview.togglePlay();
        else player.togglePause();
        return true;
    }
    if (mods.isCtrlDown() && (k == 'f' || k == 'F')) { searchBox.grabKeyboardFocus(); searchBox.selectAll(); return true; }
    if (mods.isCtrlDown() && k == KeyPress::rightKey) { player.next(); return true; }
    if (mods.isCtrlDown() && k == KeyPress::leftKey) { player.previous(); return true; }
    if (k == KeyPress::playKey) { player.togglePause(); return true; }
    if (k == KeyPress::stopKey) { player.stop(); return true; }
    if (k == KeyPress::fastForwardKey) { player.next(); return true; }
    if (k == KeyPress::rewindKey) { player.previous(); return true; }
    if (k == KeyPress::deleteKey && ! mods.isAnyModifierKeyDown()) { deleteMarked(); return true; }
    if (k == KeyPress::F2Key && mods.isCtrlDown()) { batchRenameDialog(); return true; }
    if (k == KeyPress::F2Key) { renameSelected(); return true; }
    if ((k == 'v' || k == 'V') && ! mods.isAnyModifierKeyDown()) { grid.toggleMode(); return true; }
    if (k == KeyPress::F11Key)
    {
        const auto kk = kindOf (grid.getSelectedFile());
        if (isDocKind (kk) && kk != Kind::App) openDocFullscreen();
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
