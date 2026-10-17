class TrackMapPage : public PageWindow {
public:
    explicit TrackMapPage(Overlay& overlay) : PageWindow(ui.heading,ui.text), o_(overlay) {}
private:
    enum Key { Info, Numbers, OwnClass, Pit, Details, Rounded, Header, Footer, Retry, Cache, Background, PitLane, StartFinish, CornerNumbers, CornerNames, LabelSize, LabelContrast };
    static constexpr int ListId=100, SlotEditId=230, LabelSizeId=240;
    static constexpr wchar_t ExpandGlyph=L'\uE70D', CollapseGlyph=L'\uE70E';
    void OnCreate() override { list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont()); editors_.Attach(list_,Instance(),TextFont()); Refresh(); }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,std::min(width,760),height); }
    void Refresh() {
        std::vector<RowList::Row> rows; keys_.clear();
        auto push=[&](RowList::Row row,Key key) { rows.push_back(std::move(row)); keys_.push_back(key); };
        auto heading=[&](const wchar_t* title) { RowList::Row row; row.header=true; row.title=title; push(std::move(row),Info); };
        auto toggle=[&](const wchar_t* title,const wchar_t* detail,bool on,Key key) { RowList::Row row; row.title=title; row.detail=detail; row.toggle=on?1:0; push(std::move(row),key); };
        heading(L"Display");
        toggle(L"Background and frame",L"Off: transparent map and cars only. Hides the frame, captions, header and footer; their settings are kept.",o_.map.background,Background);
        toggle(L"Pit lane",L"Show the official pit-lane drawing, independently of cars in the pits.",o_.map.pitLane,PitLane);
        toggle(L"Start/finish marking",L"Show the line and direction arrow. Position mapping keeps using these data when hidden.",o_.map.startFinish,StartFinish);
        toggle(L"Car numbers",L"Switch off for smaller dots. Your reference car is yellow with a white outline.",o_.map.numbers,Numbers);
        toggle(L"Own class only",L"Uses the class of your car, or the car you are watching.",o_.map.ownClass,OwnClass);
        toggle(L"Cars in the pits",L"Hollow markers at their lap percentage on the main circuit; not their exact pit-lane position.",o_.map.pit,Pit);
        toggle(L"Projection details",L"Show track ID, cache source, refresh rate and the distance-projection notice. With background only.",o_.map.details,Details);
        rows.back().muted=MapOnly(o_); rows.back().toggleEnabled=!MapOnly(o_);
        auto rounded=RoundedRow(o_); rounded.muted=MapOnly(o_); rounded.toggleEnabled=!MapOnly(o_); push(std::move(rounded),Rounded);
        heading(L"Corner labels");
        toggle(L"Corner numbers",L"Numbers at their official SVG positions. Also available with a transparent background.",o_.map.cornerNumbers,CornerNumbers);
        toggle(L"Corner names",L"Official names where iRacing supplies them. Tracks with numbers only do not get invented names.",o_.map.cornerNames,CornerNames);
        toggle(L"Label contrast outline",L"Dark outline around the text for readability over the circuit or other windows.",o_.map.labelContrast,LabelContrast);
        RowList::Row size; size.title=L"Label size"; size.detail=std::to_wstring(o_.map.labelScale)+L"% of the official SVG text size; follows the overlay scale.";
        size.iconButtons={expanded_==LabelSize?CollapseGlyph:ExpandGlyph}; size.expandHeight=expanded_==LabelSize?52:0; push(std::move(size),LabelSize);
        heading(L"Header and footer");
        for(Key key:{Header,Footer}) {
            RowList::Row row=BandRow(o_,key==Footer); row.iconButtons={expanded_==key?CollapseGlyph:ExpandGlyph}; row.expandHeight=expanded_==key?BandEditorHeight:0;
            row.muted=MapOnly(o_); row.toggleEnabled=!MapOnly(o_);
            push(std::move(row),key);
        }
        heading(L"SVG source");
        RowList::Row status; status.title=L"Automatic layout match"; status.detail=L"Exact iRacing TrackID plus internal TrackName. Unknown layouts are left empty. Preview uses Lime Rock GP with sample cars."; push(std::move(status),Info);
        RowList::Row retry; retry.title=L"Retry loading this map"; retry.detail=L"Uses valid cached geometry. Retries missing corner labels immediately; otherwise failed label requests wait six hours."; retry.button=L"Retry"; push(std::move(retry),Retry);
        RowList::Row cache; cache.title=L"Local SVG cache"; cache.detail=TrackMapCache().wstring(); cache.button=L"Open folder"; push(std::move(cache),Cache);
        RowList::Row note; note.title=L"Experimental distance projection"; note.detail=L"Start/finish and direction come from the official SVG. Lap percentages follow its prepared path length. No learned fallback or per-track corrections. SVGs download once per layout while this overlay is enabled."; push(std::move(note),Info);
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
            if(key==Cache) { std::error_code error; std::filesystem::create_directories(TrackMapCache(),error); if(!error) ShellExecuteW(Handle(),L"open",TrackMapCache().c_str(),nullptr,nullptr,SW_SHOWNORMAL); }
        }
        return true;
    }
    Overlay& o_; RowList list_; RowEditors editors_; std::vector<Key> keys_; Key expanded_=Info; bool syncing_=false;
};
HWND CreateTrackMapPage(HWND parent,Overlay& overlay) { return PageWindow::Show(std::make_unique<TrackMapPage>(overlay),app.instance,parent,560,360); }
