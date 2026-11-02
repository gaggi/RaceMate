// The open layout's own page, right of the overlay list: its name, when it is active, its preview, its overlays'
// places, sharing and removing it. Layouts are added and ordered in the list.
Layout* FindLayout(int id) { for(auto& l:app.layouts) if(l.id==id) return &l; return nullptr; }
// A layout file to write or read; empty when cancelled.
std::wstring LayoutFile(bool save,std::wstring name) {
    for(wchar_t& c:name) if(wcschr(L"\\/:*?\"<>|",c)) c=L'-';
    wchar_t file[MAX_PATH]{}; if(save) wcsncpy_s(file,name.c_str(),_TRUNCATE);
    OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner=app.main; dialog.lpstrFile=file; dialog.nMaxFile=MAX_PATH;
    dialog.lpstrFilter=L"RaceMate layouts (*.racemate-layout)\0*.racemate-layout\0All files\0*.*\0"; dialog.lpstrDefExt=L"racemate-layout";
    dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    return (save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))?std::wstring(file):std::wstring();
}
void ExportLayoutChoice(int id) {
    const Layout* layout=FindLayout(id); if(!layout) return;
    const auto path=LayoutFile(true,layout->name); if(path.empty()) return;
    if(!ExportLayout(id,path)) MessageBoxW(app.main,(L"The layout could not be written to "+path+L".").c_str(),L"RaceMate",MB_OK|MB_ICONWARNING);
}
// A layout from a file, opened; 0 when cancelled or unreadable.
int ImportLayoutChoice() {
    const auto path=LayoutFile(false,L""); if(path.empty()) return 0;
    // Another monitor can make the old places wrong; the overlays can also start where new ones do.
    const int answer=MessageBoxW(app.main,L"Place the overlays where they were when the layout was exported?\n\nNo puts them at their default places, as for new overlays.",L"RaceMate",MB_YESNOCANCEL|MB_ICONQUESTION);
    if(answer==IDCANCEL) return 0;
    const int id=ImportLayout(path,answer==IDYES);
    if(!id) MessageBoxW(app.main,L"This file is not a layout exported from RaceMate.",L"RaceMate",MB_OK|MB_ICONWARNING);
    return id;
}
// After RemoveOverlayChoice: its page goes first, as it works on the overlay; an open one then opens the next.
void RemoveOverlayNow(Overlay* target) {
    if(std::none_of(app.overlays.begin(),app.overlays.end(),[&](const Overlay& o){return &o==target;})) return;
    const bool open=ui.page==Page::Overlay && ui.overlay==target;
    if(open) { ShowPage(Page::Overlays); ui.overlay=nullptr; }
    RemoveInstance(*target); SettingChanged();
    if(open) OpenOverlays(); else RefreshPage();
}
// Asks first; afterwards the next layout's first overlay opens, or the empty layouts page.
void RemoveLayoutChoice(int id) {
    const Layout* layout=FindLayout(id); if(!layout) return;
    const auto question=L"Remove the layout "+layout->name+L" with its overlays and their settings?";
    if(MessageBoxW(app.main,question.c_str(),L"RaceMate",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) return;
    if(ui.page==Page::Overlay) ShowPage(Page::Overlays);
    RemoveLayout(id); OpenOverlays();
}
class OverlaysPage : public PageWindow {
public:
    OverlaysPage() : PageWindow(ui.heading,ui.text) {}
private:
    enum class Key { None, Name, Condition, Session, Preview, Reset, Export, Remove };
    static constexpr int ListId=100, NameEditId=200;
    static constexpr const wchar_t* SessionChoices[]={L"Any",L"Practice",L"Qualifying",L"Race"};

    void OnCreate() override { list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont()); editors_.Attach(list_,Instance(),TextFont()); Refresh(); }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,width,height); }
    void Refresh() {
        std::vector<RowList::Row> rows; keys_.clear();
        auto push=[&](RowList::Row row,Key key) { rows.push_back(std::move(row)); keys_.push_back(key); };
        auto header=[&](const wchar_t* title) { RowList::Row row; row.header=true; row.title=title; push(std::move(row),Key::None); };
        const Layout* l=FindLayout(app.layout);
        if(!l) {
            RowList::Row none; none.title=L"No layouts"; none.detail=L"Add one with Add layout in the list."; push(std::move(none),Key::None);
            list_.SetRows(std::move(rows)); return;
        }
        // The name first, as on an overlay's page; its field opens under it.
        RowList::Row name; name.title=L"Name"; name.detail=l->name; name.iconButtons={naming_?Glyph::Collapse:Glyph::Expand}; name.expandHeight=naming_?WindowNameHeight:0;
        push(std::move(name),Key::Name);
        header(L"When it shows");
        RowList::Row when; when.title=L"Active when"; when.detail=ConditionDetails[l->condition]; when.choices.assign(std::begin(ConditionNames),std::end(ConditionNames)); when.choice=l->condition;
        push(std::move(when),Key::Condition);
        RowList::Row session; session.title=L"Session"; session.choices.assign(std::begin(SessionChoices),std::end(SessionChoices)); session.choice=l->session;
        session.detail=l->session?L"Only "+std::wstring(SessionPhrases[l->session]):L"In every kind of session";
        push(std::move(session),Key::Session);
        RowList::Row preview; preview.title=L"Preview";
        preview.detail=LayoutLive(l->id)?L"Active now, with live telemetry":L"Its overlays with sample data, while iRacing is not running it";
        preview.button=LayoutPreviewRequested(l->id)?L"Stop preview":L"Preview"; preview.buttonEnabled=!LayoutLive(l->id) || LayoutPreviewRequested(l->id);
        push(std::move(preview),Key::Preview);
        header(L"Overlays");
        RowList::Row reset; reset.title=L"Reset positions"; reset.detail=L"Every overlay of "+l->name+L" back to its default place."; reset.button=L"Reset";
        push(std::move(reset),Key::Reset);
        header(L"Layout");
        RowList::Row share; share.title=L"Export"; share.detail=L"Save it to a file, to share it or take it to another computer."; share.button=L"Export...";
        push(std::move(share),Key::Export);
        RowList::Row remove; remove.title=L"Remove layout"; remove.detail=L"With its overlays and their settings."; remove.button=L"Remove..."; remove.buttonDanger=true;
        push(std::move(remove),Key::Remove);
        list_.SetRows(std::move(rows));
    }
    // The name field in the first row.
    void BuildEditor() {
        editors_.Clear();
        const Layout* l=FindLayout(app.layout); if(!naming_ || !l) return;
        syncing_=true; editors_.Begin(0);
        editors_.Label(L"Name",0,10,84); SendMessageW(editors_.Edit(NameEditId,l->name,88,10,410),EM_SETLIMITTEXT,127,0);
        syncing_=false; editors_.Position();
    }
    // Typed names show at once, in the title and the list; an empty one keeps the last.
    void NameCommand(int code) {
        Layout* l=FindLayout(app.layout); if(!l || syncing_) return;
        if(code==EN_CHANGE) {
            auto name=editors_.Text(NameEditId);
            const auto first=name.find_first_not_of(L" \t"); if(first==std::wstring::npos) return;
            l->name=name.substr(first,name.find_last_not_of(L" \t")-first+1).substr(0,127); Save();
            SetWindowTextW(ui.title,l->name.c_str());
            if(ui.pane) SendMessageW(ui.pane,WM_COMMAND,MAKEWPARAM(PageRefreshId,0),0);
        }
        if(code==EN_KILLFOCUS) Refresh();
    }
    bool OnCommand(int id,int code,HWND) override {
        if(id==PageRefreshId) { Refresh(); return true; }
        if(id==NameEditId) { NameCommand(code); return true; }
        if(id!=ListId) return false;
        if(code==RowList::kLayoutChanged) { editors_.Position(); return true; }
        const int row=list_.NotifiedRow();
        if(row<0 || static_cast<size_t>(row)>=keys_.size()) return true;
        Layout* l=FindLayout(app.layout); if(!l) return true;
        const bool pressed=code==RowList::kButton;
        // Each choice below may rebuild the pages, this one included: nothing of it is used afterwards.
        switch(keys_[row]) {
        case Key::Name: if(code==RowList::kIconButton || code==RowList::kActivated) { naming_=!naming_; Refresh(); BuildEditor(); } break;
        case Key::Condition: if(code==RowList::kChoice) { l->condition=list_.NotifiedChoice(); ReconsiderLayout(); Save(); RefreshPage(); } break;
        case Key::Session: if(code==RowList::kChoice) { l->session=list_.NotifiedChoice(); ReconsiderLayout(); Save(); RefreshPage(); } break;
        case Key::Preview: if(pressed) ToggleLayoutPreview(l->id); break;
        case Key::Reset: if(pressed) ResetPositions(); break;
        case Key::Export: if(pressed) ExportLayoutChoice(l->id); break;
        case Key::Remove: if(pressed) RemoveLayoutChoice(l->id); break;
        default: break;
        }
        return true;
    }
    RowList list_; RowEditors editors_; std::vector<Key> keys_;
    bool naming_=false, syncing_=false;
};
HWND CreateOverlaysPage(HWND parent) { return PageWindow::Show(std::make_unique<OverlaysPage>(),app.instance,parent,560,360); }
