// Snapping while an overlay is dragged: its edges and centre lines go to those of the other overlays and of the
// monitor within reach, else its fixed corner goes to a grid from the monitor's corner. Alt held places freely.
// Guide lines show what it snapped to. Nothing here runs unless an overlay is being dragged.
constexpr int SnapReach=8; // In DIPs.
constexpr COLORREF GuideInk=RGB(255,64,200); // Magenta, as smart guides usually are; distinct from the yellow and red rings.
// A line an edge can snap to: where it lies across, and how far it reaches along, for its guide.
struct SnapLine { int at, from, to; };
// The shift that brings the nearest of an edge's start, middle and end onto a line within reach; hit tells which.
int SnapAxis(int start,int size,const std::vector<SnapLine>& lines,int reach,const SnapLine*& hit) {
    int best=reach+1, shift=0; hit=nullptr;
    for(const auto& line:lines) for(int edge:{start,start+size/2,start+size}) {
        const int distance=line.at-edge;
        if(std::abs(distance)<best) { best=std::abs(distance); shift=distance; hit=&line; }
    }
    return shift;
}
int SnapToGrid(int value,int origin,int grid) {
    return grid>1?origin+static_cast<int>(std::lround(static_cast<double>(value-origin)/grid))*grid:value;
}
// One window per guide: the vertical, then the horizontal. Hidden whenever nothing snaps.
HWND guideWindows[2]{};
void HideGuides() { for(HWND guide:guideWindows) if(guide && IsWindowVisible(guide)) ShowWindow(guide,SW_HIDE); }
// The guide runs along the snapped line from the target to the dragged overlay.
void ShowGuide(int index,const SnapLine* line,const RECT& at,int thick) {
    HWND& guide=guideWindows[index];
    if(!line) { if(guide && IsWindowVisible(guide)) ShowWindow(guide,SW_HIDE); return; }
    if(!guide) {
        guide=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,L"RaceMate.Guide",L"",WS_POPUP,0,0,0,0,nullptr,nullptr,app.instance,nullptr);
        if(!guide) return;
        SetLayeredWindowAttributes(guide,0,255,LWA_ALPHA);
    }
    const bool vertical=index==0;
    const int from=std::min<int>(line->from,vertical?at.top:at.left), to=std::max<int>(line->to,vertical?at.bottom:at.right);
    if(vertical) SetWindowPos(guide,HWND_TOPMOST,line->at-thick/2,from,thick,to-from,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    else SetWindowPos(guide,HWND_TOPMOST,from,line->at-thick/2,to-from,thick,SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
// Moves a dragged overlay's proposed place onto the nearest line or the grid. The Side Warning keeps its place
// across, so only its height snaps.
void SnapDrag(const Overlay& o,WINDOWPOS& p) {
    if(GetKeyState(VK_MENU)<0 || (!app.snapToGrid && !app.snapToOverlays)) { HideGuides(); return; }
    RECT now{}; GetWindowRect(o.window,&now);
    const int w=p.flags&SWP_NOSIZE?now.right-now.left:p.cx, h=p.flags&SWP_NOSIZE?now.bottom-now.top:p.cy;
    RECT at{p.x,p.y,p.x+w,p.y+h};
    MONITORINFO info{sizeof(info)};
    if(!GetMonitorInfoW(MonitorFromRect(&at,MONITOR_DEFAULTTONEAREST),&info)) return;
    const int dpi=GetDpiForWindow(o.window);
    std::vector<SnapLine> across, down; // Vertical lines, placed across; horizontal lines, placed down.
    if(app.snapToOverlays) {
        auto add=[&](const RECT& r) {
            for(int x:{static_cast<int>(r.left),static_cast<int>(r.left+r.right)/2,static_cast<int>(r.right)}) across.push_back({x,r.top,r.bottom});
            for(int y:{static_cast<int>(r.top),static_cast<int>(r.top+r.bottom)/2,static_cast<int>(r.bottom)}) down.push_back({y,r.left,r.right});
        };
        add(info.rcMonitor);
        for(const auto& other:app.overlays) {
            if(&other==&o) continue;
            for(HWND window:{other.window,other.mirror}) if(window && IsWindowVisible(window)) { RECT r{}; GetWindowRect(window,&r); add(r); }
        }
    }
    const bool side=o.kind==SideWarningKind;
    const int reach=MulDiv(SnapReach,dpi,96), grid=MulDiv(app.gridSize,dpi,96);
    const SnapLine *x=nullptr, *y=nullptr;
    int dx=side?0:SnapAxis(at.left,w,across,reach,x), dy=SnapAxis(at.top,h,down,reach,y);
    if(side) x=nullptr;
    // The grid holds the corner the overlay grows from, so its stored place lies on the grid.
    if(!x && !side && app.snapToGrid) { const int edge=o.place.growLeft?at.right:at.left; dx=SnapToGrid(edge,info.rcMonitor.left,grid)-edge; }
    if(!y && app.snapToGrid) { const int edge=o.place.growUp?at.bottom:at.top; dy=SnapToGrid(edge,info.rcMonitor.top,grid)-edge; }
    p.x+=dx; p.y+=dy; OffsetRect(&at,dx,dy);
    const int thick=std::max(1,MulDiv(2,dpi,96));
    ShowGuide(0,x,at,thick); ShowGuide(1,y,at,thick);
}
