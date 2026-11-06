// Rows shared by the overlay pages: the name at the top, removing it at the bottom, the Window section, rounded corners under Layout, and header and footer,
// each with a switch and its four metric slots in the expanded row.
std::wstring SlotsText(const Slots& metrics) {
    std::wstring text;
    for(int slot=0;slot<SlotCount;++slot) text+=(slot?L"  \u00B7  ":L"")+std::wstring(metrics[slot]==NoMetric?L"\u2014":MetricTitles[metrics[slot]]);
    return text;
}
RowList::Row RoundedRow(const Overlay& o) {
    RowList::Row row; row.title=L"Rounded corners"; row.detail=L"Off: square corners."; row.toggle=o.place.rounded?1:0; return row;
}
void SetRounded(Overlay& o,bool on) { o.place.rounded=on; Fit(o); }
// Where an overlay shows, first on every overlay page, and its Window section right under it: name, size, opacity,
// fixed corner, updates and OBS's alpha. A page lists these rows under its own key with the WindowItem as id; the
// name row expands to its edit field. Size and opacity are sliders, a few values are chosen in the row, and the
// others' buttons show their value.
enum WindowItem { WindowName, WindowSize, WindowOpacity, WindowBackground, WindowCorner, WindowRate, WindowObs, WindowShows, WindowRemove };
constexpr int WindowNameEditId=180, WindowNameHeight=48;
const wchar_t* OverlayCorners[]={L"Top left",L"Top right",L"Bottom left",L"Bottom right"};
int CornerIndex(const Placement& place) { return (place.growLeft?1:0)+(place.growUp?2:0); }
// A value on a button that opens a menu of the others.
std::wstring MenuButton(const std::wstring& value) { return value+L"  ▾"; }
// Off, your screen, OBS, or both: a bit for each, as the choices stand.
RowList::Row ShowsRow(const Overlay& o) {
    RowList::Row row; row.title=L"Shows on"; row.choices={L"Off",L"Screen",L"OBS",L"Both"}; row.choice=(o.place.enabled?1:0)+(o.place.spout?2:0);
    const auto sender=L"“"+Wide(SpoutSenderName(o))+L"”";
    row.detail=row.choice==0?L"Neither on your screen nor in OBS":(row.choice==1?L"Your screen":(row.choice==2?L"OBS only, as Spout2 source "+sender:L"Your screen, and OBS as Spout2 source "+sender));
    return row;
}
// The name, first on the page, above Shows on; its arrow expands it to the edit field.
RowList::Row OverlayNameRow(const Overlay& o,bool open) {
    RowList::Row row; row.title=L"Name"; row.detail=Name(o)+(o.customName.empty()?L"":L"  \u00B7  "+std::wstring(Titles[o.kind]));
    row.iconButtons={open?Glyph::Collapse:Glyph::Expand}; row.expandHeight=open?WindowNameHeight:0;
    return row;
}
// Last on the page, under its own heading.
RowList::Row RemoveOverlayRow() {
    RowList::Row row; row.title=L"Remove overlay"; row.detail=L"With its settings. Add overlay in the list adds a new one."; row.button=L"Remove..."; row.buttonDanger=true; return row;
}
// Asks first. The main window removes it afterwards: its page, which asked, closes with it.
void RemoveOverlayChoice(Overlay& o) {
    const auto question=L"Remove "+Name(o)+L" and its settings?";
    if(MessageBoxW(app.main,question.c_str(),L"RaceMate",MB_OKCANCEL|MB_ICONQUESTION)==IDOK) PostMessageW(app.main,RemoveOverlayMessage,0,reinterpret_cast<LPARAM>(&o));
}
std::vector<std::pair<int,RowList::Row>> WindowRows(const Overlay& o) {
    std::vector<std::pair<int,RowList::Row>> rows;
    auto add=[&](int item,const wchar_t* title,std::wstring detail) { RowList::Row row; row.title=title; row.detail=std::move(detail); rows.emplace_back(item,std::move(row)); return &rows.back().second; };
    auto slider=[](RowList::Row* row,int value,int from,int to) { row->slider=value; row->sliderMin=from; row->sliderMax=to; row->sliderStep=5; row->sliderUnit=L" %"; };
    slider(add(WindowSize,L"Size",L"Text and layout alike"),o.place.scale,50,150);
    slider(add(WindowOpacity,L"Opacity",L""),o.place.opacity,40,100);
    add(WindowBackground,L"Background only",L"Text and markings stay solid")->toggle=o.place.backgroundOnly?1:0;
    // The Side Warning's bars stay centred on their monitor, so no corner holds them.
    if(o.kind!=SideWarningKind) {
        auto* corner=add(WindowCorner,L"Fixed corner",std::wstring(OverlayCorners[CornerIndex(o.place)])+L"  \u00B7  it grows away from it");
        corner->choices={L"↖",L"↗",L"↙",L"↘"}; corner->choice=CornerIndex(o.place);
    }
    auto* rate=add(WindowRate,L"Updates",L"Times a second it redraws");
    for(int n:OverlayRates) rate->choices.push_back(std::to_wstring(n));
    rate->choice=static_cast<int>(std::find(std::begin(OverlayRates),std::end(OverlayRates),o.place.rate)-std::begin(OverlayRates));
    // Straight alpha only for OBS's Default mode; while OBS gets nothing, nothing to choose.
    if(o.place.spout) {
        auto* alpha=add(WindowObs,L"OBS alpha",o.place.spoutPremultiplied?L"For OBS's Premultiplied alpha mode":L"For OBS's Default alpha mode");
        alpha->choices={L"Premultiplied",L"Straight"}; alpha->choice=o.place.spoutPremultiplied?0:1;
    }
    return rows;
}
// A Window row's switch, slider, choice or button; choose is the page's menu, picked the choice clicked or the
// slider's value. The name row's arrow is the page's own: it expands it.
template<class Choose> void WindowCommand(Overlay& o,int item,int code,bool on,int picked,Choose choose) {
    if(item==WindowBackground) { if(code==RowList::kToggled) { o.place.backgroundOnly=on; Fit(o); SettingChanged(); } return; }
    if(code==RowList::kChoice) {
        if(item==WindowShows) { o.place.enabled=picked&1; o.place.spout=(picked&2)!=0; if(picked) o.place.lastShows=picked; SettingChanged(); }
        if(item==WindowCorner) SetOverlayCorner(o,picked);
        if(item==WindowObs) { o.place.spoutPremultiplied=picked==0; SettingChanged(); }
        if(item==WindowRate && picked>=0 && picked<static_cast<int>(std::size(OverlayRates))) { o.place.rate=OverlayRates[picked]; o.due=0; Timer(); SettingChanged(); UpdateStatus(); }
        return;
    }
    if(code==RowList::kSlider) {
        if(item==WindowSize) { o.place.scale=picked; Fit(o); SettingChanged(); }
        if(item==WindowOpacity) { o.place.opacity=picked; if(o.place.backgroundOnly) { Fit(o); SettingChanged(); } else { ApplyOpacity(o); Save(); } }
        return;
    }
    if(code!=RowList::kButton) return;
    if(item==WindowRemove) RemoveOverlayChoice(o);
    (void)choose;
}
void WindowNameEditor(RowEditors& editors,const Overlay& o) {
    editors.Label(L"Name",0,10,84); SendMessageW(editors.Edit(WindowNameEditId,Name(o),88,10,410),EM_SETLIMITTEXT,127,0);
}
// Typed names are kept as typed; leaving the field trims them, and an empty name restores the default.
// True when the page should show the new name.
bool WindowNameCommand(Overlay& o,RowEditors& editors,int code) {
    if(code==EN_CHANGE) { o.customName=editors.Text(WindowNameEditId).substr(0,127); Save(); return false; }
    if(code!=EN_KILLFOCUS) return false;
    o.customName=CustomName(o,o.customName);
    const auto title=Name(o);
    if(o.window) SetWindowTextW(o.window,title.c_str());
    if(o.mirror) SetWindowTextW(o.mirror,title.c_str());
    if(ui.page==Page::Overlay && ui.overlay==&o) SetWindowTextW(ui.title,OverlayTitle(o).c_str());
    if(ui.pane && IsWindowVisible(ui.pane)) SendMessageW(ui.pane,WM_COMMAND,MAKEWPARAM(PageRefreshId,0),0);
    Save(); InvalidateModel(); return true;
}
RowList::Row BandRow(const Overlay& o,bool footer) {
    RowList::Row row; row.title=footer?L"Footer":L"Header";
    row.detail=SlotsText(footer?o.bands.footer.metrics:o.bands.headerMetrics); row.toggle=(footer?o.bands.footer.enabled:o.bands.header)?1:0;
    return row;
}
void SetBand(Overlay& o,bool footer,bool on) { (footer?o.bands.footer.enabled:o.bands.header)=on; }
// The four slots of an expanded header or footer row, as combo boxes firstId to firstId+3, two per
// line so they fit the narrowest window.
constexpr int BandEditorHeight=84;
void BandEditors(RowEditors& editors,const Overlay& o,bool footer,int firstId) {
    const Slots& metrics=footer?o.bands.footer.metrics:o.bands.headerMetrics;
    const std::vector<std::wstring> titles(std::begin(MetricTitles),std::end(MetricTitles));
    const wchar_t* labels[]={L"Left",L"Right 1",L"Right 2",L"Right 3"};
    for(int slot=0;slot<SlotCount;++slot) { const int x=slot%2*268, y=10+slot/2*36; editors.Label(labels[slot],x,y,56); editors.Combo(firstId+slot,titles,metrics[slot],x+60,y,188); }
}
void ApplyBandEditor(RowEditors& editors,Overlay& o,bool footer,int id,int firstId) {
    (footer?o.bands.footer.metrics:o.bands.headerMetrics)[id-firstId]=std::clamp(editors.Selection(id),0,MetricCount-1);
}
