// Rows shared by the overlay pages: the Window section, rounded corners under Layout, and header and footer,
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
// Where an overlay shows, first on every overlay page, and its Window section, last: name, size, opacity, fixed
// corner, updates and OBS's alpha. A page lists these rows under its own key with the WindowItem as id; the name
// row expands to its edit field. A few values are chosen in the row; the others' buttons show their value.
enum WindowItem { WindowName, WindowSize, WindowOpacity, WindowBackground, WindowCorner, WindowRate, WindowObs, WindowShows };
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
std::vector<std::pair<int,RowList::Row>> WindowRows(const Overlay& o,bool nameOpen) {
    std::vector<std::pair<int,RowList::Row>> rows;
    auto add=[&](int item,const wchar_t* title,std::wstring detail) { RowList::Row row; row.title=title; row.detail=std::move(detail); rows.emplace_back(item,std::move(row)); return &rows.back().second; };
    auto* name=add(WindowName,L"Name",Name(o)+(o.customName.empty()?L"":L"  \u00B7  "+std::wstring(Titles[o.kind])));
    name->iconButtons={nameOpen?Glyph::Collapse:Glyph::Expand}; name->expandHeight=nameOpen?WindowNameHeight:0;
    add(WindowSize,L"Size",L"Text and layout alike")->button=MenuButton(std::to_wstring(o.place.scale)+L" %");
    add(WindowOpacity,L"Opacity",L"")->button=MenuButton(std::to_wstring(o.place.opacity)+L" %");
    add(WindowBackground,L"Background only",L"Text and markings stay solid")->toggle=o.place.backgroundOnly?1:0;
    // The Side Warning's bars stay centred on their monitor, so no corner holds them.
    if(o.kind!=SideWarningKind) {
        auto* corner=add(WindowCorner,L"Fixed corner",std::wstring(OverlayCorners[CornerIndex(o.place)])+L"  \u00B7  it grows away from it");
        corner->choices={L"↖",L"↗",L"↙",L"↘"}; corner->choice=CornerIndex(o.place);
    }
    add(WindowRate,L"Updates",L"How often it redraws")->button=MenuButton(std::to_wstring(o.place.rate)+L" Hz");
    // Straight alpha only for OBS's Default mode; while OBS gets nothing, nothing to choose.
    if(o.place.spout) {
        auto* alpha=add(WindowObs,L"OBS alpha",o.place.spoutPremultiplied?L"For OBS's Premultiplied alpha mode":L"For OBS's Default alpha mode");
        alpha->choices={L"Premultiplied",L"Straight"}; alpha->choice=o.place.spoutPremultiplied?0:1;
    }
    return rows;
}
// A Window row's switch, choice or button; choose is the page's menu, picked the choice clicked. The name row's
// arrow is the page's own: it expands it.
template<class Choose> void WindowCommand(Overlay& o,int item,int code,bool on,int picked,Choose choose) {
    if(item==WindowBackground) { if(code==RowList::kToggled) { o.place.backgroundOnly=on; Fit(o); SettingChanged(); } return; }
    if(code==RowList::kChoice) {
        if(item==WindowShows) { o.place.enabled=picked&1; o.place.spout=(picked&2)!=0; SettingChanged(); }
        if(item==WindowCorner) SetOverlayCorner(o,picked);
        if(item==WindowObs) { o.place.spoutPremultiplied=picked==0; SettingChanged(); }
        return;
    }
    if(code!=RowList::kButton) return;
    auto percents=[](int from,int to) { std::vector<std::wstring> items; for(int n=from;n<=to;n+=10) items.push_back(std::to_wstring(n)+L" %"); return items; };
    if(item==WindowSize) { const int choice=choose(percents(50,150),(o.place.scale-50)/10); if(choice>=0) { o.place.scale=50+10*choice; Fit(o); SettingChanged(); } }
    if(item==WindowOpacity) {
        const int choice=choose(percents(40,100),(o.place.opacity-40)/10); if(choice<0) return;
        o.place.opacity=40+10*choice;
        if(o.place.backgroundOnly) { Fit(o); SettingChanged(); } else { ApplyOpacity(o); Save(); }
    }
    if(item==WindowRate) {
        std::vector<std::wstring> items; for(int n:OverlayRates) items.push_back(std::to_wstring(n)+L" Hz");
        const int choice=choose(items,static_cast<int>(std::find(std::begin(OverlayRates),std::end(OverlayRates),o.place.rate)-std::begin(OverlayRates)));
        if(choice>=0) { o.place.rate=OverlayRates[choice]; o.due=0; Timer(); SettingChanged(); UpdateStatus(); }
    }
}
void WindowNameEditor(RowEditors& editors,const Overlay& o) {
    editors.Label(L"Name",0,10,84); SendMessageW(editors.Edit(WindowNameEditId,Name(o),88,10,410),EM_SETLIMITTEXT,127,0);
}
// Typed names are kept as typed; leaving the field trims them, and an empty name restores the default.
// True when the page should show the new name.
bool WindowNameCommand(Overlay& o,RowEditors& editors,int code) {
    if(code==EN_CHANGE) { o.customName=editors.Text(WindowNameEditId).substr(0,127); Save(); return false; }
    if(code!=EN_KILLFOCUS) return false;
    auto& text=o.customName;
    const auto first=text.find_first_not_of(L" \t\r\n");
    text=first==std::wstring::npos?std::wstring():text.substr(first,text.find_last_not_of(L" \t\r\n")-first+1);
    const auto title=Name(o);
    if(o.window) SetWindowTextW(o.window,title.c_str());
    if(o.mirror) SetWindowTextW(o.mirror,title.c_str());
    if(ui.page==Page::Overlay && ui.overlay==&o) SetWindowTextW(ui.title,title.c_str());
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
    for(int slot=0;slot<SlotCount;++slot) { const int x=slot%2*280, y=10+slot/2*36; editors.Label(labels[slot],x,y,56); editors.Combo(firstId+slot,titles,metrics[slot],x+60,y,200); }
}
void ApplyBandEditor(RowEditors& editors,Overlay& o,bool footer,int id,int firstId) {
    (footer?o.bands.footer.metrics:o.bands.headerMetrics)[id-firstId]=std::clamp(editors.Selection(id),0,MetricCount-1);
}
