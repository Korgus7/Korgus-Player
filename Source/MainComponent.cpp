#include <BinaryData.h>
#include "MainComponent.h"
#include <set>
#include "Updater.h"
#include "Convert.h"
#include "WinPrint.h"
#include "Similar.h"
#include "ExtraFormats.h"
#include "ImageEdit.h"
#include "WinImaging.h"
#include "PhotoInfo.h"

namespace kp
{
namespace { bool explorerIntegrated(); void setExplorerIntegration (bool on); }
static constexpr int topH = 48, bottomH = 34, playerH = 42;

MainComponent::MainComponent (const String& commandLine)
{
    PropertiesFile::Options o;
    o.applicationName = "Korgus Player";
    o.filenameSuffix = ".settings";
    o.folderName = "Korgus Player";
    o.osxLibrarySubFolder = "Application Support";
    props.setStorageParameters (o);

    setWantsKeyboardFocus (true);

    upButton.onClick = [this] { goUp(); };
    filterButton.onClick = [this] { showFilterMenu(); };
    sortButton.onClick = [this] { showSortMenu(); };
    viewButton.onClick = [this] { grid.toggleMode(); grid.grabKeyboardFocus(); };
    settingsButton.onClick = [this] { showSettingsMenu(); };
    filterButton.setText (tr ("Filter"));
    sortButton.setText (tr ("Triedenie"));
    slideshowButton.setText (tr ("Prezentácia"));
    dualButton.setText (tr ("Dva panely"));
    settingsButton.setText (tr ("Nastavenia"));
    for (auto* b : { &upButton, &filterButton, &sortButton, &viewButton, &settingsButton, &dualButton, &slideshowButton })
    {
        b->background = true;
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    grid.setThumbWidth (settings().getIntValue ("thumbW", 152));
    grid.setRowHeight (settings().getIntValue ("rowH", 26));
    grid2.setRowHeight (grid.getRowHeight());
    grid.setColumns (settings().getIntValue ("listCols", grid.getColumns()));
    grid2.setColumns (grid.getColumns());
    grid.setColumnWidths (settings().getValue ("listColW"));
    grid2.setColumnWidths (grid.getColumnWidths());
    grid.onSizeChanged = [this] { grid2.setRowHeight (grid.getRowHeight()); grid2.setColumnWidths (grid.getColumnWidths()); saveViewSizes(); updateSizeSlider(); };
    grid2.onSizeChanged = [this] { grid.setRowHeight (grid2.getRowHeight()); grid.setColumnWidths (grid2.getColumnWidths()); saveViewSizes(); updateSizeSlider(); };
    copyPathButton.setWantsKeyboardFocus (false);
    copyPathButton.onClick = [this]
    {
        SystemClipboard::copyTextToClipboard (grid.getFolder().getFullPathName());
        message (tr ("Cesta skopírovaná do schránky"));
    };
    addAndMakeVisible (copyPathButton);
    sizeSlider.onChange = [this]
    {
        const double v = sizeSlider.getValue();
        if (grid.getMode() == FileGrid::Mode::Grid)
            grid.setThumbWidth (roundToInt (FileGrid::minThumbW + v * (FileGrid::maxThumbW - FileGrid::minThumbW)));
        else
        {
            grid.setRowHeight (roundToInt (FileGrid::minRowH + v * (FileGrid::maxRowH - FileGrid::minRowH)));
            grid2.setRowHeight (grid.getRowHeight());
        }
    };
    sizeSlider.onEnd = [this] { saveViewSizes(); grid.grabKeyboardFocus(); };
    addAndMakeVisible (sizeSlider);

    addAndMakeVisible (tree);
    addAndMakeVisible (bar1);
    addAndMakeVisible (grid);
    addAndMakeVisible (bar2);
    addAndMakeVisible (preview);
    addAndMakeVisible (status);
    addAndMakeVisible (playerBar);

    searchBox.setTextToShowWhenEmpty (tr ("Hľadať (Ctrl+F) · Enter = aj v podpriečinkoch"), col::text.withAlpha (0.55f));
    searchBox.setColour (TextEditor::backgroundColourId, col::light ? Colours::white : col::bg);
    searchBox.setColour (TextEditor::outlineColourId, col::accentHi.withAlpha (0.8f));
    searchBox.setColour (TextEditor::focusedOutlineColourId, col::accentHi);
    searchBox.setColour (TextEditor::textColourId, col::text);
    searchBox.setIndents (28, 0);
    searchBox.setJustification (Justification::centredLeft);
    searchBox.onTextChange = [this] { grid.setFilter (searchBox.getText().trim(), typeBox.getSelectedItemIndex()); updateFolderInfo(); };
    searchBox.onEscapeKey = [this] { searchBox.clear(); grid.setFilter ({}, typeBox.getSelectedItemIndex()); grid.grabKeyboardFocus(); };
    searchBox.onReturnKey = [this]
    {
        // Enter = hľadať aj vo všetkých podpriečinkoch
        if (searchBox.getText().trim().isNotEmpty()) searchDeep (searchBox.getText().trim());
        grid.grabKeyboardFocus();
    };
    addAndMakeVisible (searchBox);

    typeBox.addItemList ({ tr ("Všetko"), tr ("Obrázky"), tr ("Zvuk"), tr ("Video"),
                           tr ("Dokumenty"), tr ("3D a CAD"), tr ("Priečinky"),
                           tr ("S hodnotením ★"), tr ("So štítkom") }, 1);
    typeBox.setSelectedItemIndex (0, dontSendNotification);
    typeBox.setTooltip (tr ("Zobraziť len vybraný typ súborov"));
    typeBox.setWantsKeyboardFocus (false);
    typeBox.onChange = [this]
    {
        grid.setFilter (searchBox.getText().trim(), typeBox.getSelectedItemIndex());
        filterButton.setActive (typeBox.getSelectedItemIndex() > 0);
        updateFolderInfo();
        grid.grabKeyboardFocus();
    };

    sortBox.addItemList ({ tr ("Názov A–Z"), tr ("Názov Z–A"), tr ("Najnovšie"),
                           tr ("Najstaršie"), tr ("Dátum fotenia"), tr ("Najväčšie"),
                           tr ("Typ súboru") }, 1);
    sortBox.setTooltip (tr ("Triedenie"));
    sortBox.setWantsKeyboardFocus (false);
    sortBox.setSelectedItemIndex (jlimit (0, 6, settings().getIntValue ("sort", 0)), dontSendNotification);
    sortBox.onChange = [this] { settings().setValue ("sort", sortBox.getSelectedItemIndex()); applySortBox(); grid.grabKeyboardFocus(); };
    grid.onSortChanged = [this] { sortBox.setSelectedItemIndex (-1, dontSendNotification); sortBox.setText (tr ("Vlastné"), dontSendNotification); };

    slideshowButton.onClick = [this] { startSlideshow(); };
    addAndMakeVisible (slideshowButton);

    // prehrávač: nastavenia, sledovanie skladby
    player.repeat = (AudioPlayer::Repeat) jlimit (0, 2, settings().getIntValue ("repeat", 1));
    player.shuffle = settings().getBoolValue ("shuffle", false);
    player.setVolume ((float) settings().getDoubleValue ("volume", 0.85));
    player.setSyncTempo (settings().getDoubleValue ("syncTempo", 0.0));
    if (auto xml = parseXML (settings().getValue ("audioDevice"))) player.setSavedDeviceState (std::move (xml));
    player.bpmOf = [this] (const File& f) -> double
    {
        if (auto* i = audioInfo.get (f)) return i->bpm;
        return 0.0;
    };
    playerBar.update();
    playerBar.onSettingsChanged = [this]
    {
        settings().setValue ("syncTempo", player.getSyncTempo());
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
    preview.onOpenLarge = [this]
    {
        const auto f = grid.getSelectedFile();
        if (f != File() && grid.onActivateFile) grid.onActivateFile (f);
    };
    preview.onRotate = [this] (bool cw) { rotateMarked (cw); grid.grabKeyboardFocus(); };
    preview.onResize = [this] { resizeDialog(); };
    preview.onEdit = [this] { openEditor(); };
    preview.onVideoFullscreen = [this] { openVideoFullscreen(); };
    preview.onFileCreated = [this] (const File& f, const String& m)
    {
        if (f.existsAsFile()) { const auto keep = grid.getSelectedFile(); grid.reload(); grid.selectFile (keep); }
        message (m, ! f.existsAsFile());
    };
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

    tree.onFolderSelected = [this] (const File& f) { openFolderIn (activeGrid(), f); };
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
    grid.onModeChanged = [this] { updateViewButton(); updateSizeSlider(); };
    grid.onMarksChanged = [this] { updateFolderInfo(); };
    grid.setMetaStore (&meta);
    preview.setMetaStore (&meta);
    preview.onRating = [this] (int r) { setRatingMarked (r); };
    preview.onLabel = [this] (int l) { setLabelMarked (l); };
    grid.onContextMenu = [this] (Point<int> p) { showContextMenu (p); };

    // druhý panel
    grid2.setMetaStore (&meta);
    grid2.setMode (FileGrid::Mode::List);
    grid2.onOpenFolder = [this] (const File& f) { openFolderIn (grid2, f); grid2.grabKeyboardFocus(); };
    grid2.onGoUp = [this]
    {
        const auto cur = grid2.getFolder();
        if (cur.getParentDirectory() != cur) openFolderIn (grid2, cur.getParentDirectory(), cur);
    };
    grid2.onActivateFile = [] (const File& f) { f.startAsProcess(); };
    grid2.onMarksChanged = [this] { updateFolderInfo(); };
    grid2.onSelectionChanged = [this] (const File&) { repaint (0, 0, getWidth(), topH); };
    addChildComponent (grid2);
    up2.background = true;
    up2.setWantsKeyboardFocus (false);
    up2.onClick = [this] { if (grid2.onGoUp) grid2.onGoUp(); grid2.grabKeyboardFocus(); };
    addChildComponent (up2);
    dualButton.onClick = [this] { setDual (! dual); };
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
    // karty
    {
        Array<File> t;
        for (auto& p : StringArray::fromTokens (settings().getValue ("tabs"), "|", ""))
            if (File (p).isDirectory()) t.add (File (p));
        tabs.setTabs (t, jlimit (0, jmax (0, t.size() - 1), settings().getIntValue ("tabIndex", 0)));
    }
    tabs.onSelect = [this] (int i) { selectTab (i); };
    tabs.onAdd = [this] { addTab(); };
    tabs.onClose = [this] (int i) { closeTab (i); };
    addAndMakeVisible (tabs);
    Desktop::getInstance().addFocusChangeListener (this);
    if (settings().getBoolValue ("dual", false)) MessageManager::callAsync ([sp = Component::SafePointer<MainComponent> (this)] { if (sp != nullptr) sp->setDual (true); });
    if (explorerIntegrated()) setExplorerIntegration (true);   // obnoviť cestu k exe (mohol sa premenovať/presunúť)
    updater::cleanupOld();
    // pripojená karta / fotoaparát (priečinok DCIM) → ponúknuť import
    drivePoll.fn = [this]
    {
        Array<File> roots;
        File::findFileSystemRoots (roots);
        StringArray now;
        for (auto& r : roots)
            if (r.getChildFile ("DCIM").isDirectory()) now.add (r.getFullPathName());
        for (auto& c : now)
            if (! knownCards.contains (c) && ! knownCards.isEmpty())
                message (tr ("Pripojená karta s fotkami (") + c + tr (") – ☰ → Importovať fotky"));
        knownCards = now;
        if (knownCards.isEmpty()) knownCards.add ("-");   // „nič“ ≠ prvé spustenie
    };
    drivePoll.fn();
    drivePoll.startTimer (4000);
   #if JUCE_WINDOWS
    if (settings().getBoolValue ("autoUpdate", true))
    {
        Component::SafePointer<MainComponent> sp (this);
        Timer::callAfterDelay (4000, [sp] { if (sp != nullptr) sp->checkForUpdates (false); });
    }
   #endif
    setSize (1400, 860);
    // po znovuotvorení programu sa naposledy vybraný zvuk, video ani MIDI samo nespustí
    if (arg.isEmpty())
    {
        preview.quietNext = true;
        Component::SafePointer<MainComponent> spq (this);
        Timer::callAfterDelay (2000, [spq] { if (spq != nullptr) spq->preview.quietNext = false; });
    }
    if (start.existsAsFile()) openFolder (start.getParentDirectory(), start);
    else openFolder (start, File (settings().getValue ("lastFile")));

    status.showMessage (tr ("Enter = obrázok na celú obrazovku · V = mriežka/zoznam · Šípky = listovanie · 1–9 = presun do cieľa · Ctrl+1–9 = nastaviť cieľ · Ctrl+Z = späť · Medzerník = prehrať/zastaviť"));
}

MainComponent::~MainComponent()
{
    transferPoll.stopTimer();
    transfer.reset();
    videoPoll.stopTimer();
    videoExport.reset();
    drivePoll.stopTimer();
    Desktop::getInstance().removeFocusChangeListener (this);
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
    g.setFont (FontOptions (15.0f));
    g.setFont (FontOptions (13.5f));   // ako v strome priečinkov
    g.drawText (grid.isListMode() ? grid.getListTitle() : grid.getFolder().getFullPathName(), pathArea(), Justification::centredLeft, true);
    {
        // dole vpravo pod posuvníkom veľkosti
        Rectangle<int> b (status.getRight(), status.getY(), getWidth() - status.getRight(), status.getHeight());
        g.setColour (col::panel);
        g.fillRect (b);
        g.setColour (col::line);
        g.fillRect (b.removeFromTop (1));
    }
    if (dual)
    {
        auto head = dualHolder.getBounds().removeFromTop (30);
        g.setColour (col::panel);
        g.fillRect (head);
        g.setColour (col::text);
        g.drawText (grid2.getFolder().getFullPathName(), head.withTrimmedLeft (42).reduced (4, 0), Justification::centredLeft, true);
        // aktívny panel = zlatá čiara hore
        g.setColour (col::gold);
        g.fillRect ((lastFocused == &grid2 ? grid2.getBounds() : grid.getBounds()).removeFromTop (2).translated (0, -2));
    }
}

void MainComponent::globalFocusChanged (Component*) { focusChangeNotify(); }

void MainComponent::paintOverChildren (Graphics& g)
{
    // lupa v poli hľadania
    auto r = searchBox.getBounds().toFloat();
    const float d = 11.0f, cx = r.getX() + 15.0f, cy = r.getCentreY() - 1.5f;
    g.setColour (col::text.withAlpha (0.6f));
    g.drawEllipse (cx - d / 2, cy - d / 2, d, d, 1.6f);
    g.drawLine (cx + d * 0.35f, cy + d * 0.35f, cx + d * 0.75f, cy + d * 0.75f, 1.8f);
}

void MainComponent::resized()
{
    if (editor) editor->setBounds (getLocalBounds());
    auto r = getLocalBounds();
    auto top = r.removeFromTop (topH);
    {
        auto bottom = r.removeFromBottom (bottomH);
        sizeSlider.setBounds (bottom.removeFromRight (190).reduced (10, 6));
        status.setBounds (bottom);
    }
    playerBar.setBounds (r.removeFromBottom (playerH));
    Component* comps[] = { &tree, &bar1, &grid, &bar2, dual ? (Component*) &dualHolder : (Component*) &preview };
    layout.layOutComponents (comps, 5, r.getX(), r.getY(), r.getWidth(), r.getHeight(), false, true);
    {
        auto gb = grid.getBounds();
        tabs.setBounds (gb.removeFromTop (30));
        grid.setBounds (gb);
    }
    if (dual)
    {
        auto d = dualHolder.getBounds();
        auto head = d.removeFromTop (30);
        up2.setBounds (head.removeFromLeft (36).reduced (4, 3));
        grid2.setBounds (d);
    }

    // horná lišta: úplne vľavo kopírovanie cesty, ↑ a cesta; od začiatku druhého okna filter, triedenie,
    // mriežka/zoznam a hľadanie; úplne vpravo prezentácia, dva panely, nastavenia (ikony s popisom)
    top = top.reduced (6, 0);
    copyPathButton.setBounds (top.removeFromLeft (34).reduced (3, 8));
    upButton.setBounds (top.removeFromLeft (40).reduced (3, 8));
    layoutLabelButton (settingsButton, top, true);
    layoutLabelButton (dualButton, top, true);
    layoutLabelButton (slideshowButton, top, true);
    top.removeFromRight (10);
    auto labelW = [] (IconButton& b) { return 7 + 18 + 5 + roundToInt (GlyphArrangement::getStringWidth (Font (FontOptions (12.5f)), b.getText())) + 18; };
    const int groupW = labelW (filterButton) + labelW (sortButton) + labelW (viewButton) + 180;
    const int startX = jlimit (top.getX() + 120, jmax (top.getX() + 120, top.getRight() - groupW), grid.getX());
    auto grp = top.withLeft (startX);
    layoutLabelButton (filterButton, grp, false);
    layoutLabelButton (sortButton, grp, false);
    layoutLabelButton (viewButton, grp, false);
    searchBox.setBounds (grp.reduced (4, 8));
    updateSizeSlider();
    repaint (0, 0, getWidth(), topH);   // cesta sa prekreslí aj pri ťahaní deliacej čiary medzi oknami
}

void MainComponent::focusChangeNotify()
{
    if (! dual) return;
    auto* f = Component::getCurrentlyFocusedComponent();
    FileGrid* now = nullptr;
    if (f == &grid2 || grid2.isParentOf (f)) now = &grid2;
    else if (f == &grid || grid.isParentOf (f)) now = &grid;
    if (now != nullptr && now != lastFocused) { lastFocused = now; repaint(); }
}

void MainComponent::setDual (bool on)
{
    dual = on;
    settings().setValue ("dual", on);
    if (on && grid2.getFolder() == File())
    {
        File f (settings().getValue ("secondFolder"));
        openFolderIn (grid2, f.isDirectory() ? f : grid.getFolder());
    }
    preview.setVisible (! on);
    if (on) preview.show ({});
    grid2.setVisible (on);
    up2.setVisible (on);
    dualButton.setActive (on);
    lastFocused = &grid;
    resized();
    repaint();
    if (! on) selectionChanged (grid.getSelectedFile());
    grid.grabKeyboardFocus();
    if (on) message (tr ("Dva panely: F5 kopíruje, F6 presúva označené do druhého panela · Tab prepína panel · F9 vypne"));
}

void MainComponent::openFolderIn (FileGrid& g, const File& folder, const File& toSelect)
{
    if (&g == &grid) { openFolder (folder, toSelect); return; }
    if (! folder.isDirectory()) return;
    grid2.setFolder (folder, toSelect);
    settings().setValue ("secondFolder", folder.getFullPathName());
    repaint();
}

void MainComponent::transferToOther (bool move)
{
    auto& src = activeGrid();
    auto& dst = otherGrid();
    const auto files = src.getMarkedFiles();
    const auto dir = dst.getFolder();
    if (files.isEmpty() || ! dir.isDirectory()) return;
    if (src.getFolder() == dir) { message (tr ("Oba panely ukazujú ten istý priečinok."), true); return; }
    if (move)
    {
        moveFilesTo (files, dir, src);
        return;
    }
    // kopírovanie na pozadí s priebehom (Esc zruší)
    std::vector<Transfer::Item> items;
    for (auto& f : files)
    {
        if (f.isDirectory() && (dir == f || dir.isAChildOf (f))) { message (tr ("Priečinok sa nedá kopírovať sám do seba."), true); return; }
        auto dest = dir.getChildFile (f.getFileName());
        if (dest.exists()) dest = dir.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), true);
        items.push_back ({ f, dest });
    }
    startTransfer (std::move (items), false, [this, dir] (Transfer& t)
    {
        otherGrid().reload();
        if (grid.getFolder() == dir) grid.reload();
        activeGrid().clearMarks();
        tree.refreshFolder (dir);
        updateFolderInfo();
        String m = (t.wasCancelled() ? tr ("Kopírovanie zrušené · skopírované: ") : tr ("Skopírované: "))
                   + String ((int) t.completed.size()) + tr (" → ") + dir.getFileName();
        if (t.error.isNotEmpty()) m << " · " << t.error;
        message (m, t.error.isNotEmpty());
    });
}

// ---------------------------------------------------------------- navigácia

void MainComponent::openFolder (const File& folder, const File& toSelect)
{
    if (! folder.isDirectory()) return;
    if (folder != grid.getFolder()) searchBox.clear();
    grid.setFolder (folder, toSelect);
    tree.reveal (folder);
    settings().setValue ("lastFolder", folder.getFullPathName());
    {
        auto t = tabs.getTabs();
        if (t.isEmpty()) t.add (folder);
        else t.set (jlimit (0, t.size() - 1, tabs.getCurrent()), folder);
        tabs.setTabs (t, jlimit (0, t.size() - 1, tabs.getCurrent()));
        saveTabs();
    }
    updateFolderInfo();
    repaint (0, 0, getWidth(), topH);
}

void MainComponent::goUp()
{
    if (grid.isListMode())
    {
        searchBox.clear();
        grid.setFilter ({}, typeBox.getSelectedItemIndex());
        openFolder (grid.getFolder(), grid.getSelectedFile().getParentDirectory() == grid.getFolder() ? grid.getSelectedFile() : File());
        return;
    }
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

// BPM a tónina pre {bpm} {tonina}: cesta → (bpm, tónina v tvare „Am“ / „C“)
using AudioNames = std::map<String, std::pair<String, String>>;
static String shortKey (const String& key)
{
    // „a mol“ → „Am“, „C dur“ → „C“, „F# minor“ → „F#m“
    auto k = key.trim();
    if (k.isEmpty()) return {};
    const auto note = k.upToFirstOccurrenceOf (" ", false, false);
    const bool minor = k.containsIgnoreCase ("mol") || k.containsIgnoreCase ("minor");
    return note.substring (0, 1).toUpperCase() + note.substring (1) + (minor ? "m" : "");
}

static String batchName (const String& pattern, const File& f, int number, int digits, const AudioNames* audio = nullptr)
{
    Time t = f.getLastModificationTime();
    const auto pi = readPhotoInfo (f);
    if (pi.hasTaken) t = pi.taken;
    String n = pattern;
    if (n.contains ("{bpm}") || n.contains ("{tonina}"))
    {
        String bpm, key;
        if (audio != nullptr)
            if (auto it = audio->find (f.getFullPathName()); it != audio->end()) { bpm = it->second.first; key = it->second.second; }
        n = n.replace ("{bpm}", bpm).replace ("{tonina}", key);
        // prázdna hodnota nenechá zdvojené oddeľovače
        for (auto d : { "__", "--", "  ", "_-", "-_" }) while (n.contains (d)) n = n.replace (d, String (d).substring (0, 1));
        n = n.trimCharactersAtEnd ("_- ").trimCharactersAtStart ("_- ");
        if (bpm.isEmpty()) n = n.replace ("_bpm", "").replace ("bpm_", "").replace ("-bpm", "");
    }
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
                                                 tr ("Značky: {datum} {cas} {cislo} {nazov} {priecinok} {bpm} {tonina}"), MessageBoxIconType::NoIcon, this);
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
        AudioNames names;
        for (int i = 0; i < jmin (3, files.size()); ++i)
            if (auto* info = audioInfo.get (files[i]))
                names[files[i].getFullPathName()] = { info->bpm > 0 ? String (roundToInt (info->bpm)) : String(), shortKey (info->key) };
        for (int i = 0; i < jmin (3, files.size()); ++i) p << batchName (pattern, files[i], start + i, digits, &names) << "\n";
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

        auto rename = [sp, files, pattern, start] (AudioNames names)
        {
            if (sp == nullptr) return;
            const int digits = jmax (2, String (start + files.size()).length());
            // dva kroky (cez dočasné mená), aby sa mená nepobili
            Batch batch;
            std::vector<std::pair<File, File>> temp;
            for (int i = 0; i < files.size(); ++i)
            {
                const auto& f = files.getReference (i);
                sp->preview.release (f);
                const auto t = f.getSiblingFile ("~korgus-" + String (i) + "-" + String (Random::getSystemRandom().nextInt (1000000)) + f.getFileExtension());
                if (f.moveFileTo (t))
                {
                    temp.push_back ({ f, t });
                    if (auto it = names.find (f.getFullPathName()); it != names.end()) names[t.getFullPathName()] = it->second;
                }
            }
            int done = 0;
            for (size_t i = 0; i < temp.size(); ++i)
            {
                const auto& [orig, t] = temp[i];
                auto name = batchName (pattern, orig, start + (int) i, digits, &names);
                // dátum treba čítať z dočasného súboru (pôvodný už neexistuje)
                name = batchName (pattern, t, start + (int) i, digits, &names).replace (t.getFileNameWithoutExtension(), orig.getFileNameWithoutExtension());
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
        };
        if (! (pattern.contains ("{bpm}") || pattern.contains ("{tonina}"))) { rename ({}); return; }
        // BPM a tónina sa zistia na pozadí (môže to chvíľu trvať)
        sp->message (tr ("Zisťujem BPM a tóninu…"));
        auto* audio = &sp->audioInfo;
        Thread::launch ([sp, files, audio, rename]
        {
            AudioNames names;
            for (int i = 0; i < files.size(); ++i)
            {
                const auto& f = files.getReference (i);
                if (kindOf (f) != Kind::Audio) continue;
                const auto info = audio->computeNow (f);
                if (info.ok) names[f.getFullPathName()] = { info.bpm > 0 ? String (roundToInt (info.bpm)) : String(), shortKey (info.key) };
                if (i % 5 == 0)
                    MessageManager::callAsync ([sp, i, n = files.size()] { if (sp != nullptr) sp->message (tr ("Zisťujem BPM a tóninu ") + String (i + 1) + "/" + String (n) + "…"); });
            }
            MessageManager::callAsync ([rename, names] { rename (names); });
        });
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
    m.addSectionHeader (tr ("Fotky"));
    m.addItem (12, tr ("Importovať fotky z karty / fotoaparátu…"));
    m.addSectionHeader (tr ("Zvuk"));
    m.addItem (11, tr ("Zvukový výstup (zvuková karta, WASAPI)…"));
    m.addSectionHeader (tr ("Windows"));
    m.addItem (5, tr ("„Otvoriť v Korgus Player“ v Prieskumníkovi (pravý klik)"), true, explorerIntegrated());
    if (! VideoView::isAvailable()) m.addItem (6, tr ("Stiahnuť prehrávač videa (libmpv, 50 MB)"));
    m.addSectionHeader (tr ("Aktualizácie"));
    m.addItem (7, tr ("Skontrolovať aktualizácie"), ! updateBusy);
    m.addItem (8, tr ("Kontrolovať pri spustení"), true, settings().getBoolValue ("autoUpdate", true));
    m.addSeparator();
    m.addItem (10, tr ("Návod na použitie (web)"));
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
        else if (r == 6) sp->downloadVideoPlayer();
        else if (r == 7) sp->checkForUpdates (true);
        else if (r == 8) s.setValue ("autoUpdate", ! s.getBoolValue ("autoUpdate", true));
        else if (r == 11) sp->audioSettingsDialog();
        else if (r == 12) sp->importDialog();
        else if (r == 10)
        {
            // navod je pribaleny v programe: zapise sa vedla nastaveni a otvori v prehliadaci
            auto dir = File (SystemStats::getEnvironmentVariable ("LOCALAPPDATA", File::getSpecialLocation (File::userApplicationDataDirectory).getFullPathName()))
                           .getChildFile ("KorgusPlayer");
            dir.createDirectory();
            auto f = dir.getChildFile ("navod.html");
            if (f.getSize() != (int64) BinaryData::navod_htmlSize)
                f.replaceWithData (BinaryData::navod_html, (size_t) BinaryData::navod_htmlSize);
            if (! f.startAsProcess())
                URL ("https://cami.sk/korgussequencer").launchInDefaultBrowser();
        }
        else if (r == 9)
        {
            AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::NoIcon)
                                        .withTitle ("Korgus Player " + JUCEApplication::getInstance()->getApplicationVersion())
                                        .withMessage (tr ("Zadarmo · cami.sk") + "\n\n" + tr ("Licencia: zadarmo na používanie, úpravy a šírenie len zadarmo a bez reklám, predaj zakázaný."))
                                        .withButton ("OK"), nullptr);
        }
    });
}

void MainComponent::checkForUpdates (bool manual)
{
    if (updateBusy) return;
    updateBusy = true;
    if (manual) message (tr ("Hľadám novú verziu…"));
    Component::SafePointer<MainComponent> sp (this);
    updater::check ([sp, manual] (updater::Info info)
    {
        if (sp == nullptr) return;
        sp->updateBusy = false;
        const auto current = JUCEApplication::getInstance()->getApplicationVersion();
        if (info.version.isEmpty()) { if (manual) sp->message (tr ("Nepodarilo sa zistiť novú verziu (si pripojený na internet?)."), true); return; }
        if (! updater::isNewer (info.version, current))
        {
            if (manual) sp->message (tr ("Máš najnovšiu verziu ") + current);
            return;
        }
        if (! manual && sp->settings().getValue ("skipVersion") == info.version) return;
        String text = tr ("Je k dispozícii Korgus Player ") + info.version + tr (" (máš ") + current + ").";
        if (info.notes.isNotEmpty()) text << "\n\n" << info.notes;
        text << "\n\n" << tr ("Program sa stiahne, nahradí a znova spustí.");
        auto opts = MessageBoxOptions().withIconType (MessageBoxIconType::InfoIcon).withTitle (tr ("Nová verzia"))
                        .withMessage (text).withButton (tr ("Aktualizovať")).withButton (tr ("Neskôr")).withButton (tr ("Preskočiť túto verziu"));
        AlertWindow::showAsync (opts, [sp, info] (int choice)
        {
            if (sp == nullptr) return;
            if (choice == 3) { sp->settings().setValue ("skipVersion", info.version); return; }
            if (choice != 1) return;
            sp->updateBusy = true;
            sp->message (tr ("Sťahujem novú verziu…"));
            updater::install (info, [sp] (String err)
            {
                if (sp == nullptr) return;
                sp->updateBusy = false;
                if (err.isNotEmpty()) sp->message (err, true);
            });
        });
    });
}

void MainComponent::downloadVideoPlayer()
{
    if (updateBusy) return;
    updateBusy = true;
    message (tr ("Sťahujem prehrávač videa (50 MB)…"));
    Component::SafePointer<MainComponent> sp (this);
    updater::check ([sp] (updater::Info info)
    {
        if (sp == nullptr) return;
        updater::installVideo (info, [sp] (String err)
        {
            if (sp == nullptr) return;
            sp->updateBusy = false;
            if (err.isNotEmpty()) { sp->message (err, true); return; }
            sp->message (tr ("Prehrávač videa je nainštalovaný."));
            if (restartUI) restartUI();
        });
    });
}

bool MainComponent::startTransfer (std::vector<Transfer::Item> items, bool move, std::function<void (Transfer&)> done)
{
    if (transfer != nullptr) { message (tr ("Počkaj, kým sa dokončí prebiehajúci prenos (Esc ho zruší)."), true); return false; }
    if (items.empty()) return false;
    transfer = std::make_unique<Transfer> (std::move (items), move);
    transferDone = std::move (done);
    transfer->start();
    transferPoll.fn = [this]
    {
        if (transfer == nullptr) { transferPoll.stopTimer(); return; }
        if (transfer->isDone())
        {
            transferPoll.stopTimer();
            auto t = std::move (transfer);
            if (transferDone) transferDone (*t);
            transferDone = nullptr;
            return;
        }
        const int pct = roundToInt (transfer->progress() * 100.0);
        message ((transfer->isMove() ? tr ("Presúvam ") : tr ("Kopírujem ")) + String (transfer->currentIndex() + 1) + "/" + String (transfer->count())
                 + " · " + String (pct) + " %" + tr (" · Esc = zrušiť"));
    };
    transferPoll.startTimer (250);
    return true;
}

void MainComponent::afterMoved (const File& from, const File& to, FileGrid& source)
{
    thumbs.forget (from);
    imageCache.forget (from);
    audioInfo.forget (from);
    meta.moved (from, to);
    source.removeFile (from);
}

void MainComponent::openCompare()
{
    Array<File> imgs;
    for (int i = 0; i < grid.getNumFiles(); ++i)
        if (grid.getKindAt (i) == Kind::Image) imgs.add (grid.getFileAt (i));
    Array<File> marked;
    for (auto& f : grid.getMarkedFiles()) if (kindOf (f) == Kind::Image) marked.add (f);
    if (marked.isEmpty() || imgs.size() < 2) { message (tr ("Na porovnanie treba aspoň dve fotky v priečinku."), true); return; }
    File left = marked[0], right = marked.size() > 1 ? marked[1] : File();
    if (right == File())   // jedna označená → porovnať s nasledujúcou
        right = imgs[(imgs.indexOf (left) + 1) % imgs.size()];
    closeFullscreen();
    compare = std::make_unique<CompareView> (left, right, imgs);
    Component::SafePointer<MainComponent> sp (this);
    auto close = [sp]
    {
        MessageManager::callAsync ([sp]
        {
            if (sp == nullptr || sp->compare == nullptr) return;
            Desktop::getInstance().setKioskModeComponent (nullptr);
            sp->compare.reset();
            if (auto* top = sp->getTopLevelComponent()) top->toFront (true);
            sp->grid.grabKeyboardFocus();
        });
    };
    compare->onClose = close;
    compare->onPick = [sp, close] (const File& keep, const File& other)
    {
        if (sp == nullptr) return;
        sp->grid.clearMarks();
        sp->grid.markFiles ({ other });
        sp->grid.selectFile (keep);
        sp->message (tr ("Nechávaš ") + keep.getFileName() + tr (" · označená druhá: ") + other.getFileName()
                     + tr (" – Del ju dá do koša, číslo 1–9 ju presunie"));
        close();
    };
    compare->addToDesktop (0);
    compare->setVisible (true);
    Desktop::getInstance().setKioskModeComponent (compare.get(), false);
    compare->grabKeyboardFocus();
}

void MainComponent::videoDialog()
{
    if (videoExport != nullptr) { message (tr ("Video sa ešte vytvára – Esc ho zruší."), true); return; }
    Array<File> imgs;
    for (auto& f : grid.getMarkedFiles()) if (kindOf (f) == Kind::Image) imgs.add (f);
    if (imgs.size() < 2)
    {
        imgs.clear();
        for (int i = 0; i < grid.getNumFiles(); ++i) if (grid.getKindAt (i) == Kind::Image) imgs.add (grid.getFileAt (i));
    }
    if (imgs.size() < 2) { message (tr ("Na video treba aspoň dve fotky (označ ich, alebo budú všetky z priečinka)."), true); return; }
    videoWindow = std::make_unique<AlertWindow> (tr ("Video z fotiek"),
        String (imgs.size()) + tr (" fotiek · 1920×1080 · pomalé približovanie a prelínanie · MP4 sa uloží do priečinka"), MessageBoxIconType::NoIcon);
    videoWindow->addComboBox ("sec", { "3 s", "4 s", "5 s", "6 s", "8 s", "10 s" }, tr ("Jedna fotka"));
    videoWindow->getComboBoxComponent ("sec")->setSelectedItemIndex (jlimit (0, 5, settings().getIntValue ("videoSec", 2)));
    videoWindow->addComboBox ("music", { tr ("Bez hudby"), tr ("Vybrať hudbu…") }, tr ("Hudba"));
    videoWindow->getComboBoxComponent ("music")->setSelectedItemIndex (1);
    videoWindow->addButton (tr ("Vytvoriť"), 1, KeyPress (KeyPress::returnKey));
    videoWindow->addButton (tr ("Zrušiť"), 0, KeyPress (KeyPress::escapeKey));
    Component::SafePointer<MainComponent> sp (this);
    videoWindow->enterModalState (true, ModalCallbackFunction::create ([sp, imgs] (int r)
    {
        if (sp == nullptr) return;
        const int secIdx = sp->videoWindow->getComboBoxComponent ("sec")->getSelectedItemIndex();
        const bool withMusic = sp->videoWindow->getComboBoxComponent ("music")->getSelectedItemIndex() == 1;
        sp->videoWindow.reset();
        if (r != 1) return;
        sp->settings().setValue ("videoSec", secIdx);
        static const double secs[] = { 3, 4, 5, 6, 8, 10 };
        const double s = secs[jlimit (0, 5, secIdx)];
        if (! withMusic) { sp->startVideo (imgs, {}, s); return; }
        sp->chooser = std::make_unique<FileChooser> (tr ("Hudba k videu"), File (sp->settings().getValue ("slideshowMusic")),
                                                     "*.mp3;*.wav;*.flac;*.m4a;*.ogg;*.aif;*.aiff");
        sp->chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [sp, imgs, s] (const FileChooser& fc)
        {
            if (sp != nullptr) sp->startVideo (imgs, fc.getResult(), s);
        });
    }));
}

void MainComponent::startVideo (Array<File> imgs, File music, double secs)
{
    const auto dir = imgs[0].getParentDirectory();
    const auto out = dir.getNonexistentChildFile (dir.getFileName() + tr ("_video"), ".mp4", false);
    videoExport = std::make_unique<VideoExport> (imgs, music, out, secs);
    videoExport->start();
    videoPoll.fn = [this]
    {
        if (videoExport == nullptr) { videoPoll.stopTimer(); return; }
        if (! videoExport->isDone())
        {
            message (tr ("Vytváram video ") + String (roundToInt (videoExport->progress() * 100)) + tr (" % · Esc = zrušiť"));
            return;
        }
        videoPoll.stopTimer();
        auto v = std::move (videoExport);
        if (v->wasCancelled()) { message (tr ("Video zrušené.")); return; }
        if (v->error.isNotEmpty()) { message (v->error, true); return; }
        grid.reload();
        grid.selectFile (v->output);
        message (tr ("Video hotové: ") + v->output.getFileName() + tr (" · Enter ho prehrá"));
    };
    videoPoll.startTimer (300);
}

void MainComponent::importDialog()
{
    Array<File> roots, cards;
    File::findFileSystemRoots (roots);
    for (auto& r : roots) if (r.getChildFile ("DCIM").isDirectory()) cards.add (r.getChildFile ("DCIM"));
    importWindow = std::make_unique<AlertWindow> (tr ("Import fotiek"),
        tr ("Fotky a videá sa skopírujú do priečinkov podľa dátumu fotenia. Na karte ostanú, už skopírované sa preskočia."),
        MessageBoxIconType::NoIcon);
    StringArray src;
    for (auto& c : cards) src.add (c.getFullPathName());
    src.add (tr ("Vybrať priečinok…"));
    importWindow->addComboBox ("src", src, tr ("Odkiaľ"));
    const File pics (settings().getValue ("importTarget", File::getSpecialLocation (File::userPicturesDirectory).getFullPathName()));
    importWindow->addComboBox ("dst", { pics.getFullPathName(), tr ("Vybrať priečinok…") }, tr ("Kam"));
    importWindow->addComboBox ("struct", { tr ("Rok\\Rok-mesiac-deň (2026\\2026-11-18)"), tr ("Rok-mesiac-deň (2026-11-18)"),
                                          tr ("Rok\\mesiac (2026\\11)"), tr ("Rok-mesiac (2026-11)") }, tr ("Priečinky"));
    importWindow->getComboBoxComponent ("struct")->setSelectedItemIndex (jlimit (0, 3, settings().getIntValue ("importStruct", 0)));
    importWindow->addTextEditor ("suffix", {}, tr ("Pridať k názvu priečinka (napr. Dovolenka) – nepovinné"));
    importWindow->addButton (tr ("Importovať"), 1, KeyPress (KeyPress::returnKey));
    importWindow->addButton (tr ("Zrušiť"), 0, KeyPress (KeyPress::escapeKey));
    Component::SafePointer<MainComponent> sp (this);
    importWindow->enterModalState (true, ModalCallbackFunction::create ([sp, cards, pics] (int r)
    {
        if (sp == nullptr) return;
        auto* w = sp->importWindow.get();
        const int si = w->getComboBoxComponent ("src")->getSelectedItemIndex();
        const int di = w->getComboBoxComponent ("dst")->getSelectedItemIndex();
        const int st = w->getComboBoxComponent ("struct")->getSelectedItemIndex();
        const auto suffix = File::createLegalFileName (w->getTextEditorContents ("suffix").trim());
        sp->importWindow.reset();
        if (r != 1) return;
        sp->settings().setValue ("importStruct", st);
        // výber priečinkov, ak treba (postupne)
        auto withTarget = [sp, st, suffix, di, pics] (File source)
        {
            if (sp == nullptr || ! source.isDirectory()) return;
            if (di == 0) { sp->runImport (source, pics, st, suffix); return; }
            sp->chooser = std::make_unique<FileChooser> (tr ("Kam importovať fotky"), pics);
            sp->chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [sp, source, st, suffix] (const FileChooser& fc)
            {
                if (sp == nullptr || ! fc.getResult().isDirectory()) return;
                sp->settings().setValue ("importTarget", fc.getResult().getFullPathName());
                sp->runImport (source, fc.getResult(), st, suffix);
            });
        };
        if (si >= 0 && si < cards.size()) { withTarget (cards[si]); return; }
        sp->chooser = std::make_unique<FileChooser> (tr ("Odkiaľ importovať (karta, fotoaparát, priečinok z mobilu)"), File());
        sp->chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [sp, withTarget] (const FileChooser& fc)
        {
            if (sp != nullptr) MessageManager::callAsync ([withTarget, f = fc.getResult()] { withTarget (f); });
        });
    }));
}

void MainComponent::runImport (const File& source, const File& target, int structure, const String& suffix)
{
    message (tr ("Hľadám fotky a videá na ") + source.getFullPathName() + "…");
    Component::SafePointer<MainComponent> sp (this);
    Thread::launch ([sp, source, target, structure, suffix]
    {
        std::vector<Transfer::Item> items;
        int skipped = 0;
        std::set<String> folders;
        for (const auto& e : RangedDirectoryIterator (source, true, "*", File::findFiles | File::ignoreHiddenFiles))
        {
            const auto f = e.getFile();
            const auto k = kindOf (f);
            if (k != Kind::Image && k != Kind::Video) continue;
            Time t = e.getModificationTime();
            if (k == Kind::Image) { const auto pi = readPhotoInfo (f); if (pi.hasTaken) t = pi.taken; }
            String rel;
            switch (structure)
            {
                case 1:  rel = t.formatted ("%Y-%m-%d"); break;
                case 2:  rel = t.formatted ("%Y") + File::getSeparatorString() + t.formatted ("%m"); break;
                case 3:  rel = t.formatted ("%Y-%m"); break;
                default: rel = t.formatted ("%Y") + File::getSeparatorString() + t.formatted ("%Y-%m-%d"); break;
            }
            if (suffix.isNotEmpty()) rel << " " << suffix;
            const auto dir = target.getChildFile (rel);
            auto dest = dir.getChildFile (f.getFileName());
            if (dest.existsAsFile() && dest.getSize() == e.getFileSize()) { ++skipped; continue; }   // už importované
            if (dest.exists()) dest = dir.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), true);
            // dva rovnaké názvy v jednom importe
            for (auto& it : items) if (it.to == dest) { dest = dir.getChildFile (f.getFileNameWithoutExtension() + "_" + String ((int) items.size()) + f.getFileExtension()); break; }
            folders.insert (dir.getFullPathName());
            items.push_back ({ f, dest });
        }
        MessageManager::callAsync ([sp, items, skipped, folders, target]
        {
            if (sp == nullptr) return;
            if (items.empty()) { sp->message (skipped > 0 ? tr ("Všetko už je importované (") + String (skipped) + tr (" súborov).") : tr ("Na zdroji nie sú fotky ani videá."), skipped == 0); return; }
            for (auto& d : folders) File (d).createDirectory();
            const int n = (int) items.size();
            sp->startTransfer (items, false, [sp, n, skipped, target] (Transfer& t)
            {
                if (sp == nullptr) return;
                String m = (t.wasCancelled() ? tr ("Import zrušený · skopírované: ") : tr ("Importované: ")) + String ((int) t.completed.size()) + "/" + String (n);
                if (skipped > 0) m << tr (" · preskočené (už boli): ") << skipped;
                if (t.error.isNotEmpty()) m << " · " << t.error;
                sp->message (m, t.error.isNotEmpty());
                sp->tree.refreshFolder (target);
                if (! t.completed.empty()) sp->openFolder (t.completed.back().to.getParentDirectory());
            });
        });
    });
}

void MainComponent::audioSettingsDialog()
{
    auto& dm = player.getDeviceManager();
    auto* sel = new AudioDeviceSelectorComponent (dm, 0, 0, 2, 2, false, false, true, false);
    sel->setSize (520, 420);
    DialogWindow::LaunchOptions o;
    o.content.setOwned (sel);
    o.dialogTitle = tr ("Zvukový výstup");
    o.dialogBackgroundColour = col::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    auto* w = o.launchAsync();
    // po zatvorení okna uložiť výber
    struct Saver : public ComponentListener
    {
        MainComponent& mc;
        explicit Saver (MainComponent& m) : mc (m) {}
        void componentBeingDeleted (Component& c) override
        {
            if (auto xml = mc.player.getDeviceState()) mc.settings().setValue ("audioDevice", xml->toString());
            c.removeComponentListener (this);
            delete this;
        }
    };
    if (w != nullptr) w->addComponentListener (new Saver (*this));
}

void MainComponent::openSequencer()
{
    // nainštalovaný Sequencer (pre používateľa), alebo uložená cesta
    Array<File> places { File (settings().getValue ("sequencerPath")),
                         File (SystemStats::getEnvironmentVariable ("LOCALAPPDATA", "/nonexistent")).getChildFile ("Programs/Korgus Sequencer/Korgus Sequencer.exe"),
                         File::getSpecialLocation (File::currentExecutableFile).getParentDirectory().getSiblingFile ("Korgus Sequencer").getChildFile ("Korgus Sequencer.exe") };
    for (auto& f : places)
        if (f.existsAsFile())
        {
            f.startAsProcess();
            message (tr ("Korgus Sequencer sa otvára – sample alebo MIDI pretiahni myšou zo zoznamu na stopu v Sequenceri"));
            return;
        }
    chooser = std::make_unique<FileChooser> (tr ("Kde je Korgus Sequencer.exe? (Zrušiť = stiahnuť ho z cami.sk)"), File(), "*.exe");
    Component::SafePointer<MainComponent> sp (this);
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [sp] (const FileChooser& fc)
    {
        if (sp == nullptr) return;
        const auto f = fc.getResult();
        if (f.existsAsFile()) { sp->settings().setValue ("sequencerPath", f.getFullPathName()); sp->openSequencer(); }
        else URL ("https://cami.sk/korgussequencer").launchInDefaultBrowser();
    });
}

void MainComponent::saveTabs()
{
    StringArray paths;
    for (auto& f : tabs.getTabs()) paths.add (f.getFullPathName());
    settings().setValue ("tabs", paths.joinIntoString ("|"));
    settings().setValue ("tabIndex", tabs.getCurrent());
}

void MainComponent::selectTab (int i)
{
    const auto t = tabs.getTabs();
    if (i < 0 || i >= t.size()) return;
    tabs.setTabs (t, i);
    if (t[i].isDirectory()) openFolder (t[i]);
    grid.grabKeyboardFocus();
}

void MainComponent::addTab()
{
    auto t = tabs.getTabs();
    t.insert (tabs.getCurrent() + 1, grid.getFolder());
    tabs.setTabs (t, tabs.getCurrent() + 1);
    saveTabs();
    grid.grabKeyboardFocus();
    message (tr ("Nová karta – vyber priečinok v strome · Ctrl+W ju zavrie"));
}

void MainComponent::closeTab (int i)
{
    auto t = tabs.getTabs();
    if (t.size() <= 1 || i < 0 || i >= t.size()) return;
    t.remove (i);
    int cur = tabs.getCurrent();
    if (cur > i || cur >= t.size()) cur = jmax (0, cur - 1);
    tabs.setTabs (t, cur);
    selectTab (cur);
}

void MainComponent::searchDeep (const String& text)
{
    const auto base = grid.getFolder();
    if (! base.isDirectory()) return;
    if (searchCancel) *searchCancel = true;
    auto cancel = std::make_shared<std::atomic<bool>> (false);
    searchCancel = cancel;
    message (tr ("Hľadám „") + text + tr ("“ vo všetkých podpriečinkoch…"));
    Component::SafePointer<MainComponent> sp (this);
    Thread::launch ([sp, base, text, cancel]
    {
        const auto words = StringArray::fromTokens (FileGrid::fold (text), " ", "");
        Array<File> found;
        int scanned = 0;
        bool limited = false;
        for (const auto& e : RangedDirectoryIterator (base, true, "*", File::findFilesAndDirectories | File::ignoreHiddenFiles))
        {
            if (*cancel) return;
            ++scanned;
            const auto rel = FileGrid::fold (e.getFile().getRelativePathFrom (base));
            bool ok = true;
            for (auto& w : words) if (! rel.contains (w)) { ok = false; break; }
            if (ok) found.add (e.getFile());
            if (found.size() >= 5000) { limited = true; break; }
        }
        MessageManager::callAsync ([sp, base, text, found, scanned, limited, cancel]
        {
            if (sp == nullptr || *cancel) return;
            const String title = tr ("Hľadanie „") + text + tr ("“ v ") + base.getFullPathName() + " · " + String (found.size())
                               + tr (" nájdených") + (limited ? tr (" (prvých 5000)") : String());
            sp->grid.showList (base, found, title, false);
            sp->repaint (0, 0, sp->getWidth(), topH);
            sp->updateFolderInfo();
            sp->message (String (found.size()) + tr (" nájdených z ") + String (scanned) + tr (" · Backspace alebo Esc = späť do priečinka"));
        });
    });
}

void MainComponent::findSimilarPhotos()
{
    Array<File> imgs;
    for (int i = 0; i < grid.getNumFiles(); ++i)
        if (grid.getKindAt (i) == Kind::Image) imgs.add (grid.getFileAt (i));
    if (imgs.size() < 2) { message (tr ("Na porovnanie treba aspoň dve fotky v priečinku."), true); return; }
    if (searchCancel) *searchCancel = true;
    auto cancel = std::make_shared<std::atomic<bool>> (false);
    searchCancel = cancel;
    const auto base = grid.getFolder();
    message (tr ("Porovnávam ") + String (imgs.size()) + tr (" fotiek…"));
    Component::SafePointer<MainComponent> sp (this);
    Thread::launch ([sp, imgs, base, cancel]
    {
        std::vector<similar::ImageSig> h ((size_t) imgs.size());
        std::vector<bool> ok ((size_t) imgs.size());
        for (int i = 0; i < imgs.size(); ++i)
        {
            if (*cancel) return;
            ok[(size_t) i] = similar::imageHash (imgs[i], h[(size_t) i]);
            if (i % 20 == 0)
                MessageManager::callAsync ([sp, i, n = imgs.size()] { if (sp != nullptr) sp->message (tr ("Porovnávam fotky ") + String (i) + "/" + String (n) + "…"); });
        }
        // zoskupenie (union-find), prah 10 z 64 bitov
        std::vector<int> parent ((size_t) imgs.size());
        for (size_t i = 0; i < parent.size(); ++i) parent[i] = (int) i;
        std::function<int (int)> root = [&] (int x) { while (parent[(size_t) x] != x) x = parent[(size_t) x] = parent[(size_t) parent[(size_t) x]]; return x; };
        for (int i = 0; i < imgs.size(); ++i)
            for (int j = i + 1; j < imgs.size(); ++j)
                if (ok[(size_t) i] && ok[(size_t) j] && similar::similarImages (h[(size_t) i], h[(size_t) j]))
                    parent[(size_t) root (j)] = root (i);
        std::map<int, Array<File>> groups;
        for (int i = 0; i < imgs.size(); ++i) groups[root (i)].add (imgs[i]);
        Array<File> list, extra;
        int count = 0;
        for (auto& [r, g] : groups)
        {
            if (g.size() < 2) continue;
            ++count;
            // nechať najväčší súbor (najviac detailov), ostatné označiť
            int keep = 0;
            for (int k = 1; k < g.size(); ++k) if (g[k].getSize() > g[keep].getSize()) keep = k;
            list.add (g[keep]);
            for (int k = 0; k < g.size(); ++k) if (k != keep) { list.add (g[k]); extra.add (g[k]); }
        }
        MessageManager::callAsync ([sp, base, list, extra, count, cancel]
        {
            if (sp == nullptr || *cancel) return;
            if (count == 0) { sp->message (tr ("Žiadne podobné fotky – každá je iná.")); return; }
            sp->grid.showList (base, list, tr ("Podobné fotky: ") + String (count) + tr (" sérií · ") + String (extra.size()) + tr (" navyše"), true);
            sp->grid.markFiles (extra);
            sp->repaint (0, 0, sp->getWidth(), topH);
            sp->updateFolderInfo();
            sp->message (tr ("V každej sérii je prvá fotka ponechaná, ostatné sú označené · C = porovnať · Del = do koša · Esc = späť"));
        });
    });
}

void MainComponent::findSimilarSounds (const File& reference)
{
    if (kindOf (reference) != Kind::Audio) return;
    if (searchCancel) *searchCancel = true;
    auto cancel = std::make_shared<std::atomic<bool>> (false);
    searchCancel = cancel;
    const auto base = grid.isListMode() ? grid.getFolder() : reference.getParentDirectory();
    message (tr ("Hľadám podobné zvuky v ") + base.getFileName() + tr (" a podpriečinkoch…"));
    Component::SafePointer<MainComponent> sp (this);
    Thread::launch ([sp, reference, base, cancel]
    {
        AudioFormatManager fm;
        registerAllFormats (fm);
        const auto ref = similar::soundFeatures (fm, reference);
        if (! ref.ok) return;
        std::vector<std::pair<File, std::vector<float>>> cand;
        int n = 0;
        for (const auto& e : RangedDirectoryIterator (base, true, "*", File::findFiles | File::ignoreHiddenFiles))
        {
            if (*cancel) return;
            const auto f = e.getFile();
            if (f == reference || kindOf (f) != Kind::Audio || e.getFileSize() > 60 * 1024 * 1024) continue;
            const auto sf = similar::soundFeatures (fm, f);
            if (sf.ok) cand.push_back ({ f, sf.v });
            if (++n % 25 == 0)
                MessageManager::callAsync ([sp, n] { if (sp != nullptr) sp->message (tr ("Porovnávam zvuky… ") + String (n)); });
            if (n >= 4000) break;
        }
        if (cand.empty()) return;
        // normalizovať každú vlastnosť (priemer / rozptyl), potom vzdialenosť
        const size_t dims = ref.v.size();
        std::vector<double> mean (dims, 0.0), sd (dims, 0.0);
        for (auto& c : cand) for (size_t d = 0; d < dims; ++d) mean[d] += c.second[d];
        for (auto& m : mean) m /= (double) cand.size();
        for (auto& c : cand) for (size_t d = 0; d < dims; ++d) sd[d] += std::pow (c.second[d] - mean[d], 2);
        for (auto& v : sd) v = std::sqrt (v / (double) cand.size()) + 1.0e-3;
        std::vector<std::pair<double, File>> scored;
        for (auto& c : cand)
        {
            double dist = 0;
            for (size_t d = 0; d < dims; ++d) dist += std::pow ((c.second[d] - ref.v[d]) / sd[d], 2);
            scored.push_back ({ dist, c.first });
        }
        std::sort (scored.begin(), scored.end(), [] (auto& a, auto& b) { return a.first < b.first; });
        Array<File> list { reference };
        for (size_t i = 0; i < scored.size() && i < 40; ++i) list.add (scored[i].second);
        MessageManager::callAsync ([sp, base, list, reference, cancel]
        {
            if (sp == nullptr || *cancel) return;
            sp->grid.showList (base, list, tr ("Podobné ako ") + reference.getFileName() + tr (" · najpodobnejšie hore"), true);
            sp->repaint (0, 0, sp->getWidth(), topH);
            sp->updateFolderInfo();
            sp->message (tr ("Prvý je pôvodný zvuk, pod ním najpodobnejšie · Esc = späť"));
        });
    });
}

void MainComponent::convertDialog()
{
    Array<File> files;
    bool anyImg = false, anyAudio = false;
    for (auto& f : grid.getMarkedFiles())
    {
        const auto k = kindOf (f);
        if (k == Kind::Image) { anyImg = true; files.add (f); }
        else if (k == Kind::Audio) { anyAudio = true; files.add (f); }
    }
    if (files.isEmpty()) { message (tr ("Previesť sa dajú obrázky a zvuky – označ ich."), true); return; }
    auto opts = convert::optionsFor (anyImg, anyAudio);
    convertWindow = std::make_unique<AlertWindow> (tr ("Previesť do iného formátu"),
        String (files.size()) + tr (" súborov · nové súbory sa uložia vedľa originálov, originály ostanú"), MessageBoxIconType::NoIcon);
    StringArray labels;
    for (auto& o : opts) labels.add ((o.audio ? tr ("Zvuk → ") : tr ("Obrázok → ")) + o.label);
    convertWindow->addComboBox ("fmt", labels, tr ("Formát"));
    if (auto* c = convertWindow->getComboBoxComponent ("fmt")) c->setSelectedItemIndex (jlimit (0, labels.size() - 1, settings().getIntValue ("convertFmt", 0)));
    convertWindow->addButton (tr ("Previesť"), 1, KeyPress (KeyPress::returnKey));
    convertWindow->addButton (tr ("Zrušiť"), 0, KeyPress (KeyPress::escapeKey));
    Component::SafePointer<MainComponent> sp (this);
    convertWindow->enterModalState (true, ModalCallbackFunction::create ([sp, files, opts] (int r)
    {
        if (sp == nullptr) return;
        const int sel = sp->convertWindow->getComboBoxComponent ("fmt")->getSelectedItemIndex();
        sp->convertWindow.reset();
        if (r != 1 || sel < 0) return;
        sp->settings().setValue ("convertFmt", sel);
        const auto target = opts[(size_t) sel];
        auto aliveFlag = sp->alive;
        auto* self = sp.getComponent();
        self->message (tr ("Prevádzam…"));
        self->editPool.addJob ([self, aliveFlag, files, target]
        {
            int ok = 0, skipped = 0; String err;
            Array<File> made;
            for (int i = 0; i < files.size(); ++i)
            {
                const auto& f = files[i];
                // len vhodné súbory (zvuk na zvuk, obrázok na obrázok)
                if ((kindOf (f) == Kind::Audio) != target.audio) { ++skipped; continue; }
                MessageManager::callAsync ([self, aliveFlag, i, n = files.size()]
                {
                    if (*aliveFlag) self->message (tr ("Prevádzam ") + String (i + 1) + "/" + String (n) + "…");
                });
                File out; String e;
                if (convert::run (f, target.target, out, e)) { ++ok; made.add (out); } else err = e;
            }
            MessageManager::callAsync ([self, aliveFlag, ok, skipped, err, made]
            {
                if (! *aliveFlag) return;
                self->grid.reload();
                if (! made.isEmpty()) self->grid.markFiles (made);
                String m = tr ("Prevedené: ") + String (ok);
                if (skipped > 0) m << tr (" · preskočené (iný druh): ") << skipped;
                if (err.isNotEmpty()) m << " · " << err;
                self->message (m, err.isNotEmpty());
                self->updateFolderInfo();
            });
        });
    }));
}

void MainComponent::exportList()
{
    const auto folder = grid.getFolder();
    Array<File> files;
    for (int i = 0; i < grid.getNumFiles(); ++i)
        if (grid.getKindAt (i) != Kind::Folder) files.add (grid.getFileAt (i));
    if (files.isEmpty()) { message (tr ("V priečinku nie sú súbory."), true); return; }
    auto out = folder.getNonexistentChildFile (tr ("zoznam_") + folder.getFileName(), ".csv", false);
    message (tr ("Pripravujem zoznam ") + String (files.size()) + tr (" súborov…"));
    Component::SafePointer<MainComponent> sp (this);
    auto* audio = &audioInfo;
    auto* metaStore = &meta;
    // hodnotenia sa čítajú tu (hlavné vlákno), zvuk na pozadí
    StringArray stars;
    for (auto& f : files) stars.add (String (metaStore->get (f).rating));
    Thread::launch ([sp, files, out, audio, stars]
    {
        auto q = [] (String s) { return "\"" + s.replace ("\"", "\"\"") + "\""; };
        String csv;
        const String sep = ";";
        csv << q (tr ("Názov")) << sep << q (tr ("Typ")) << sep << q (tr ("Veľkosť (kB)")) << sep << q (tr ("Dĺžka")) << sep
            << "BPM" << sep << q (tr ("Tónina")) << sep << q (tr ("Formát")) << sep << q (tr ("Hodnotenie")) << sep << q (tr ("Zmenené")) << "\r\n";
        for (int i = 0; i < files.size(); ++i)
        {
            const auto& f = files[i];
            String len, bpm, key, fmt;
            if (kindOf (f) == Kind::Audio)
            {
                const auto info = audio->computeNow (f);
                if (info.ok)
                {
                    const int t = roundToInt (info.length);
                    len = String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2);
                    if (info.bpm > 0) bpm = String (roundToInt (info.bpm));
                    key = info.key;
                    fmt = String (info.sampleRate / 1000.0, 1) + " kHz " + String (info.bits) + " bit";
                }
            }
            csv << q (f.getFileName()) << sep << q (f.getFileExtension().trimCharactersAtStart (".").toUpperCase()) << sep
                << String ((f.getSize() + 512) / 1024) << sep << q (len) << sep << bpm << sep << q (key) << sep << q (fmt) << sep
                << (stars[i] == "0" ? String() : stars[i]) << sep << q (f.getLastModificationTime().formatted ("%d.%m.%Y %H:%M")) << "\r\n";
        }
        // UTF-8 s BOM – Excel potom správne ukáže diakritiku
        MemoryOutputStream mo;
        mo.writeByte ((char) 0xef); mo.writeByte ((char) 0xbb); mo.writeByte ((char) 0xbf);
        mo.writeString (csv);
        const bool ok = out.replaceWithData (mo.getData(), mo.getDataSize() - 1);   // bez koncovej nuly
        MessageManager::callAsync ([sp, ok, out]
        {
            if (sp == nullptr) return;
            if (! ok) { sp->message (tr ("Zoznam sa nepodarilo uložiť."), true); return; }
            sp->grid.reload();
            sp->grid.selectFile (out);
            sp->message (tr ("Zoznam uložený: ") + out.getFileName() + tr (" · otvorí sa v Exceli / LibreOffice (Enter)"));
        });
    });
}

void MainComponent::printMarked()
{
    Array<File> files;
    for (auto& f : grid.getMarkedFiles()) if (f.existsAsFile()) files.add (f);
    if (files.isEmpty()) { message (tr ("Vyber súbor, ktorý chceš vytlačiť."), true); return; }
    void* owner = getPeer() != nullptr ? getPeer()->getNativeHandle() : nullptr;
    const auto r = win::printFiles (files, owner);
    if (r.cancelled && r.sent == 0) return;
    if (r.sent > 0) message (tr ("Odoslané na tlač: ") + String (r.sent) + (r.error.isNotEmpty() ? " · " + r.error : String()), r.error.isNotEmpty());
    else message (r.error.isNotEmpty() ? r.error : tr ("Tento súbor sa nedá vytlačiť (Windows preň nemá tlač)."), true);
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

    if (dual) return;   // v dvoch paneloch je náhľad skrytý a nič sa samo nespúšťa

    // zvuk: prehrať (zoznam = zvuky v priečinku); tá istá skladba sa nespúšťa odznova
    // (hneď po otvorení programu sa nič samo nespúšťa)
    if (kindOf (f) == Kind::Audio && f != player.getCurrent() && ! preview.quietNext)
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
    const bool isGrid = grid.getMode() == FileGrid::Mode::Grid;
    viewButton.setIcon (isGrid ? Icon::Grid : Icon::List);
    viewButton.setText (isGrid ? tr ("Mriežka") : tr ("Zoznam"));
    if (getWidth() > 0) resized();
}

Rectangle<int> MainComponent::pathArea() const
{
    return { upButton.getRight() + 10, 0, jmax (0, filterButton.getX() - 12 - upButton.getRight() - 10), topH };
}


void MainComponent::layoutLabelButton (IconButton& b, Rectangle<int>& row, bool fromRight)
{
    const int w = 7 + 18 + 5 + roundToInt (GlyphArrangement::getStringWidth (Font (FontOptions (12.5f)), b.getText())) + 12;
    b.setBounds ((fromRight ? row.removeFromRight (w + 6) : row.removeFromLeft (w + 6)).reduced (3, 8));
}

void MainComponent::updateSizeSlider()
{
    const bool isGrid = grid.getMode() == FileGrid::Mode::Grid;
    const double v = isGrid ? (grid.getThumbWidth() - FileGrid::minThumbW) / (double) (FileGrid::maxThumbW - FileGrid::minThumbW)
                            : (grid.getRowHeight() - FileGrid::minRowH) / (double) (FileGrid::maxRowH - FileGrid::minRowH);
    sizeSlider.setValue (v);
    sizeSlider.setTooltip (isGrid ? tr ("Veľkosť náhľadov (aj Ctrl + koliesko)") : tr ("Veľkosť riadkov v zozname (aj Ctrl + koliesko)"));
}

void MainComponent::copyMarkedPaths()
{
    auto files = activeGrid().getMarkedFiles();
    if (files.isEmpty() && activeGrid().getFolder() != File()) files.add (activeGrid().getFolder());
    if (files.isEmpty()) return;
    StringArray paths;
    for (auto& x : files) paths.add (x.getFullPathName());
    SystemClipboard::copyTextToClipboard (paths.joinIntoString ("\n"));
    message (tr ("Cesta skopírovaná do schránky"));
}

void MainComponent::saveViewSizes()
{
    settings().setValue ("thumbW", grid.getThumbWidth());
    settings().setValue ("rowH", grid.getRowHeight());
    settings().setValue ("listCols", grid.getColumns());
    settings().setValue ("listColW", grid.getColumnWidths());
}

void MainComponent::showPathMenu (const File& folder)
{
    if (folder == File()) return;
    PopupMenu m;
    m.addItem (1, tr ("Kopírovať cestu"));
    Component::SafePointer<MainComponent> sp (this);
    m.showMenuAsync (PopupMenu::Options().withMousePosition(), [sp, folder] (int r)
    {
        if (sp == nullptr || r != 1) return;
        SystemClipboard::copyTextToClipboard (folder.getFullPathName());
        sp->message (tr ("Cesta skopírovaná do schránky"));
    });
}

void MainComponent::showFilterMenu()
{
    PopupMenu m;
    for (int i = 0; i < typeBox.getNumItems(); ++i)
    {
        if (i == 6) m.addSeparator();   // priečinky, hodnotenie, štítok
        m.addItem (i + 1, typeBox.getItemText (i), true, typeBox.getSelectedItemIndex() == i);
    }
    Component::SafePointer<MainComponent> sp (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&filterButton), [sp] (int r)
    {
        if (sp == nullptr || r == 0) return;
        sp->typeBox.setSelectedItemIndex (r - 1, sendNotificationSync);
    });
}

void MainComponent::showSortMenu()
{
    PopupMenu m;
    for (int i = 0; i < sortBox.getNumItems(); ++i)
        m.addItem (i + 1, sortBox.getItemText (i), true, sortBox.getSelectedItemIndex() == i);
    if (sortBox.getSelectedItemIndex() < 0)
    {
        m.addSeparator();
        m.addItem (-1, tr ("Vlastné (podľa stĺpca v zozname)"), false, true);
    }
    PopupMenu cols;
    const std::pair<int, const char*> colItems[] = { { FileGrid::ColWave, "Vlna" }, { FileGrid::ColLength, "Dĺžka" }, { FileGrid::ColFormat, "Formát" },
                                                     { FileGrid::ColSize, "Veľkosť" }, { FileGrid::ColDate, "Zmenené" } };
    for (int i = 0; i < 5; ++i) cols.addItem (30 + i, tr (colItems[i].second), true, (grid.getColumns() & colItems[i].first) != 0);
    m.addSeparator();
    m.addSubMenu (tr ("Stĺpce v zozname"), cols);
    Component::SafePointer<MainComponent> sp (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&sortButton), [sp, colItems] (int r)
    {
        if (sp == nullptr || r <= 0) return;
        if (r >= 30 && r < 35)
        {
            auto& g = sp->grid;
            g.setColumns (g.getColumns() ^ colItems[r - 30].first);
            sp->grid2.setColumns (g.getColumns());
            if (g.getMode() != FileGrid::Mode::List) g.setMode (FileGrid::Mode::List);
            sp->saveViewSizes();
            g.grabKeyboardFocus();
            return;
        }
        if (sp->sortBox.getSelectedItemIndex() == r - 1) sp->applySortBox();
        else sp->sortBox.setSelectedItemIndex (r - 1, sendNotificationSync);
        sp->grid.grabKeyboardFocus();
    });
}

void MainComponent::prepareToQuit()
{
    // zvuk a video stíšiť hneď – zvyšok sa uprace, keď už okno nie je vidieť
    player.stop();
    preview.show ({});
    if (videoFull != nullptr) videoFull.reset();
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

    moveFilesTo (files, dir, grid);
}

bool MainComponent::moveFilesTo (const Array<File>& files, const File& dir, FileGrid& source)
{
    Batch batch;
    String lastError;
    bool anyDir = false;
    std::vector<Transfer::Item> slow;   // na iný disk = kopírovanie, to ide na pozadí
    const auto dirRoot = Transfer::rootOf (dir);
    for (auto& f : files)
    {
        if (f.getParentDirectory() == dir) { lastError = tr ("Súbor už je v priečinku ") + dir.getFileName() + "."; continue; }
        if (f.isDirectory() && (dir == f || dir.isAChildOf (f))) { lastError = tr ("Priečinok sa nedá presunúť sám do seba."); continue; }

        auto dest = dir.getChildFile (f.getFileName());
        if (dest.exists())
            dest = dir.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), true);

        preview.release (f);
        if (Transfer::rootOf (f) != dirRoot) { slow.push_back ({ f, dest }); continue; }
        const bool wasDir = f.isDirectory();
        if (! f.moveFileTo (dest))
        {
            lastError = tr ("Nepodarilo sa presunúť ") + f.getFileName() + tr (" (možno je otvorený v inom programe).");
            continue;
        }
        anyDir = anyDir || wasDir;
        afterMoved (f, dest, source);
        batch.push_back ({ f, dest });
    }
    source.clearMarks();

    if (! slow.empty())
    {
        FileGrid* src = &source;
        startTransfer (std::move (slow), true, [this, src, dir] (Transfer& t)
        {
            Batch b;
            for (auto& it : t.completed) { afterMoved (it.from, it.to, *src); b.push_back ({ it.from, it.to }); }
            if (! b.empty()) { undoStack.push_back (b); if (undoStack.size() > 200) undoStack.erase (undoStack.begin()); }
            if (dual) (src == &grid ? grid2 : grid).reload();
            tree.refreshFolder (dir);
            updateFolderInfo();
            String m = (t.wasCancelled() ? tr ("Presun zrušený · presunuté: ") : tr ("Presunuté: "))
                       + String ((int) t.completed.size()) + tr (" → ") + dir.getFileName() + tr (" · Ctrl+Z vráti");
            if (t.error.isNotEmpty()) m << " · " << t.error;
            message (m, t.error.isNotEmpty());
        });
    }

    if (batch.empty())
    {
        if (lastError.isNotEmpty()) message (lastError, true);
        return false;
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
    if (dual) { (&source == &grid ? grid2 : grid).reload(); }
    return true;
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
    m.addItem (12, tr ("Tlačiť…\tCtrl+P"), ! files.isEmpty());
    m.addItem (13, tr ("Porovnať fotky vedľa seba\tC"), anyResize);
    m.addItem (15, tr ("Previesť do iného formátu…"), ! files.isEmpty());
    m.addItem (14, tr ("Exportovať zoznam priečinka do tabuľky (CSV)…"));
    m.addItem (16, tr ("Nájsť podobné fotky (série záberov)"));
    m.addItem (19, tr ("Vytvoriť video z fotiek (MP4 s hudbou)…"));
    m.addItem (18, tr ("Otvoriť Korgus Sequencer (sample sem potom pretiahni myšou)"));
    m.addItem (17, tr ("Nájsť podobné zvuky ako tento"), kindOf (f) == Kind::Audio);
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
    m.addItem (8, tr ("Kopírovať cestu\tCtrl+Shift+C"));
    m.addSeparator();
    m.addItem (10, tr ("Nájsť duplikáty v priečinku"));

    Component::SafePointer<MainComponent> sp (this);
    // „Text\tSkratka“ → skratka zarovnaná vpravo
    std::function<void (PopupMenu&)> fix = [&fix] (PopupMenu& pm)
    {
        for (PopupMenu::MenuItemIterator it (pm); it.next();)
        {
            auto& item = it.getItem();
            if (item.text.containsChar ('\t'))
            {
                item.shortcutKeyDescription = item.text.fromFirstOccurrenceOf ("\t", false, false);
                item.text = item.text.upToFirstOccurrenceOf ("\t", false, false);
            }
            if (item.subMenu != nullptr) fix (*item.subMenu);
        }
    };
    fix (m);
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
        else if (r == 12) sp->printMarked();
        else if (r == 13) sp->openCompare();
        else if (r == 14) sp->exportList();
        else if (r == 15) sp->convertDialog();
        else if (r == 16) sp->findSimilarPhotos();
        else if (r == 18) sp->openSequencer();
        else if (r == 19) sp->videoDialog();
        else if (r == 17) sp->findSimilarSounds (f);
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
    if ((k == 'p' || k == 'P') && mods.isCtrlDown()) { printMarked(); return true; }
    if ((k == 'c' || k == 'C') && ! mods.isAnyModifierKeyDown()) { openCompare(); return true; }
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

    if (k == KeyPress::escapeKey && transfer != nullptr) { transfer->cancel(); return true; }
    if (k == KeyPress::escapeKey && videoExport != nullptr) { videoExport->cancel(); return true; }
    if (k == KeyPress::escapeKey && grid.isListMode()) { goUp(); return true; }
    if (k == KeyPress::F9Key) { setDual (! dual); return true; }
    if (mods.isCtrlDown() && mods.isShiftDown() && (k == 'c' || k == 'C')) { copyMarkedPaths(); return true; }
    if (mods.isCtrlDown() && (k == 't' || k == 'T')) { addTab(); return true; }
    if (mods.isCtrlDown() && (k == 'w' || k == 'W')) { closeTab (tabs.getCurrent()); return true; }
    if (mods.isCtrlDown() && k == KeyPress::tabKey)
    {
        const int n = tabs.getTabs().size();
        if (n > 1) selectTab ((tabs.getCurrent() + (mods.isShiftDown() ? n - 1 : 1)) % n);
        return true;
    }
    if (dual)
    {
        if (k == KeyPress::F5Key) { transferToOther (false); return true; }
        if (k == KeyPress::F6Key) { transferToOther (true); return true; }
        if (k == KeyPress::tabKey) { auto& o = otherGrid(); lastFocused = &o; o.grabKeyboardFocus(); repaint(); return true; }
        if (lastFocused == &grid2) return false;   // ostatné skratky platia pre ľavý panel
    }
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
