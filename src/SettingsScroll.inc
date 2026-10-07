// Shared scrolling and DPI handling for native settings dialogs.
struct SettingsViewport {
    HWND window;
    int width, height, dpi;
    int x=0, y=0, wheel=0;
    bool layout=false;
    HFONT font=nullptr;
};

void ScrollSettings(SettingsViewport& view, int x, int y) {
    RECT client{}; GetClientRect(view.window,&client);
    x=std::clamp(x,0,std::max(0,view.width-static_cast<int>(client.right)));
    y=std::clamp(y,0,std::max(0,view.height-static_cast<int>(client.bottom)));
    const int dx=view.x-x, dy=view.y-y;
    view.x=x; view.y=y;
    if(dx || dy) {
        for(HWND child=GetWindow(view.window,GW_CHILD); child; child=GetWindow(child,GW_HWNDNEXT)) {
            RECT bounds{}; GetWindowRect(child,&bounds);
            MapWindowPoints(nullptr,view.window,reinterpret_cast<POINT*>(&bounds),2);
            SetWindowPos(child,nullptr,bounds.left+dx,bounds.top+dy,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        }
        InvalidateRect(view.window,nullptr,TRUE);
    }
    SetScrollPos(view.window,SB_HORZ,x,TRUE);
    SetScrollPos(view.window,SB_VERT,y,TRUE);
}

void LayoutSettingsViewport(SettingsViewport& view) {
    if(view.layout) return;
    view.layout=true;
    // Start from the space with neither bar. Otherwise each visible bar can
    // keep the other one visible even after the window grows to fit its content.
    RECT client{}; GetClientRect(view.window,&client);
    const LONG_PTR style=GetWindowLongPtrW(view.window,GWL_STYLE);
    const int barWidth=GetSystemMetricsForDpi(SM_CXVSCROLL,view.dpi);
    const int barHeight=GetSystemMetricsForDpi(SM_CYHSCROLL,view.dpi);
    const int width=client.right+((style&WS_VSCROLL)?barWidth:0);
    const int height=client.bottom+((style&WS_HSCROLL)?barHeight:0);
    bool horizontal=view.width>width, vertical=view.height>height;
    horizontal=horizontal || view.width>width-(vertical?barWidth:0);
    vertical=vertical || view.height>height-(horizontal?barHeight:0);
    horizontal=horizontal || view.width>width-(vertical?barWidth:0);
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};
    info.nMax=view.width-1; info.nPage=std::max(0,width-(vertical?barWidth:0)); info.nPos=view.x;
    SetScrollInfo(view.window,SB_HORZ,&info,TRUE);
    info.nMax=view.height-1; info.nPage=std::max(0,height-(horizontal?barHeight:0)); info.nPos=view.y;
    SetScrollInfo(view.window,SB_VERT,&info,TRUE);
    ScrollSettings(view,view.x,view.y);
    view.layout=false;
}

void RevealSetting(SettingsViewport& view, HWND child) {
    RECT bounds{},client{}; GetWindowRect(child,&bounds); GetClientRect(view.window,&client);
    MapWindowPoints(nullptr,view.window,reinterpret_cast<POINT*>(&bounds),2);
    const int margin=MulDiv(8,view.dpi,96);
    int x=view.x, y=view.y;
    if(bounds.left<margin) x+=bounds.left-margin;
    else if(bounds.right>client.right-margin) x+=bounds.right-client.right+margin;
    if(bounds.top<margin) y+=bounds.top-margin;
    else if(bounds.bottom>client.bottom-margin) y+=bounds.bottom-client.bottom+margin;
    ScrollSettings(view,x,y);
}

LRESULT CALLBACK SettingsFocusProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
    if(m==WM_SETFOCUS) RevealSetting(*reinterpret_cast<SettingsViewport*>(data),w);
    if(m==WM_NCDESTROY) RemoveWindowSubclass(w,SettingsFocusProc,id);
    return DefSubclassProc(w,m,wp,lp);
}

RECT SettingsWorkArea(HWND w) {
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&monitor);
    return monitor.rcWork;
}

void FitSettingsWindow(HWND w, RECT bounds) {
    if(!app.diagnostics) {
        const RECT work=SettingsWorkArea(w);
        const LONG width=std::min(bounds.right-bounds.left,work.right-work.left);
        const LONG height=std::min(bounds.bottom-bounds.top,work.bottom-work.top);
        bounds.left=std::clamp(bounds.left,work.left,work.right-width);
        bounds.top=std::clamp(bounds.top,work.top,work.bottom-height);
        bounds.right=bounds.left+width; bounds.bottom=bounds.top+height;
    }
    SetWindowPos(w,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
        SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
}

LRESULT CALLBACK SettingsViewportProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
    auto& view=*reinterpret_cast<SettingsViewport*>(data);
    if(m==WM_SIZE) LayoutSettingsViewport(view);
    if(m==WM_MOUSEWHEEL) {
        view.wheel+=GET_WHEEL_DELTA_WPARAM(wp);
        const int steps=view.wheel/WHEEL_DELTA; view.wheel%=WHEEL_DELTA;
        ScrollSettings(view,view.x,view.y-steps*MulDiv(48,view.dpi,96)); return 0;
    }
    if(m==WM_VSCROLL || m==WM_HSCROLL) {
        const bool vertical=m==WM_VSCROLL;
        SCROLLINFO info{sizeof(info),SIF_ALL}; GetScrollInfo(w,vertical?SB_VERT:SB_HORZ,&info);
        int target=vertical?view.y:view.x;
        switch(LOWORD(wp)) {
        case SB_LINEUP: target-=MulDiv(24,view.dpi,96); break;
        case SB_LINEDOWN: target+=MulDiv(24,view.dpi,96); break;
        case SB_PAGEUP: target-=info.nPage; break;
        case SB_PAGEDOWN: target+=info.nPage; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: target=info.nTrackPos; break;
        case SB_TOP: target=0; break;
        case SB_BOTTOM: target=info.nMax; break;
        }
        ScrollSettings(view,vertical?view.x:target,vertical?target:view.y); return 0;
    }
    if(m==WM_DPICHANGED) {
        const int dpi=HIWORD(wp), previousDpi=view.dpi;
        LOGFONTW font{}; GetObjectW(view.font,sizeof(font),&font);
        font.lfHeight=MulDiv(font.lfHeight,dpi,previousDpi);
        HFONT replacement=CreateFontIndirectW(&font);
        for(HWND child=GetWindow(w,GW_CHILD); child; child=GetWindow(child,GW_HWNDNEXT)) {
            RECT bounds{}; GetWindowRect(child,&bounds);
            wchar_t type[32]{}; GetClassNameW(child,type,32);
            if(wcscmp(type,L"ComboBox")==0) SendMessageW(child,CB_GETDROPPEDCONTROLRECT,0,reinterpret_cast<LPARAM>(&bounds));
            MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&bounds),2);
            SetWindowPos(child,nullptr,MulDiv(bounds.left+view.x,dpi,previousDpi),MulDiv(bounds.top+view.y,dpi,previousDpi),
                MulDiv(bounds.right-bounds.left,dpi,previousDpi),MulDiv(bounds.bottom-bounds.top,dpi,previousDpi),SWP_NOZORDER|SWP_NOACTIVATE);
            if(replacement) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(replacement),TRUE);
        }
        if(replacement) { DeleteObject(view.font); view.font=replacement; SetPropW(w,L"SettingsFont",replacement); }
        view.width=MulDiv(view.width,dpi,previousDpi); view.height=MulDiv(view.height,dpi,previousDpi);
        view.dpi=dpi; view.x=view.y=0;
        FitSettingsWindow(w,*reinterpret_cast<RECT*>(lp)); LayoutSettingsViewport(view);
        const HWND focus=GetFocus(); if(focus && IsChild(w,focus)) RevealSetting(view,focus);
        return 0;
    }
    if(m==WM_NCDESTROY) {
        RemoveWindowSubclass(w,SettingsViewportProc,id);
        RemovePropW(w,L"SettingsViewport"); RemovePropW(w,L"SettingsFont");
        DeleteObject(view.font); delete &view;
    }
    return DefSubclassProc(w,m,wp,lp);
}

void MakeSettingsScrollable(HWND w,int width,int height) {
    const int dpi=GetDpiForWindow(w);
    auto* view=new SettingsViewport{w,MulDiv(width,dpi,96),MulDiv(height,dpi,96),dpi};
    view->font=reinterpret_cast<HFONT>(GetPropW(w,L"SettingsFont"));
    if(!view->font) view->font=CreateFontW(-MulDiv(13,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    SetPropW(w,L"SettingsFont",view->font); SetPropW(w,L"SettingsViewport",view);
    SetWindowSubclass(w,SettingsViewportProc,1,reinterpret_cast<DWORD_PTR>(view));
    for(HWND child=GetWindow(w,GW_CHILD); child; child=GetWindow(child,GW_HWNDNEXT)) {
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(view->font),TRUE);
        SetWindowSubclass(child,SettingsFocusProc,1,reinterpret_cast<DWORD_PTR>(view));
    }
    const auto style=static_cast<DWORD>(GetWindowLongPtrW(w,GWL_STYLE))|WS_VSCROLL|WS_HSCROLL;
    SetWindowLongPtrW(w,GWL_STYLE,style);
    RECT bounds{},size{0,0,view->width,view->height}; GetWindowRect(w,&bounds);
    AdjustWindowRectExForDpi(&size,style,FALSE,static_cast<DWORD>(GetWindowLongPtrW(w,GWL_EXSTYLE)),dpi);
    bounds.right=bounds.left+size.right-size.left; bounds.bottom=bounds.top+size.bottom-size.top;
    if(!app.diagnostics) {
        const RECT work=SettingsWorkArea(w);
        if(bounds.bottom-bounds.top>work.bottom-work.top) bounds.right+=GetSystemMetricsForDpi(SM_CXVSCROLL,dpi);
    }
    FitSettingsWindow(w,bounds); LayoutSettingsViewport(*view);
}
