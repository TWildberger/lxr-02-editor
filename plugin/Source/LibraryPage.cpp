#include "LibraryPage.h"

#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "kitforge/KitOps.h"
#include "kitforge/Pattern.h"
#include "kitforge/SysEx.h"

namespace kitforge
{

namespace
{
const std::pair<const char*, std::optional<FileKind>> kKinds[] = {
    { "All types", std::nullopt },  { "Kits", FileKind::Kit },       { "Patterns", FileKind::Pattern },
    { "Songs", FileKind::Song },    { "Globals", FileKind::Globals },
};

juce::String joinTags(const std::vector<std::string>& tags)
{
    juce::StringArray a;
    for (const auto& t : tags)
        a.add(t);
    return a.joinIntoString(", ");
}
} // namespace

LibraryPage::LibraryPage(KitForgeProcessor& p) : proc_(p)
{
    search_.setTextToShowWhenEmpty("Search name, tags, notes, source...", colours::dim);
    search_.onTextChange = [this] { refreshList(); };
    addAndMakeVisible(search_);
    for (int i = 0; i < (int) std::size(kKinds); ++i)
        kind_.addItem(kKinds[i].first, i + 1);
    kind_.setSelectedId(1, juce::dontSendNotification);
    kind_.onChange = [this] { refreshList(); };
    addAndMakeVisible(kind_);
    tag_.onChange = [this] { refreshList(); };
    addAndMakeVisible(tag_);
    favs_.onClick = [this] { refreshList(); };
    addAndMakeVisible(favs_);

    auto& h = table_.getHeader();
    h.addColumn(juce::CharPointer_UTF8("\xe2\x98\x85"), ColFav, 28, 28, 28, juce::TableHeaderComponent::notResizable);
    h.addColumn("Name", ColName, 150);
    h.addColumn("Type", ColKind, 70);
    h.addColumn("Tags", ColTags, 170);
    h.addColumn("Source", ColSource, 260);
    h.addColumn("Added", ColAdded, 120);
    table_.setModel(this);
    table_.setMultipleSelectionEnabled(true);
    table_.setRowHeight(22);
    addAndMakeVisible(table_);

    details_.setMultiLine(true);
    details_.setReadOnly(true);
    details_.setScrollbarsShown(true);
    details_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain));
    addAndMakeVisible(details_);
    count_.setColour(juce::Label::textColourId, colours::dim);
    addAndMakeVisible(count_);

    importFiles_.onClick = [this] { importFiles(); };
    importFolder_.onClick = [this] { importFolder(); };
    addKit_.onClick = [this] { addCurrentKit(); };
    load_.onClick = [this] { loadSelected(); };
    diff_.onClick = [this] { diffSelected(); };
    export_.onClick = [this] { exportSelected(); };
    rename_.onClick = [this] { renameSelected(); };
    tags_.onClick = [this] { editTags(); };
    fav_.onClick = [this] { toggleFavorite(); };
    delete_.onClick = [this] { removeSelected(); };
    send_.onClick = [this] { sendSelected(); };
    load_.setTooltip("Load a kit into the editor, apply global settings, or send a pattern to the active pattern (extended firmware)");
    diff_.setTooltip("Compare one kit with the editor, or two kits with each other");
    send_.setTooltip("Write the selected kits/patterns to consecutive device slots (extended firmware)");
    for (auto* b : { &importFiles_, &importFolder_, &addKit_, &load_, &diff_, &export_, &rename_, &tags_, &fav_, &delete_, &send_ })
        addAndMakeVisible(*b);

    proc_.deviceChanged.addChangeListener(this);
    if (!proc_.library())
        details_.setText("The library database could not be opened:\n" + proc_.libraryPath() + "\n\n" + proc_.lastProblem());
    refreshTags();
    refreshList();
}

LibraryPage::~LibraryPage() { proc_.deviceChanged.removeChangeListener(this); }

void LibraryPage::changeListenerCallback(juce::ChangeBroadcaster*) { send_.setEnabled(proc_.device().isExtended()); }

void LibraryPage::refreshTags()
{
    const auto current = tag_.getText();
    tag_.clear(juce::dontSendNotification);
    tag_.addItem("Any tag", 1);
    if (auto* lib = proc_.library())
    {
        int id = 2;
        for (const auto& t : lib->allTags())
            tag_.addItem(t, id++);
    }
    int sel = 1;
    for (int i = 0; i < tag_.getNumItems(); ++i)
        if (tag_.getItemText(i) == current)
            sel = tag_.getItemId(i);
    tag_.setSelectedId(sel, juce::dontSendNotification);
}

void LibraryPage::refreshList()
{
    std::set<int64_t> selected;
    for (auto id : selectedIds())
        selected.insert(id);
    items_.clear();
    if (auto* lib = proc_.library())
    {
        SearchQuery q;
        q.text = search_.getText().trim().toStdString();
        q.kind = kKinds[juce::jlimit(0, (int) std::size(kKinds) - 1, kind_.getSelectedId() - 1)].second;
        q.favoritesOnly = favs_.getToggleState();
        if (tag_.getSelectedId() > 1)
            q.tags.push_back(tag_.getText().toStdString());
        q.limit = 5000;
        items_ = lib->search(q);
        count_.setText(juce::String(items_.size()) + " shown, " + juce::String(lib->count()) + " in library", juce::dontSendNotification);
    }
    table_.updateContent();
    juce::SparseSet<int> rows;
    for (int i = 0; i < (int) items_.size(); ++i)
        if (selected.count(items_[(size_t) i].id))
            rows.addRange({ i, i + 1 });
    table_.setSelectedRows(rows, juce::dontSendNotification);
    table_.repaint();
    send_.setEnabled(proc_.device().isExtended());
}

std::vector<int64_t> LibraryPage::selectedIds() const
{
    std::vector<int64_t> ids;
    for (int i = 0; i < table_.getNumSelectedRows(); ++i)
    {
        const int row = table_.getSelectedRow(i);
        if (row >= 0 && row < (int) items_.size())
            ids.push_back(items_[(size_t) row].id);
    }
    return ids;
}

void LibraryPage::paintRowBackground(juce::Graphics& g, int, int, int, bool selected)
{
    if (selected)
        g.fillAll(colours::accent.withAlpha(0.35f));
}

void LibraryPage::paintCell(juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (row < 0 || row >= (int) items_.size())
        return;
    const auto& it = items_[(size_t) row];
    juce::String s;
    g.setColour(colours::text);
    switch (col)
    {
        case ColFav:
            g.setColour(it.favorite ? colours::accent : colours::outline);
            s = juce::CharPointer_UTF8(it.favorite ? "\xe2\x98\x85" : "\xe2\x98\x86");
            break;
        case ColName: s = it.name; break;
        case ColKind: s = kindName(it.kind); break;
        case ColTags: s = joinTags(it.tags); break;
        case ColSource: g.setColour(colours::dim); s = it.source; break;
        case ColAdded: g.setColour(colours::dim); s = juce::Time(it.createdAt * 1000).formatted("%Y-%m-%d %H:%M"); break;
        default: break;
    }
    g.setFont(juce::FontOptions(13.0f));
    g.drawText(s, 4, 0, w - 6, h, col == ColFav ? juce::Justification::centred : juce::Justification::centredLeft, true);
}

void LibraryPage::cellClicked(int row, int col, const juce::MouseEvent&)
{
    if (col == ColFav && row >= 0 && row < (int) items_.size())
        if (auto* lib = proc_.library())
        {
            lib->setFavorite(items_[(size_t) row].id, !items_[(size_t) row].favorite);
            refreshList();
        }
}

void LibraryPage::cellDoubleClicked(int, int col, const juce::MouseEvent&)
{
    if (col != ColFav)
        loadSelected();
}

void LibraryPage::selectedRowsChanged(int) { showDetails(); }

void LibraryPage::showDetails()
{
    auto* lib = proc_.library();
    const auto ids = selectedIds();
    if (!lib || ids.empty())
    {
        details_.clear();
        return;
    }
    if (ids.size() > 1)
    {
        details_.setText(juce::String(ids.size()) + " items selected.");
        return;
    }
    const auto item = lib->get(ids.front());
    if (!item)
        return;
    juce::String s;
    s << item->name << "  (" << kindName(item->kind) << ")\n";
    s << "Source: " << item->source << "\n";
    if (!item->tags.empty())
        s << "Tags: " << joinTags(item->tags) << "\n";
    if (!item->notes.empty())
        s << "Notes: " << item->notes << "\n";
    s << "Size: " << (int) item->data.size() << " bytes, hash " << juce::String(item->hash).substring(0, 12) << "\n\n";
    if (item->kind == FileKind::Kit)
    {
        const auto kit = Kit::fromSnd(item->data);
        const auto current = proc_.currentKit();
        const auto d = diffKits(current, kit);
        const bool sameExtension = current.extension() == kit.extension();
        if (d.empty() && sameExtension)
            s << "Identical to the kit in the editor.\n";
        else
        {
            s << d.size() << " parameter(s) differ from the editor kit (editor -> library):\n";
            for (const auto& line : describeDiff(d))
                s << "  " << line << "\n";
        }
        if (!sameExtension)
            s << "Kit extension differs (including engine opt-in version).\n";
    }
    else if (item->kind == FileKind::Pattern)
    {
        std::string err;
        if (auto pat = Pattern::fromPat(item->data, &err))
        {
            s << "Active steps: " << pat->activeStepCount() << "\n";
            for (int t = 0; t < Pattern::kTracks; ++t)
            {
                s << "  Track " << (t + 1) << ": ";
                const auto& tr = pat->tracks[(size_t) t];
                for (int st = 0; st < PatternTrack::kSteps && st < 128; ++st)
                {
                    if (st && st % 16 == 0)
                        s << "\n            ";
                    s << (tr.steps[(size_t) st].active() ? "x" : ".");
                }
                s << "\n";
            }
        }
        else
            s << "Cannot parse pattern: " << err << "\n";
    }
    details_.setText(s);
}

void LibraryPage::addCurrentKit()
{
    auto* lib = proc_.library();
    if (!lib)
        return;
    const auto r = lib->addKit(proc_.currentKit(), "editor");
    if (r)
        proc_.addLog(r->duplicate ? "Kit is already in the library" : "Added \"" + proc_.kitName() + "\" to the library");
    refreshList();
}

void LibraryPage::importFiles()
{
    chooser_ = std::make_unique<juce::FileChooser>("Import kits, patterns, songs or global settings", juce::File(), "*.snd;*.pat;*.sng;*.cfg");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this](const juce::FileChooser& fc) {
                              auto* lib = proc_.library();
                              if (!lib || fc.getResults().isEmpty())
                                  return;
                              int added = 0, dup = 0;
                              juce::StringArray errors;
                              for (const auto& f : fc.getResults())
                              {
                                  std::string err;
                                  if (auto r = lib->importFile(f.getFullPathName().toStdString(), &err))
                                      (r->duplicate ? dup : added)++;
                                  else
                                      errors.add(f.getFileName() + ": " + juce::String(err));
                              }
                              refreshTags();
                              refreshList();
                              showMessage("Import", juce::String(added) + " added, " + juce::String(dup) + " already in the library"
                                                        + (errors.isEmpty() ? juce::String() : "\n\n" + errors.joinIntoString("\n")));
                          });
}

void LibraryPage::importFolder()
{
    chooser_ = std::make_unique<juce::FileChooser>("Import an SD card or folder (searched recursively)");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this](const juce::FileChooser& fc) {
                              auto* lib = proc_.library();
                              const auto dir = fc.getResult();
                              if (!lib || !dir.isDirectory())
                                  return;
                              const auto rep = lib->importFolder(dir.getFullPathName().toStdString());
                              refreshTags();
                              refreshList();
                              juce::String msg;
                              msg << rep.added << " added, " << rep.duplicates << " duplicates skipped, " << rep.failed << " failed";
                              for (size_t i = 0; i < rep.messages.size() && i < 20; ++i)
                                  msg << "\n" << rep.messages[i];
                              showMessage("Import " + dir.getFileName(), msg);
                          });
}

void LibraryPage::loadSelected()
{
    auto* lib = proc_.library();
    const auto ids = selectedIds();
    if (!lib || ids.size() != 1)
        return;
    const auto item = lib->get(ids.front());
    if (!item)
        return;
    switch (item->kind)
    {
        case FileKind::Kit:
        {
            Kit k = Kit::fromSnd(item->data);
            if (k.name() != item->name)
                k.setName(item->name);
            proc_.loadKit(k, KitForgeProcessor::LoadOrigin::User);
            proc_.addLog("Loaded kit \"" + juce::String(item->name) + "\"");
            break;
        }
        case FileKind::Globals:
            proc_.loadGlobals(Globals::fromCfg(item->data), false);
            proc_.addLog("Loaded global settings \"" + juce::String(item->name) + "\" (see the Globals page)");
            break;
        case FileKind::Pattern:
        {
            if (!proc_.device().isExtended())
            {
                showMessage("Pattern", "Sending patterns needs the extended firmware. Use Export to copy the .pat file to the SD card.");
                break;
            }
            const auto raw = ext::patternRawFromPat(item->data);
            if (!raw)
            {
                showMessage("Pattern", "This pattern file cannot be converted.");
                break;
            }
            auto& proc = proc_;
            const juce::String name = item->name;
            proc_.device().sendPattern(ext::kActivePattern, *raw, 0, [&proc, name](bool ok, const juce::String& msg) {
                proc.addLog(ok ? "Sent pattern \"" + name + "\" to the active pattern" : "Sending pattern failed: " + msg);
            });
            break;
        }
        case FileKind::Song:
        case FileKind::ProjectInfo:
        case FileKind::Unknown:
            showMessage("Load", "Songs and project files can be exported, but not loaded into the editor.");
            break;
    }
}

void LibraryPage::diffSelected()
{
    auto* lib = proc_.library();
    const auto ids = selectedIds();
    if (!lib || ids.empty() || ids.size() > 2)
    {
        showMessage("Diff", "Select one kit to compare with the editor, or two kits to compare with each other.");
        return;
    }
    const auto a = lib->get(ids[0]);
    const auto b = ids.size() == 2 ? lib->get(ids[1]) : std::optional<LibraryItem>();
    if (!a || a->kind != FileKind::Kit || (ids.size() == 2 && (!b || b->kind != FileKind::Kit)))
    {
        showMessage("Diff", "Only kits can be compared.");
        return;
    }
    const Kit ka = ids.size() == 2 ? Kit::fromSnd(a->data) : proc_.currentKit();
    const Kit kb = ids.size() == 2 ? Kit::fromSnd(b->data) : Kit::fromSnd(a->data);
    const juce::String na = ids.size() == 2 ? juce::String(a->name) : "editor", nb = ids.size() == 2 ? juce::String(b->name) : juce::String(a->name);
    const auto d = diffKits(ka, kb);
    juce::String s;
    s << na << " -> " << nb << ": " << (int) d.size() << " difference(s)\n\n";
    for (const auto& line : describeDiff(d))
        s << line << "\n";
    if (ka.extension() != kb.extension())
        s << "Kit extension differs (including engine opt-in version).\n";
    details_.setText(s);
}

void LibraryPage::exportSelected()
{
    const auto ids = selectedIds();
    if (ids.empty())
        return;
    chooser_ = std::make_unique<juce::FileChooser>("Export to folder (files are numbered like the device saves them)");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, ids](const juce::FileChooser& fc) {
                              auto* lib = proc_.library();
                              const auto dir = fc.getResult();
                              if (!lib || !dir.isDirectory())
                                  return;
                              std::map<FileKind, int> next;
                              juce::StringArray out;
                              for (auto id : ids)
                              {
                                  const auto item = lib->get(id);
                                  if (!item)
                                      continue;
                                  std::string err;
                                  if (auto path = lib->exportItem(id, dir.getFullPathName().toStdString(), next[item->kind]++, &err))
                                      out.add(juce::File(*path).getFileName());
                                  else
                                      out.add(juce::String(item->name) + ": " + juce::String(err));
                              }
                              showMessage("Export", out.joinIntoString("\n"));
                          });
}

void LibraryPage::renameSelected()
{
    auto* lib = proc_.library();
    const auto ids = selectedIds();
    if (!lib || ids.size() != 1)
        return;
    const auto item = lib->get(ids.front());
    if (!item)
        return;
    dialog_ = std::make_unique<juce::AlertWindow>("Rename", "Names are stored with up to 8 characters on the device.", juce::MessageBoxIconType::NoIcon);
    dialog_->addTextEditor("name", item->name);
    dialog_->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const int64_t id = item->id;
    dialog_->enterModalState(true, juce::ModalCallbackFunction::create([this, id](int r) {
        if (r == 1)
            if (auto* l = proc_.library())
            {
                l->rename(id, dialog_->getTextEditorContents("name").trim().toStdString());
                refreshList();
            }
        dialog_.reset();
    }));
}

void LibraryPage::editTags()
{
    auto* lib = proc_.library();
    const auto ids = selectedIds();
    if (!lib || ids.empty())
        return;
    std::set<std::string> common;
    bool first = true;
    for (auto id : ids)
        if (const auto item = lib->get(id))
        {
            std::set<std::string> t(item->tags.begin(), item->tags.end());
            if (first)
                common = t;
            else
            {
                std::set<std::string> keep;
                std::set_intersection(common.begin(), common.end(), t.begin(), t.end(), std::inserter(keep, keep.end()));
                common = keep;
            }
            first = false;
        }
    juce::StringArray cur;
    for (const auto& t : common)
        cur.add(t);
    dialog_ = std::make_unique<juce::AlertWindow>("Tags", "Comma-separated tags for " + juce::String(ids.size()) + " item(s).",
                                                  juce::MessageBoxIconType::NoIcon);
    dialog_->addTextEditor("tags", cur.joinIntoString(", "));
    dialog_->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog_->enterModalState(true, juce::ModalCallbackFunction::create([this, ids, common](int r) {
        if (r == 1)
            if (auto* l = proc_.library())
            {
                std::set<std::string> wanted;
                for (auto t : juce::StringArray::fromTokens(dialog_->getTextEditorContents("tags"), ",", ""))
                    if (t.trim().isNotEmpty())
                        wanted.insert(t.trim().toLowerCase().toStdString());
                for (auto id : ids)
                {
                    for (const auto& t : common)
                        if (!wanted.count(t))
                            l->removeTag(id, t);
                    for (const auto& t : wanted)
                        l->addTag(id, t);
                }
                refreshTags();
                refreshList();
                showDetails();
            }
        dialog_.reset();
    }));
}

void LibraryPage::toggleFavorite()
{
    auto* lib = proc_.library();
    if (!lib)
        return;
    bool allFav = true;
    for (int i = 0; i < table_.getNumSelectedRows(); ++i)
        allFav &= items_[(size_t) table_.getSelectedRow(i)].favorite;
    for (auto id : selectedIds())
        lib->setFavorite(id, !allFav);
    refreshList();
}

void LibraryPage::removeSelected()
{
    const auto ids = selectedIds();
    if (ids.empty())
        return;
    juce::Component::SafePointer<LibraryPage> safe(this);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Delete",
                                       "Remove " + juce::String(ids.size()) + " item(s) from the library? Files on disk are not touched.",
                                       "Delete", "Cancel", this, juce::ModalCallbackFunction::create([safe, ids](int r) {
                                           if (!safe || r != 1)
                                               return;
                                           if (auto* l = safe->proc_.library())
                                               for (auto id : ids)
                                                   l->remove(id);
                                           safe->refreshTags();
                                           safe->refreshList();
                                       }));
}

void LibraryPage::sendSelected()
{
    const auto ids = selectedIds();
    if (ids.empty() || !proc_.device().isExtended())
        return;
    dialog_ = std::make_unique<juce::AlertWindow>("Send to device",
                                                  "Kits and patterns are written to consecutive slots starting at the slot below "
                                                  "(each type counts separately). Global settings are applied directly.",
                                                  juce::MessageBoxIconType::NoIcon);
    dialog_->addTextEditor("slot", "1", "First slot (1-64)");
    dialog_->addComboBox("save", { "Keep in device RAM only", "Also save to the SD card" }, "Storage");
    dialog_->addButton("Send", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog_->enterModalState(true, juce::ModalCallbackFunction::create([this, ids](int r) {
        const int first = dialog_->getTextEditorContents("slot").getIntValue() - 1;
        const int flags = dialog_->getComboBoxComponent("save")->getSelectedItemIndex() == 1 ? ext::kFlagSave : 0;
        dialog_.reset();
        auto* lib = proc_.library();
        if (r != 1 || !lib)
            return;
        if (first < 0 || first >= ext::kNumSlots)
        {
            showMessage("Send to device", "The slot must be between 1 and 64.");
            return;
        }
        auto& proc = proc_;
        int kitSlot = first, patSlot = first, sent = 0;
        juce::StringArray skipped;
        for (auto id : ids)
        {
            const auto item = lib->get(id);
            if (!item)
                continue;
            const juce::String name = item->name;
            auto report = [&proc, name](int slot) {
                return [&proc, name, slot](bool ok, const juce::String& msg) {
                    proc.addLog(ok ? "Slot " + juce::String(slot + 1) + ": " + name : "Slot " + juce::String(slot + 1) + " (" + name + ") failed: " + msg);
                };
            };
            if (item->kind == FileKind::Kit && kitSlot < ext::kNumSlots)
            {
                Kit k = Kit::fromSnd(item->data);
                if (k.name() != item->name)
                    k.setName(item->name);
                proc_.device().sendKit(kitSlot, k, flags, report(kitSlot));
                ++kitSlot;
                ++sent;
            }
            else if (item->kind == FileKind::Pattern && patSlot < ext::kNumSlots)
            {
                if (const auto raw = ext::patternRawFromPat(item->data))
                {
                    proc_.device().sendPattern(patSlot, *raw, flags, report(patSlot));
                    ++patSlot;
                    ++sent;
                }
                else
                    skipped.add(name);
            }
            else if (item->kind == FileKind::Globals)
            {
                proc_.device().sendGlobals(Globals::fromCfg(item->data), flags, [&proc, name](bool ok, const juce::String& msg) {
                    proc.addLog(ok ? "Global settings \"" + name + "\" sent" : "Sending global settings failed: " + msg);
                });
                ++sent;
            }
            else
                skipped.add(name);
        }
        proc_.addLog("Queued " + juce::String(sent) + " transfer(s); progress is shown in the Device log");
        if (!skipped.isEmpty())
            showMessage("Send to device", "Skipped (unsupported type or no free slot): " + skipped.joinIntoString(", "));
    }));
}

void LibraryPage::showMessage(const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, title, text, "OK", this);
}

void LibraryPage::resized()
{
    auto r = getLocalBounds().reduced(8);
    auto top = r.removeFromTop(26);
    search_.setBounds(top.removeFromLeft(300));
    top.removeFromLeft(6);
    kind_.setBounds(top.removeFromLeft(120));
    top.removeFromLeft(6);
    tag_.setBounds(top.removeFromLeft(140));
    top.removeFromLeft(6);
    favs_.setBounds(top.removeFromLeft(130));
    count_.setBounds(top);
    r.removeFromTop(6);

    auto buttons = r.removeFromBottom(28);
    for (auto* b : { &importFiles_, &importFolder_, &addKit_ })
    {
        b->setBounds(buttons.removeFromLeft(124));
        buttons.removeFromLeft(4);
    }
    buttons.removeFromLeft(12);
    for (auto* b : { &load_, &diff_, &fav_, &tags_, &rename_, &export_, &delete_ })
    {
        b->setBounds(buttons.removeFromLeft(juce::jmax(60, buttons.getWidth() / 10)));
        buttons.removeFromLeft(4);
    }
    send_.setBounds(buttons.removeFromRight(130));
    r.removeFromBottom(6);
    details_.setBounds(r.removeFromRight(juce::jmin(380, r.getWidth() / 3)));
    r.removeFromRight(6);
    table_.setBounds(r);
}

} // namespace kitforge
