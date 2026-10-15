// UI-thread state; workers capture only a source and cache path. One map is shared by all instances.
struct TrackMapState {
    BackgroundTask<trackmap::Loaded> task;
    trackmap::Geometry geometry;
    std::string identity;
    std::wstring message=L"Waiting for iRacing";
    int id=-1;
    unsigned revision=0;
    bool ready=false, cached=false, preview=false;
} trackMap;
std::filesystem::path TrackMapCache() {
    return std::filesystem::path(app.config).parent_path()/L"trackmaps"/L"v1";
}
void RetryTrackMap() { trackMap.task.Cancel(); trackMap.identity.clear(); }
void UpdateTrackMap() {
    const bool enabled=std::any_of(app.overlays.begin(),app.overlays.end(),[](const Overlay& o){return o.kind==TrackMapKind && o.place.enabled;});
    if(!enabled || (!app.preview && !app.frame.connected)) {
        if(trackMap.task.Running() || trackMap.ready || !trackMap.identity.empty()) {
            trackMap.task.Cancel(); trackMap.identity.clear(); trackMap.geometry={}; trackMap.ready=false; ++trackMap.revision;
        }
        trackMap.message=L"Waiting for iRacing"; return;
    }
    const int id=app.preview?353:app.session.trackId;
    const std::string name=app.preview?"limerock 2019 gp":app.session.trackName;
    const std::string identity=std::to_string(id)+":"+trackmap::NormalizeName(name)+(app.preview?":preview":"");
    if(trackMap.identity!=identity) {
        trackMap.task.Cancel(); trackMap.geometry={}; trackMap.ready=false; trackMap.cached=false; ++trackMap.revision;
        trackMap.identity=identity; trackMap.id=id; trackMap.preview=app.preview;
        const auto* source=trackmap::FindSource(id,name);
        if(!source) trackMap.message=trackmap::FindSource(id)?L"Track ID and layout name disagree":L"No official SVG indexed for this layout";
        else {
            trackMap.message=L"Loading official SVGs ...";
            const auto cache=TrackMapCache(); const auto input=*source;
            std::error_code cacheError;
            if(app.diagnostics && !std::filesystem::exists(cache/std::to_wstring(id)/L"source.txt",cacheError)) trackMap.message=L"No cached SVG for diagnostics";
            else if(!trackMap.task.Start([input,cache](const std::atomic_bool& cancelled){return trackmap::Load(input,cache,cancelled);})) trackMap.message=L"Could not start SVG loader; retry in settings";
        }
    }
    std::optional<trackmap::Loaded> result;
    if(trackMap.task.Poll(result)) {
        ++trackMap.revision;
        if(result && result->map) {
            trackMap.geometry=std::move(result->map.geometry); trackMap.ready=true; trackMap.cached=result->cached;
            trackMap.message=trackMap.geometry.direction?L"SVG distance projection":L"Direction unresolved; car positions hidden";
        } else trackMap.message=result?Wide(result->map.error):L"SVG loader failed; retry in settings";
    }
}
struct MapTransform {
    double scale=1,x=0,y=0;
    trackmap::Point Apply(trackmap::Point p) const { return {x+p.x*scale,y+p.y*scale}; }
};
MapTransform MapFit(const trackmap::Geometry& g,int width,int height) {
    const double w=g.high.x-g.low.x,h=g.high.y-g.low.y;
    const double scale=std::min(width/w,height/h);
    return {scale,(width-w*scale)/2-g.low.x*scale,(height-h*scale)/2-g.low.y*scale};
}
void DrawMapLayer(Gdiplus::Graphics& graphics,const trackmap::Layer& layer,const MapTransform& transform,COLORREF color) {
    const Gdiplus::Color ink(255,GetRValue(color),GetGValue(color),GetBValue(color));
    Gdiplus::SolidBrush brush(ink);
    for(const auto& shape:layer.shapes) {
        Gdiplus::GraphicsPath path(shape.evenOdd?Gdiplus::FillModeAlternate:Gdiplus::FillModeWinding);
        for(const auto& line:shape.paths) {
            std::vector<Gdiplus::PointF> points; points.reserve(line.points.size());
            for(const auto p:line.points) { const auto q=transform.Apply(p); points.emplace_back(static_cast<float>(q.x),static_cast<float>(q.y)); }
            if(points.size()<2) continue;
            path.StartFigure(); path.AddLines(points.data(),static_cast<int>(points.size()));
            if(line.closed) path.CloseFigure();
        }
        if(shape.filled) graphics.FillPath(&brush,&path);
        if(shape.strokeWidth>0) {
            Gdiplus::Pen pen(ink,static_cast<float>(std::max(1.,shape.strokeWidth*transform.scale)));
            graphics.DrawPath(&pen,&path);
        }
    }
}
void PaintMap(Overlay& o,const DrawCommand& c) {
    if(!trackMap.ready) return;
    const int width=c.r.right-c.r.left,height=c.r.bottom-c.r.top;
    if(!o.mapBitmap || o.mapRevision!=trackMap.revision || o.mapWidth!=width || o.mapHeight!=height) {
        if(o.mapBitmap) DeleteObject(o.mapBitmap);
        o.mapBitmap=CreateCompatibleBitmap(o.dc,width,height); o.mapWidth=width; o.mapHeight=height; o.mapRevision=trackMap.revision;
        if(!o.mapBitmap) return;
        HDC dc=CreateCompatibleDC(o.dc); const auto old=SelectObject(dc,o.mapBitmap);
        RECT bounds{0,0,width,height}; SetDCBrushColor(dc,Ink::Panel); FillRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        {
            Gdiplus::Graphics graphics(dc); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            const auto transform=MapFit(trackMap.geometry,width,height);
            DrawMapLayer(graphics,trackMap.geometry.pit,transform,Ink::Muted);
            DrawMapLayer(graphics,trackMap.geometry.active,transform,RGB(110,120,134));
            // The stripe is clear on the map; the arrow may lie outside its tight bounds.
            DrawMapLayer(graphics,trackMap.geometry.finish,transform,Ink::White);
        }
        SelectObject(dc,old); DeleteDC(dc);
    }
    HDC source=CreateCompatibleDC(o.dc); const auto old=SelectObject(source,o.mapBitmap);
    BitBlt(o.dc,c.r.left,c.r.top,width,height,source,0,0,SRCCOPY);
    SelectObject(source,old); DeleteDC(source);
}
void PaintMapCar(Overlay& o,const DrawCommand& c) {
    const int width=c.r.right-c.r.left,height=c.r.bottom-c.r.top,stroke=std::max(1,static_cast<int>(c.shape[0]));
    const bool hollow=c.shape[1]!=0; HBITMAP bitmap=nullptr;
    for(const auto& marker:o.mapMarkers) if(marker.width==width && marker.height==height && marker.stroke==stroke && marker.color==c.color && marker.hollow==hollow) { bitmap=marker.bitmap; break; }
    if(!bitmap) {
        // Draw each antialiased symbol once into premultiplied pixels. GDI+ / GDI
        // transitions for every moving car are much more expensive than AlphaBlend.
        BITMAPINFO info{}; info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB};
        void* bits=nullptr; bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!bitmap) return;
        {
            Gdiplus::Bitmap target(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));
            Gdiplus::Graphics graphics(&target); graphics.Clear(Gdiplus::Color(0,0,0,0)); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const Gdiplus::Color ink(255,GetRValue(c.color),GetGValue(c.color),GetBValue(c.color));
            Gdiplus::SolidBrush brush(hollow?Gdiplus::Color(255,24,28,35):ink);
            const float w=static_cast<float>(width),h=static_cast<float>(height),s=static_cast<float>(stroke);
            graphics.FillEllipse(&brush,s,s,w-2*s,h-2*s);
            Gdiplus::Pen pen(hollow?ink:Gdiplus::Color(255,18,22,28),s);
            graphics.DrawEllipse(&pen,s/2,s/2,w-s,h-s);
        }
        // Even the most unusual multiclass session is bounded by MaxCars colors;
        // scale/DPI changes clear the entire cache in Overlay::Free().
        if(o.mapMarkers.size()>=MaxCars*4+4) { for(const auto& marker:o.mapMarkers) DeleteObject(marker.bitmap); o.mapMarkers.clear(); }
        o.mapMarkers.push_back({width,height,stroke,c.color,hollow,bitmap});
    }
    HDC source=CreateCompatibleDC(o.dc); const auto old=SelectObject(source,bitmap);
    AlphaBlend(o.dc,c.r.left,c.r.top,width,height,source,0,0,width,height,{AC_SRC_OVER,0,255,AC_SRC_ALPHA});
    SelectObject(source,old); DeleteDC(source);
}
