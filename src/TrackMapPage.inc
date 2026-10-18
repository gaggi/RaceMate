class TrackMapPage : public PageWindow {
public:
    explicit TrackMapPage(Overlay& overlay) : PageWindow(ui.heading,ui.text), o_(overlay) {}
private:
    enum Key { Info, LayoutKey, WhiteTrackKey, Numbers, OwnClass, Pit, PitExitKey, Details, Rounded, Header, Footer, Retry, Cache, Background, PitLane, StartFinish, CornerNumbers, CornerNames, LabelSize, LabelContrast };
    static constexpr int ListId=100, SlotEditId=230, LabelSizeId=240;
    void OnCreate() override { list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont()); editors_.Attach(list_,Instance(),TextFont()); Refresh(); }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,std::min(width,760),height); }
    void Refresh() {
        std::vector<RowList::Row> rows; keys_.clear();
        auto push=[&](RowList::Row row,Key key) { rows.push_back(std::move(row)); keys_.push_back(key); };
        auto heading=[&](const wchar_t* title) { RowList::Row row; row.header=true; row.title=title; push(std::move(row),Info); };
        auto toggle=[&](const wchar_t* title,const wchar_t* detail,bool on,Key key) { RowList::Row row; row.title=title; row.detail=detail; row.toggle=on?1:0; push(std::move(row),key); };
        heading(L"Display");
        {
            // Circle and line need no SVG: for any track, and the line for wide monitors.
            static constexpr const wchar_t* names[]={L"Map",L"Circle",L"Line"};
            static constexpr const wchar_t* details[]={L"The official iRacing track drawing",L"Cars on a ring by lap position, 0 % at the top, clockwise; no download",L"Cars on a bar by lap position, for 21:9 and 32:9 monitors; no download"};
            RowList::Row layout; layout.title=names[o_.map.layout]; layout.detail=details[o_.map.layout]; layout.button=L"Choose..."; push(std::move(layout),LayoutKey);
        }
        toggle(L"White track",L"White with a black edge, readable over any background. Off: grey, as the panel.",o_.map.whiteTrack,WhiteTrackKey);
        toggle(L"Background and frame",L"Off: transparent map and cars only. Hides the frame, captions, header and footer; their settings are kept.",o_.map.background,Background);
        toggle(L"Pit lane",L"Show the official pit-lane drawing, independently of cars in the pits.",o_.map.pitLane,PitLane);
        toggle(L"Start/finish marking",L"Show the line and direction arrow. Position mapping keeps using these data when hidden.",o_.map.startFinish,StartFinish);
        {
            static constexpr const wchar_t* texts[]={L"Car numbers",L"Position in class",L"Overall position"};
            RowList::Row row; row.title=L"Text in markers"; row.toggle=o_.map.numbers?1:0; row.button=L"Choose...";
            row.detail=std::wstring(texts[o_.map.markerText])+L"  \u00B7  off for smaller dots. Cars use their class color; the car you drive or watch is yellow with a white ring.";
            push(std::move(row),Numbers);
        }
        toggle(L"Own class only",L"Uses the class of your car, or the car you are watching.",o_.map.ownClass,OwnClass);
        toggle(L"Cars in the pits",L"Hollow markers; on the drawn pit lane beside their lap position when Pit lane is on, else on the circuit.",o_.map.pit,Pit);
        toggle(L"Pit exit",L"A blue PIT ring where you would rejoin if you boxed now. Times under Settings, Pit stop estimate.",o_.map.pitExit,PitExitKey);
        toggle(L"Projection details",L"A line under the map with track ID, cache source, refresh rate and the projection notice. With background only.",o_.map.details,Details);
        rows.back().muted=MapOnly(o_); rows.back().toggleEnabled=!MapOnly(o_);
        auto rounded=RoundedRow(o_); rounded.muted=MapOnly(o_); rounded.toggleEnabled=!MapOnly(o_); push(std::move(rounded),Rounded);
        heading(L"Corner labels");
        toggle(L"Corner numbers",L"Numbers at their official SVG positions. Also available with a transparent background.",o_.map.cornerNumbers,CornerNumbers);
        toggle(L"Corner names",L"Official names where iRacing supplies them. Tracks with numbers only do not get invented names.",o_.map.cornerNames,CornerNames);
        toggle(L"Label contrast outline",L"Dark outline around the text for readability over the circuit or other windows.",o_.map.labelContrast,LabelContrast);
        RowList::Row size; size.title=L"Label size"; size.detail=std::to_wstring(o_.map.labelScale)+L"% of the official SVG text size; follows the overlay scale.";
        size.iconButtons={expanded_==LabelSize?Glyph::Collapse:Glyph::Expand}; size.expandHeight=expanded_==LabelSize?52:0; push(std::move(size),LabelSize);
        heading(L"Header and footer");
        for(Key key:{Header,Footer}) {
            RowList::Row row=BandRow(o_,key==Footer); row.iconButtons={expanded_==key?Glyph::Collapse:Glyph::Expand}; row.expandHeight=expanded_==key?BandEditorHeight:0;
            row.muted=MapOnly(o_); row.toggleEnabled=!MapOnly(o_);
            push(std::move(row),key);
        }
        heading(L"Map data");
        RowList::Row status; status.title=L"Track detection"; status.detail=L"Exact iRacing TrackID plus internal TrackName. Unknown layouts are left empty. Preview uses Lime Rock GP with sample cars."; push(std::move(status),Info);
        RowList::Row retry; retry.title=L"Retry loading this map"; retry.detail=L"Uses valid cached geometry. Retries missing corner labels immediately; otherwise failed label requests wait six hours."; retry.button=L"Retry"; push(std::move(retry),Retry);
        RowList::Row cache; cache.title=L"Map cache"; cache.detail=TrackMapCache().wstring(); cache.button=L"Open folder"; push(std::move(cache),Cache);
        RowList::Row note; note.title=L"Car positions"; note.detail=L"Start/finish and direction come from the official SVG. Lap percentages follow its prepared path length. No learned fallback or per-track corrections. SVGs download once per layout while this overlay is enabled."; push(std::move(note),Info);
        list_.SetRows(std::move(rows));
    }
    void BuildEditors() {
        editors_.Clear();
        if(expanded_!=Header && expanded_!=Footer && expanded_!=LabelSize) return;
        for(size_t i=0;i<keys_.size();++i) if(keys_[i]==expanded_) {
            syncing_=true; editors_.Begin(static_cast<int>(i));
            if(expanded_==LabelSize) {
                editors_.Label(L"Text size",24,12,100);
                editors_.Combo(LabelSizeId,{L"75%",L"100%",L"125%",L"150%",L"175%",L"200%"},std::clamp((o_.map.labelScale-75)/25,0,5),132,12,120);
            } else BandEditors(editors_,o_,expanded_==Footer,SlotEditId);
            syncing_=false; editors_.Position(); return;
        }
    }
    bool OnCommand(int id,int code,HWND) override {
        if(id==PageRefreshId) { Refresh(); BuildEditors(); return true; }
        if(id==LabelSizeId) {
            if(!syncing_ && code==CBN_SELCHANGE) { o_.map.labelScale=75+25*std::clamp(editors_.Selection(id),0,5); SettingChanged(); Refresh(); }
            return true;
        }
        if(id>=SlotEditId && id<SlotEditId+SlotCount) {
            if(!syncing_ && code==CBN_SELCHANGE) { ApplyBandEditor(editors_,o_,expanded_==Footer,id,SlotEditId); SettingChanged(); Refresh(); }
            return true;
        }
        if(id!=ListId) return false;
        if(code==RowList::kLayoutChanged) { editors_.Position(); return true; }
        const int index=list_.NotifiedRow(); if(index<0 || static_cast<size_t>(index)>=keys_.size()) return true;
        const Key key=keys_[index]; const bool on=list_.Rows()[index].toggle==1;
        if(code==RowList::kToggled) {
            if(key==Numbers) o_.map.numbers=on;
            if(key==OwnClass) o_.map.ownClass=on;
            if(key==Pit) o_.map.pit=on;
            if(key==PitExitKey) o_.map.pitExit=on;
            if(key==WhiteTrackKey) o_.map.whiteTrack=on;
            if(key==Details) o_.map.details=on;
            if(key==Background) { o_.map.background=on; expanded_=Info; editors_.Clear(); Fit(o_); }
            if(key==PitLane) o_.map.pitLane=on;
            if(key==StartFinish) o_.map.startFinish=on;
            if(key==CornerNumbers) o_.map.cornerNumbers=on;
            if(key==CornerNames) o_.map.cornerNames=on;
            if(key==LabelContrast) o_.map.labelContrast=on;
            if(key==Rounded) SetRounded(o_,on);
            if(key==Header || key==Footer) SetBand(o_,key==Footer,on);
            SettingChanged(); Refresh(); return true;
        }
        if(code==RowList::kButton || code==RowList::kActivated || code==RowList::kIconButton) {
            if(key==Header || key==Footer || key==LabelSize) { expanded_=expanded_==key?Info:key; Refresh(); BuildEditors(); if(key==LabelSize && expanded_==LabelSize) list_.ScrollIntoView(index); }
            if(key==Retry) { RetryTrackMap(true); UpdateOverlays(); }
            if(key==Numbers) {
                const int choice=ChooseFromMenu({L"Car numbers",L"Position in class",L"Overall position"},o_.map.markerText);
                if(choice>=0 && choice!=o_.map.markerText) { o_.map.markerText=choice; SettingChanged(); Refresh(); }
            }
            if(key==LayoutKey) {
                const int choice=ChooseFromMenu({L"Map",L"Circle",L"Line"},o_.map.layout);
                if(choice>=0 && choice!=o_.map.layout) { o_.map.layout=choice; Fit(o_); SettingChanged(); Refresh(); }
            }
            if(key==Cache) { std::error_code error; std::filesystem::create_directories(TrackMapCache(),error); if(!error) ShellExecuteW(Handle(),L"open",TrackMapCache().c_str(),nullptr,nullptr,SW_SHOWNORMAL); }
        }
        return true;
    }
    Overlay& o_; RowList list_; RowEditors editors_; std::vector<Key> keys_; Key expanded_=Info; bool syncing_=false;
};
HWND CreateTrackMapPage(HWND parent,Overlay& overlay) { return PageWindow::Show(std::make_unique<TrackMapPage>(overlay),app.instance,parent,560,360); }
