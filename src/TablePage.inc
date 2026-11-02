// Relative and Standings pages: what the rows show, the columns (switch, order and
// options in the expanded row) and the header and footer metrics.
class TablePage : public PageWindow {
public:
    explicit TablePage(Overlay& o) : PageWindow(ui.heading,ui.text), o_(o) {}
private:
    enum class Key { None, Window, Rounded, Rows, GapDecimals, GapSigns, HidePit, Leaders, Ahead, Behind, OtherClassRows, ClassHeader, ClassView, OwnClass, LiveGaps, UntimedOrder, ClassColors, LapColors, Progress, PitExit, PitAhead, PitBehind, Column, Header, Footer, ColumnHeadings, CopyColumns };
    static constexpr int ListId=100, ClassHeaderEditId=240, NameFormatEditId=201, FlagEditId=202, StatusEditId=218, ShortViewEditId=205, SlotEditId=206, BrandLogoEditId=213, BrandNameEditId=214, PositionEditId=230, SectorPartsEditId=219, TopSpeedEditId=220;
    struct ColumnText { const wchar_t* name; const wchar_t* detail; };
    static constexpr ColumnText ColumnTexts[]={
        {L"Position",L"Overall or class position"},{L"Car number",L"On the class color"},{L"Driver",L""},
        {L"Gap",L""},{L"Interval",L"Gap to the car directly ahead, including hidden rows"},{L"Best lap",L"Fastest lap of this session"},{L"Last lap",L"Most recent lap"},
        {L"License",L"License class and safety rating"},{L"iRating",L""},{L"Tires",L"D dry, W wet, or S/M/H where a car has several compounds"},
        {L"Last pit lap",L"Lap of the last observed pit entry"},{L"Pit lane time",L"From pit entry to pit exit"},
        {L"Pit stop time",L"Time standing in the pit stall, estimated"},{L"Completed laps",L""},
        {L"Positions gained",L"Compared with the starting grid"},{L"Status badge",L""},
        {L"Car brand",L"Manufacturer, with its initial until logos arrive"},{L"iRating change",L"Estimated, as if the race ended in the current order"},
        {L"Laps since stop",L"L12; L- until a stop or the start was seen"},{L"Pit stops",L"Observed in this race; ? after joining late"},
        {L"Fuel to finish",L"Own-class estimate: green END at normal range, yellow SAVE with fuel saving. Full tank assumed; extra laps in Settings."},
        {L"Safety and iRating",L"iRating on the license color, in one badge"},
        {L"Sector times",L"S1, S2, S3 … in seconds, one column each. A new S1 clears the previous lap's later sectors. Green personal best, purple class best, yellow slower; estimated."},
        {L"Team name",L"In team events; solo events show the driver's name"},
        {L"Top speed",L"km/h on the last lap, from each car's way around the track; estimated"}};
    static constexpr const wchar_t* NameFormats[]={L"First name Last name",L"F. Last name",L"F Last name"};
    static constexpr const wchar_t* ClassHeaderNames[]={L"Cars",L"SOF",L"Average iRating",L"Class best lap"};
    static constexpr const wchar_t* FlagNames[]={L"Black flag",L"Slowdown",L"Required repairs"};
    static_assert(std::size(ColumnTexts)==ColumnCount);

    void OnCreate() override {
        list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont());
        editors_.Attach(list_,Instance(),TextFont());
        Refresh();
    }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,width,height); }

    bool HasOptions(int id) const { return (id==PositionColumn && o_.kind==RelativeKind) || id==DriverColumn || id==LicenseColumn || id==IRatingColumn || id==CombinedRatingColumn || id==BrandColumn || id==SectorsColumn || id==TopSpeedColumn; }
    // Short views: License, iRating, Safety and iRating.
    static int ShortViewIndex(int id) { return id==CombinedRatingColumn?2:id-LicenseColumn; }
    std::wstring ColumnDetail(int id) const {
        switch(id) {
        case PositionColumn: if(o_.kind==RelativeKind) return o_.table.classPositions?L"In each car's class":L"Overall, across all classes"; return ColumnTexts[id].detail;
        case DriverColumn: {
            // With the status badge after the name, which is part of the driver's column here.
            std::wstring text=std::wstring(NameFormats[o_.table.nameFormat])+L"  \u00B7  ";
            if(!o_.table.showStatus) return text+L"no status badge";
            text+=L"badge: PIT, TOW, OUT";
            for(int i=0;i<3;++i) if(o_.table.driverFlags[i]) text+=std::wstring(L", ")+FlagNames[i];
            return text;
        }
        case SectorsColumn: return o_.table.sectorParts==3?L"S1.1, S1.2 … one column per mini-sector. Green personal best, purple class best, yellow slower; estimated.":ColumnTexts[id].detail;
        case TopSpeedColumn: return o_.table.topSpeedSession?L"km/h over the session, from each car's way around the track; estimated":ColumnTexts[id].detail;
        case GapColumn: return o_.kind==RelativeKind?L"Time to each car, estimated":L"Gap to the class leader";
        case BrandColumn: return o_.table.brandLogo && o_.table.brandName?L"Logo and name":(o_.table.brandLogo?L"Logo":(o_.table.brandName?L"Name":L"Nothing chosen"));
        case LicenseColumn: case IRatingColumn: case CombinedRatingColumn: {
            const bool shortView=o_.table.driverShortView[ShortViewIndex(id)];
            const std::wstring base=ColumnTexts[id].detail[0]?ColumnTexts[id].detail:L"Driver rating";
            return base+(shortView?(id==LicenseColumn?L"  \u00B7  short: 3,45":L"  \u00B7  short: 2,4K"):L"");
        }
        default: return ColumnTexts[id].detail;
        }
    }
    void Refresh() {
        std::vector<RowList::Row> rows; keys_.clear(); ids_.clear();
        auto push=[&](RowList::Row row,Key key,int id=-1) { rows.push_back(std::move(row)); keys_.push_back(key); ids_.push_back(id); };
        auto header=[&](const wchar_t* title) { RowList::Row row; row.header=true; row.title=title; push(std::move(row),Key::None); };
        // Icon buttons are laid out right to left: the expand arrow stays rightmost.
        auto expandable=[&](RowList::Row& row,Key key,int id,int height) {
            const bool open=expandedKey_==key && expandedId_==id;
            row.iconButtons.insert(row.iconButtons.begin(),open?Glyph::Collapse:Glyph::Expand); row.expandHeight=open?height:0;
        };
        // A count of cars; a saved count beyond the usual range widens the slider to it.
        auto counter=[&](const wchar_t* title,const std::wstring& detail,int value,int minimum,int maximum,Key key,bool muted=false) {
            RowList::Row row; row.title=title; row.detail=detail; row.muted=muted;
            row.slider=value; row.sliderMin=minimum; row.sliderMax=std::max(maximum,value);
            push(std::move(row),key);
        };
        // Where it shows first, its window right under it, then what it shows.
        push(OverlayNameRow(o_,expandedKey_==Key::Window),Key::Window,WindowName);
        push(ShowsRow(o_),Key::Window,WindowShows);
        header(L"Window");
        push(RoundedRow(o_),Key::Rounded);
        for(auto& [item,row]:WindowRows(o_)) push(std::move(row),Key::Window,item);
        if(o_.kind==RelativeKind) {
            header(L"Rows");
            const int around=(o_.table.rows-1)/2;
            counter(L"Drivers ahead and behind",std::to_wstring(o_.table.rows)+L" rows, your car in the middle",around,0,15,Key::Rows);
            push(DecimalsRow(L"Gap decimals",L""),Key::GapDecimals);
            RowList::Row signs; signs.title=L"Show + and \u2212 before gaps"; signs.detail=L"Without them, cars above your row are ahead and cars below are behind."; signs.toggle=o_.table.gapSigns?1:0;
            push(std::move(signs),Key::GapSigns);
            RowList::Row pit; pit.title=L"Hide drivers in the pit lane"; pit.detail=L"Your own car always stays."; pit.toggle=o_.table.hidePit?1:0;
            push(std::move(pit),Key::HidePit);
        }
        if(o_.kind==StandingsKind) {
            const auto& standings=o_.table.standings;
            header(L"Rows");
            const std::wstring field=standings.separateClasses?L"your class":L"the field";
            counter(L"Leaders",L"From the top of "+field,standings.leaders,0,10,Key::Leaders);
            counter(L"Drivers ahead",L"Directly ahead of your car",standings.ahead,0,20,Key::Ahead);
            counter(L"Drivers behind",L"Directly behind your car",standings.behind,0,20,Key::Behind);
            // The other classes and the class header exist only with separate classes.
            const bool classes=standings.separateClasses && !standings.ownClass;
            counter(L"Other classes",standings.otherClasses?L"At least their leaders, from the top":std::wstring(L"Only their class header"),standings.otherClasses,0,10,Key::OtherClassRows,!classes);
            RowList::Row view; view.title=L"Classes"; view.choices={L"Separate",L"Combined"}; view.choice=standings.separateClasses?0:1;
            view.detail=standings.separateClasses?L"Separate classes, each with its leader and entrants":L"Combined overall standings";
            push(std::move(view),Key::ClassView);
            RowList::Row head; head.title=L"Class header"; head.muted=!standings.separateClasses;
            std::wstring shown;
            for(int i=0;i<4;++i) if(o_.table.classHeader[i]) shown+=(shown.empty()?L"":L", ")+std::wstring(ClassHeaderNames[i]);
            head.detail=L"Class name on its color"+(shown.empty()?std::wstring():L"  ·  "+shown);
            expandable(head,Key::ClassHeader,-1,48); push(std::move(head),Key::ClassHeader);
            RowList::Row own; own.title=L"Only my class"; own.detail=L"Your class, or the class of the car you watch."; own.toggle=standings.ownClass?1:0;
            push(std::move(own),Key::OwnClass);
            RowList::Row live; live.title=L"Live positions and gaps"; live.detail=L"Estimated from track position between start/finish crossings."; live.toggle=standings.liveGaps?1:0;
            push(std::move(live),Key::LiveGaps);
            // As in the Relative; a layout for qualifying can show thousandths and one for races tenths.
            push(DecimalsRow(L"Gap decimals",L"Gap and interval"),Key::GapDecimals);
            RowList::Row signs; signs.title=L"Show + before gaps and intervals"; signs.detail=L"Both always count behind the leader or the car ahead."; signs.toggle=o_.table.gapSigns?1:0;
            push(std::move(signs),Key::GapSigns);
            RowList::Row untimed; untimed.title=L"Drivers without a lap time"; untimed.choices={L"By number",L"By iRating"}; untimed.choice=standings.fallbackByRating?1:0;
            untimed.detail=L"Their order until they set one";
            push(std::move(untimed),Key::UntimedOrder);
        }
        header(L"Markings");
        RowList::Row classColors; classColors.title=L"Class colors"; classColors.detail=L"Stripe on the left and the car number on the class color; off: a neutral number box.";
        classColors.toggle=o_.table.classColors?1:0; push(std::move(classColors),Key::ClassColors);
        if(o_.kind==RelativeKind) {
            RowList::Row lapColors; lapColors.title=L"Lap colors"; lapColors.detail=L"In races, red names are a lap or more ahead of you, blue names behind.";
            lapColors.toggle=o_.table.lapColors?1:0; push(std::move(lapColors),Key::LapColors);
        }
        if(o_.kind==StandingsKind) {
            RowList::Row progress; progress.title=L"Session progress"; progress.detail=L"A thin bar under the header, by laps or by time.";
            progress.toggle=o_.table.progress?1:0; push(std::move(progress),Key::Progress);
            RowList::Row pitExit; pitExit.title=L"Pit exit row";
            pitExit.detail=L"In races, a faded row where you would rejoin if you boxed now. Times under Settings, Pit stop estimate.";
            pitExit.toggle=o_.table.pitExit?1:0; push(std::move(pitExit),Key::PitExit);
            counter(L"Drivers ahead of the pit exit row",L"Shown even where the rows above leave them out",o_.table.pitExitAhead,0,10,Key::PitAhead,!o_.table.pitExit);
            counter(L"Drivers behind the pit exit row",L"Shown even where the rows above leave them out",o_.table.pitExitBehind,0,10,Key::PitBehind,!o_.table.pitExit);
        }
        header(L"Columns  \u00B7  left to right");
        {
            RowList::Row copy; copy.title=L"Copy to another table"; copy.button=L"Copy to...";
            copy.detail=L"Gives a Relative or Standings these columns, in this order, with their options.";
            push(std::move(copy),Key::CopyColumns);
        }
        // The status badge has no row of its own: it is part of Driver, after the name.
        std::vector<int> order=o_.table.columnOrder; std::erase(order,static_cast<int>(StatusColumn));
        // Same button, same column: {expand} or an empty slot {0} (see "Controls line up in columns").
        const bool anyOptions=std::any_of(order.begin(),order.end(),[&](int id){ return HasOptions(id); });
        for(size_t i=0;i<order.size();++i) {
            const int id=order[i];
            RowList::Row row; row.title=ColumnTexts[id].name; row.detail=ColumnDetail(id);
            const bool shown=ColumnVisible(o_,id);
            row.toggle=shown?1:0; // Every column can be switched off, the Position and the Gap too.
            row.muted=!shown;
            row.dragGroup=1; // Reordered by dragging its grip.
            if(HasOptions(id)) expandable(row,Key::Column,id,id==DriverColumn?112:48);
            else if(anyOptions) row.iconButtons.insert(row.iconButtons.begin(),0);
            push(std::move(row),Key::Column,id);
        }
        header(L"Header and footer");
        RowList::Row head=BandRow(o_,false); expandable(head,Key::Header,-1,BandEditorHeight); push(std::move(head),Key::Header);
        RowList::Row foot=BandRow(o_,true); expandable(foot,Key::Footer,-1,BandEditorHeight); push(std::move(foot),Key::Footer);
        RowList::Row headings; headings.title=L"Column headings"; headings.detail=L"Without them, columns shrink to their values."; headings.toggle=o_.table.columnHeaders?1:0;
        push(std::move(headings),Key::ColumnHeadings);
        header(L"Overlay"); push(RemoveOverlayRow(),Key::Window,WindowRemove);
        list_.SetRows(std::move(rows));
    }
    int ExpandedRow() const {
        if(expandedKey_==Key::None) return -1; // Section headers have no key either.
        for(size_t i=0;i<keys_.size();++i) if(keys_[i]==expandedKey_ && ids_[i]==expandedId_) return static_cast<int>(i);
        return -1;
    }
    void BuildEditors() {
        editors_.Clear();
        const int row=ExpandedRow(); if(row<0) return;
        syncing_=true; editors_.Begin(row);
        if(expandedKey_==Key::Window) WindowNameEditor(editors_,o_);
        else if(expandedKey_==Key::ClassHeader) {
            for(int i=0;i<4;++i) editors_.Check(ClassHeaderEditId+i,ClassHeaderNames[i],o_.table.classHeader[i],i*150,10,140);
        } else if(expandedKey_==Key::Header || expandedKey_==Key::Footer) {
            BandEditors(editors_,o_,expandedKey_==Key::Footer,SlotEditId);
        } else if(expandedId_==PositionColumn) {
            editors_.Label(L"Position",0,10,90);
            editors_.Combo(PositionEditId,{L"In class",L"Overall"},o_.table.classPositions?0:1,94,10,200);
        } else if(expandedId_==DriverColumn) {
            editors_.Label(L"Name format",0,10,90);
            editors_.Combo(NameFormatEditId,std::vector<std::wstring>(std::begin(NameFormats),std::end(NameFormats)),o_.table.nameFormat,94,10,200);
            editors_.Check(StatusEditId,L"Status badge after the name: PIT, TOW, OUT",o_.table.showStatus,0,46,360);
            for(int i=0;i<3;++i) editors_.Check(FlagEditId+i,FlagNames[i],o_.table.driverFlags[i],20+i*170,74,150);
        } else if(expandedId_==SectorsColumn) {
            editors_.Label(L"Sector detail",0,10,90);
            editors_.Combo(SectorPartsEditId,{L"SDK sectors",L"3 mini-sectors per sector"},o_.table.sectorParts==3?1:0,94,10,200);
        } else if(expandedId_==TopSpeedColumn) {
            editors_.Label(L"Top speed of",0,10,90);
            editors_.Combo(TopSpeedEditId,{L"The last lap",L"The session"},o_.table.topSpeedSession?1:0,94,10,200);
        } else if(expandedId_==BrandColumn) {
            editors_.Check(BrandLogoEditId,L"Show logo",o_.table.brandLogo,0,10,160);
            editors_.Check(BrandNameEditId,L"Show name",o_.table.brandName,170,10,160);
        } else if(expandedId_==LicenseColumn || expandedId_==IRatingColumn || expandedId_==CombinedRatingColumn) {
            const int index=ShortViewIndex(expandedId_);
            editors_.Check(ShortViewEditId,expandedId_==LicenseColumn?L"Short view: safety rating only (3,45)":L"Short view: thousands (2,4K)",o_.table.driverShortView[index],0,10,360);
        }
        syncing_=false; editors_.Position();
    }
    void Expand(Key key,int id) {
        const bool same=expandedKey_==key && expandedId_==id;
        expandedKey_=same?Key::None:key; expandedId_=same?-1:id;
        Refresh(); BuildEditors();
        if(const int row=ExpandedRow(); row>=0) list_.ScrollIntoView(row);
    }
    void Changed() { SettingChanged(); Refresh(); }
    // Every other Relative and Standings, the other kind first; the chosen one gets these columns after asking.
    void CopyTo() {
        std::vector<Overlay*> targets; std::vector<std::wstring> names;
        for(int pass=0;pass<2;++pass) for(auto& o:app.overlays) if(IsTable(o.kind) && &o!=&o_ && (o.kind!=o_.kind)==(pass==0)) { targets.push_back(&o); names.push_back(Name(o)); }
        if(targets.empty()) return;
        const int choice=ChooseFromMenu(names,-1); if(choice<0) return;
        const auto question=L"Give "+names[choice]+L" the columns of "+Name(o_)+L"? Its own column settings are replaced.";
        if(MessageBoxW(app.main,question.c_str(),L"RaceMate",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) return;
        CopyColumns(o_,*targets[choice]);
    }
    // Columns move among those with a row; the status badge then goes back behind Driver.
    template<class Change> void Reorder(Change change) {
        auto& order=o_.table.columnOrder; std::erase(order,static_cast<int>(StatusColumn));
        const bool moved=change(order); StatusAfterDriver(order);
        if(moved) { SettingChanged(); Refresh(); BuildEditors(); }
    }
    // Places after the point, as the gap reads with them.
    RowList::Row DecimalsRow(const wchar_t* title,const wchar_t* detail) const {
        RowList::Row row; row.title=title; row.detail=detail; row.choices={L"1 s",L"0.1",L"0.01",L"0.001"}; row.choice=o_.table.gapDecimals; return row;
    }
    bool OnCommand(int id,int code,HWND) override {
        if(id==PageRefreshId) { Refresh(); return true; }
        if(id==WindowNameEditId) { if(!syncing_ && WindowNameCommand(o_,editors_,code)) Refresh(); return true; }
        if(id>=ClassHeaderEditId && id<ClassHeaderEditId+4) {
            if(!syncing_ && code==BN_CLICKED) { o_.table.classHeader[id-ClassHeaderEditId]=editors_.Checked(id); Changed(); }
            return true;
        }
        if(id==BrandLogoEditId || id==BrandNameEditId) {
            if(!syncing_ && code==BN_CLICKED) { (id==BrandLogoEditId?o_.table.brandLogo:o_.table.brandName)=editors_.Checked(id); Changed(); }
            return true;
        }
        if(id==SectorPartsEditId) {
            if(!syncing_ && code==CBN_SELCHANGE && editors_.Selection(id)>=0) { o_.table.sectorParts=editors_.Selection(id)==1?3:1; Changed(); }
            return true;
        }
        if(id==TopSpeedEditId) {
            if(!syncing_ && code==CBN_SELCHANGE && editors_.Selection(id)>=0) { o_.table.topSpeedSession=editors_.Selection(id)==1; Changed(); }
            return true;
        }
        if(id==PositionEditId) {
            if(!syncing_ && code==CBN_SELCHANGE && editors_.Selection(id)>=0) { o_.table.classPositions=editors_.Selection(id)==0; Changed(); }
            return true;
        }
        if(id==StatusEditId) {
            if(!syncing_ && code==BN_CLICKED) { o_.table.showStatus=editors_.Checked(id); Changed(); }
            return true;
        }
        if(id>=NameFormatEditId && id<SlotEditId+SlotCount) {
            if(syncing_) return true;
            if(id==NameFormatEditId && code==CBN_SELCHANGE) o_.table.nameFormat=std::max(0,editors_.Selection(id));
            else if(id>=FlagEditId && id<FlagEditId+3 && code==BN_CLICKED) o_.table.driverFlags[id-FlagEditId]=editors_.Checked(id);
            else if(id==ShortViewEditId && code==BN_CLICKED) o_.table.driverShortView[ShortViewIndex(expandedId_)]=editors_.Checked(id);
            else if(id>=SlotEditId && code==CBN_SELCHANGE) ApplyBandEditor(editors_,o_,expandedKey_==Key::Footer,id,SlotEditId);
            else return true;
            Changed(); return true;
        }
        if(id!=ListId) return false;
        if(code==RowList::kLayoutChanged) { editors_.Position(); return true; }
        const int row=list_.NotifiedRow();
        if(row<0 || static_cast<size_t>(row)>=keys_.size()) return true;
        const Key key=keys_[row]; const int column=ids_[row];
        const bool on=list_.Rows()[row].toggle==1, pressed=code==RowList::kButton || code==RowList::kActivated;
        const int icon=code==RowList::kIconButton?list_.NotifiedIconButton():-1;
        const wchar_t glyph=icon>=0 && icon<static_cast<int>(list_.Rows()[row].iconButtons.size())?list_.Rows()[row].iconButtons[icon]:0;
        const bool toggleExpand=glyph==Glyph::Expand || glyph==Glyph::Collapse || code==RowList::kActivated;
        switch(key) {
        case Key::Window:
            if(column==WindowName) { if(toggleExpand) Expand(key,WindowName); }
            else { WindowCommand(o_,column,code,on,code==RowList::kChoice?list_.NotifiedChoice():(code==RowList::kSlider?list_.Rows()[row].slider:-1),[this](const std::vector<std::wstring>& items,int checked){ return ChooseFromMenu(items,checked); }); Refresh(); }
            break;
        case Key::Rows: case Key::Leaders: case Key::Ahead: case Key::Behind: case Key::OtherClassRows: case Key::PitAhead: case Key::PitBehind:
            if(code==RowList::kSlider) {
                const int value=list_.Rows()[row].slider; auto& standings=o_.table.standings;
                if(key==Key::Rows) o_.table.rows=RelativeRowCount(2*value+1);
                else (key==Key::Leaders?standings.leaders:key==Key::Ahead?standings.ahead:key==Key::Behind?standings.behind
                    :key==Key::OtherClassRows?standings.otherClasses:key==Key::PitAhead?o_.table.pitExitAhead:o_.table.pitExitBehind)=value;
                Changed();
            }
            break;
        case Key::GapDecimals: if(code==RowList::kChoice) { o_.table.gapDecimals=list_.NotifiedChoice(); Changed(); } break;
        case Key::GapSigns: if(code==RowList::kToggled) { SetGapSigns(o_,on); Refresh(); } break;
        case Key::HidePit: if(code==RowList::kToggled) { o_.table.hidePit=on; Changed(); } break;
        case Key::ClassHeader: if(toggleExpand) Expand(key,-1); break;
        case Key::ClassView:
            if(code==RowList::kChoice) { o_.table.standings.separateClasses=list_.NotifiedChoice()==0; Changed(); }
            break;
        case Key::OwnClass: if(code==RowList::kToggled) { o_.table.standings.ownClass=on; Changed(); } break;
        case Key::ClassColors: if(code==RowList::kToggled) { o_.table.classColors=on; Changed(); } break;
        case Key::LapColors: if(code==RowList::kToggled) { o_.table.lapColors=on; Changed(); } break;
        case Key::Progress: if(code==RowList::kToggled) { o_.table.progress=on; Changed(); } break;
        case Key::PitExit: if(code==RowList::kToggled) { o_.table.pitExit=on; Changed(); } break;
        case Key::LiveGaps: if(code==RowList::kToggled) { o_.table.standings.liveGaps=on; Changed(); } break;
        case Key::UntimedOrder:
            if(code==RowList::kChoice) { o_.table.standings.fallbackByRating=list_.NotifiedChoice()==1; Changed(); }
            break;
        case Key::CopyColumns: if(pressed) CopyTo(); break;
        case Key::Column:
            if(code==RowList::kMoved) { const int place=ids_[list_.DropRow()]; Reorder([&](std::vector<int>& order){ return MoveToPlace(order,column,place); }); }
            else if(code==RowList::kToggled) { SetColumnVisible(o_,column,on); Refresh(); }
            else if(toggleExpand && HasOptions(column)) Expand(key,column);
            break;
        case Key::Header: case Key::Footer:
            if(code==RowList::kToggled) { SetBand(o_,key==Key::Footer,on); Changed(); }
            else if(toggleExpand) Expand(key,-1);
            break;
        case Key::Rounded: if(code==RowList::kToggled) { SetRounded(o_,on); Changed(); } break;
        case Key::ColumnHeadings: if(code==RowList::kToggled) { o_.table.columnHeaders=on; Changed(); } break;
        default: break;
        }
        return true;
    }

    Overlay& o_;
    RowList list_; RowEditors editors_;
    std::vector<Key> keys_; std::vector<int> ids_;
    Key expandedKey_=Key::None; int expandedId_=-1;
    bool syncing_=false;
};
HWND CreateTablePage(HWND parent,Overlay& overlay) { return PageWindow::Show(std::make_unique<TablePage>(overlay),app.instance,parent,560,360); }
