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
void RetryTrackMap(bool retryLabels=false) {
    trackMap.task.Cancel(); trackMap.identity.clear();
    if(retryLabels && trackMap.id>=0) { std::error_code error; std::filesystem::remove(TrackMapCache()/std::to_wstring(trackMap.id)/L"turns-attempt.txt",error); }
}
void UpdateTrackMap() {
    // Only the official map needs the SVGs; circle and line work without them.
    const bool enabled=std::any_of(app.overlays.begin(),app.overlays.end(),[](const Overlay& o){return o.kind==TrackMapKind && o.place.enabled && o.map.layout==MapLayout;});
    if(!enabled || (!app.preview && !app.frame.connected)) {
        if(trackMap.task.Running() || trackMap.ready || !trackMap.identity.empty()) {
            trackMap.task.Cancel(); trackMap.identity.clear(); trackMap.geometry={}; trackMap.ready=false; ++trackMap.revision;
        }
        trackMap.message=L"Waiting for iRacing"; return;
    }
    const int id=app.preview?353:app.session.trackId;
    const std::string name=app.preview?"limerock 2019 gp":app.session.trackName;
    const bool labels=std::any_of(app.overlays.begin(),app.overlays.end(),[](const Overlay& o){return o.kind==TrackMapKind && o.place.enabled && o.map.layout==MapLayout && (o.map.cornerNumbers || o.map.cornerNames);});
    const std::string layout=std::to_string(id)+":"+trackmap::NormalizeName(name)+(app.preview?":preview":"");
    const std::string identity=layout+(labels?":labels":"");
    if(trackMap.identity!=identity) {
        const bool sameLayout=trackMap.ready && (trackMap.identity==layout || trackMap.identity==layout+":labels");
        trackMap.task.Cancel();
        if(!sameLayout) { trackMap.geometry={}; trackMap.ready=false; trackMap.cached=false; ++trackMap.revision; }
        trackMap.identity=identity; trackMap.id=id; trackMap.preview=app.preview;
        const auto* source=trackmap::FindSource(id,name);
        if(!source) trackMap.message=trackmap::FindSource(id)?L"Track ID and layout name disagree":L"No official SVG indexed for this layout";
        else if(!sameLayout || (labels && trackMap.geometry.labels.empty())) {
            trackMap.message=L"Loading official SVGs ...";
            const auto cache=TrackMapCache(); const auto input=*source;
            std::error_code cacheError;
            if(app.diagnostics && !std::filesystem::exists(cache/std::to_wstring(id)/L"source.txt",cacheError)) trackMap.message=L"No cached SVG for diagnostics";
            else if(!trackMap.task.Start([input,cache,labels](const std::atomic_bool& cancelled){return trackmap::Load(input,cache,cancelled,labels);})) trackMap.message=L"Could not start SVG loader; retry in settings";
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
bool ShowMapLabel(const trackmap::Label& label,const TrackMapSettings& settings) {
    return label.number?settings.cornerNumbers:settings.cornerNames;
}
int MapStyle(const TrackMapSettings& settings) {
    return (settings.background?1:0)|(settings.pitLane?2:0)|(settings.startFinish?4:0)
        |(settings.cornerNumbers?8:0)|(settings.cornerNames?16:0)|(settings.labelContrast?32:0)|(settings.labelScale<<6)|(settings.layout<<15);
}
// Circle and line: where a lap fraction lies in an area of this physical size; the scene and the static drawing share it.
struct MapShapeInfo {
    int layout=MapLayout; double width=0,height=0,inset=0;
    double Radius() const { return std::max(1.,std::min(width,height)/2-inset); }
    trackmap::Point At(double pct) const {
        if(layout==LineLayout) return {inset+pct*(width-2*inset),height/2};
        const double angle=2*3.141592653589793*pct;
        return {width/2+Radius()*std::sin(angle),height/2-Radius()*std::cos(angle)};
    }
};
MapShapeInfo MapShape(const Overlay& o,int width,int height) {
    return {o.map.layout,static_cast<double>(width),static_cast<double>(height),static_cast<double>(MulDiv(14,o.dpi*o.place.scale,9600))};
}
MapTransform MapFit(const trackmap::Geometry& g,int width,int height,const TrackMapSettings& settings) {
    auto low=g.low,high=g.high;
    for(const auto& label:g.labels) if(ShowMapLabel(label,settings)) {
        // Conservative text bounds also reserve room for names outside the road.
        const double size=label.size*settings.labelScale/100.,w=label.text.size()*size*.75;
        const double left=label.anchor==1?-w/2:(label.anchor==2?-w:0);
        const double cosine=std::cos(label.angle),sine=std::sin(label.angle);
        for(double x:{left-size*.1,left+w+size*.1}) for(double y:{-size*1.1,size*.3}) {
            const trackmap::Point p{label.position.x+x*cosine-y*sine,label.position.y+x*sine+y*cosine};
            low.x=std::min(low.x,p.x); low.y=std::min(low.y,p.y); high.x=std::max(high.x,p.x); high.y=std::max(high.y,p.y);
        }
    }
    const double w=high.x-low.x,h=high.y-low.y;
    // Small inset protects the contrast outline against clipping at bitmap edges.
    width=std::max(8,width); height=std::max(8,height);
    const int inset=(settings.cornerNumbers || settings.cornerNames)?3:0;
    const double scale=std::min((width-2*inset)/w,(height-2*inset)/h);
    return {scale,(width-w*scale)/2-low.x*scale,(height-h*scale)/2-low.y*scale};
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
    if(o.map.layout==MapLayout && !trackMap.ready) return;
    const int width=c.r.right-c.r.left,height=c.r.bottom-c.r.top;
    const int style=MapStyle(o.map);
    if(!o.mapBitmap || o.mapRevision!=trackMap.revision || o.mapWidth!=width || o.mapHeight!=height || o.mapStyle!=style) {
        if(o.mapBitmap) DeleteObject(o.mapBitmap);
        o.mapBitmap=CreateCompatibleBitmap(o.dc,width,height); o.mapWidth=width; o.mapHeight=height; o.mapRevision=trackMap.revision; o.mapStyle=style;
        if(!o.mapBitmap) return;
        HDC dc=CreateCompatibleDC(o.dc); const auto old=SelectObject(dc,o.mapBitmap);
        RECT bounds{0,0,width,height}; SetDCBrushColor(dc,MapOnly(o)?TrackMapTransparentKey:Ink::Panel); FillRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        {
            Gdiplus::Graphics graphics(dc); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            if(o.map.layout!=MapLayout) {
                // The circuit as a ring or a bar in the map's color; start/finish as a white mark at 0 %.
                const auto shape=MapShape(o,width,height); const float stroke=static_cast<float>(std::max(2,MulDiv(6,o.dpi*o.place.scale,9600)));
                Gdiplus::Pen road(Gdiplus::Color(255,110,120,134),stroke); road.SetStartCap(Gdiplus::LineCapRound); road.SetEndCap(Gdiplus::LineCapRound);
                const float w=static_cast<float>(width),h=static_cast<float>(height),inset=static_cast<float>(shape.inset),r=static_cast<float>(shape.Radius());
                if(o.map.layout==CircleLayout) graphics.DrawEllipse(&road,w/2-r,h/2-r,2*r,2*r);
                else graphics.DrawLine(&road,inset,h/2,w-inset,h/2);
                if(o.map.startFinish) {
                    Gdiplus::Pen mark(Gdiplus::Color(255,255,255,255),std::max(2.f,stroke/2));
                    if(o.map.layout==CircleLayout) graphics.DrawLine(&mark,w/2,h/2-r-stroke,w/2,h/2-r+stroke);
                    else for(float x:{inset,w-inset}) graphics.DrawLine(&mark,x,h/2-stroke*1.5f,x,h/2+stroke*1.5f);
                }
            } else {
                const auto transform=MapFit(trackMap.geometry,width,height,o.map);
                if(o.map.pitLane) DrawMapLayer(graphics,trackMap.geometry.pit,transform,Ink::Muted);
                DrawMapLayer(graphics,trackMap.geometry.active,transform,RGB(110,120,134));
                // The stripe is clear on the map; the arrow may lie outside its tight bounds.
                if(o.map.startFinish) DrawMapLayer(graphics,trackMap.geometry.finish,transform,Ink::White);
                Gdiplus::FontFamily family(L"Segoe UI");
                Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
                format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                Gdiplus::SolidBrush brush(Gdiplus::Color(255,232,237,242));
                Gdiplus::Pen outline(Gdiplus::Color(255,12,15,19),static_cast<float>(std::max(1.,1.4*o.place.scale/100.)));
                outline.SetLineJoin(Gdiplus::LineJoinRound);
                for(const auto& item:trackMap.geometry.labels) if(ShowMapLabel(item,o.map)) {
                    const auto text=Wide(item.text); const auto p=transform.Apply(item.position);
                    const float size=static_cast<float>(item.size*transform.scale*o.map.labelScale/100.);
                    const auto fontStyle=item.number?Gdiplus::FontStyleBold:Gdiplus::FontStyleRegular;
                    const float ascent=size*family.GetCellAscent(fontStyle)/family.GetEmHeight(fontStyle);
                    format.SetAlignment(item.anchor==1?Gdiplus::StringAlignmentCenter:(item.anchor==2?Gdiplus::StringAlignmentFar:Gdiplus::StringAlignmentNear));
                    Gdiplus::GraphicsPath path;
                    path.AddString(text.c_str(),static_cast<int>(text.size()),&family,fontStyle,size,Gdiplus::PointF(0,-ascent),&format);
                    Gdiplus::Matrix matrix;
                    matrix.Translate(static_cast<float>(p.x),static_cast<float>(p.y));
                    matrix.Rotate(static_cast<float>(item.angle*180/3.141592653589793)); path.Transform(&matrix);
                    if(o.map.labelContrast) graphics.DrawPath(&outline,&path);
                    graphics.FillPath(&brush,&path);
                }
            }
        }
        SelectObject(dc,old); DeleteDC(dc);
    }
    HDC source=CreateCompatibleDC(o.dc); const auto old=SelectObject(source,o.mapBitmap);
    BitBlt(o.dc,c.r.left,c.r.top,width,height,source,0,0,SRCCOPY);
    SelectObject(source,old); DeleteDC(source);
    // The cars follow: copy the map area with room for markers at its edge into the canvas.
    const int margin=MulDiv(18,o.dpi*o.place.scale,9600);
    RECT area=c.r; InflateRect(&area,margin,margin);
    const RECT buffer{0,0,o.width,o.height}; IntersectRect(&area,&area,&buffer);
    const int w=area.right-area.left,h=area.bottom-area.top;
    if(w<=0 || h<=0) return;
    if(!o.canvasDc || o.canvasRect.right-o.canvasRect.left!=w || o.canvasRect.bottom-o.canvasRect.top!=h) {
        if(o.canvasDc) { SelectObject(o.canvasDc,o.canvasOriginal); DeleteDC(o.canvasDc); DeleteObject(o.canvas); o.canvasDc=nullptr; }
        BITMAPINFO info{}; info.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB}; void* bits=nullptr;
        o.canvas=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!o.canvas) return;
        o.canvasDc=CreateCompatibleDC(o.dc); o.canvasOriginal=SelectObject(o.canvasDc,o.canvas); o.canvasBits=static_cast<std::uint32_t*>(bits);
    }
    o.canvasRect=area;
    BitBlt(o.canvasDc,0,0,w,h,o.dc,area.left,area.top,SRCCOPY); GdiFlush();
    o.canvasPending=true;
}
// Back into the buffer, clipped like every other command.
void FlushMapCanvas(Overlay& o) {
    o.canvasPending=false;
    const auto& r=o.canvasRect;
    BitBlt(o.dc,r.left,r.top,r.right-r.left,r.bottom-r.top,o.canvasDc,0,0,SRCCOPY);
}
// Premultiplied source over the canvas: a few hundred pixels per car instead of one AlphaBlend call each.
void BlendMarker(Overlay& o,const RECT& r,const std::uint32_t* source,int width,int height) {
    const int W=o.canvasRect.right-o.canvasRect.left,H=o.canvasRect.bottom-o.canvasRect.top;
    const int left=r.left-o.canvasRect.left,top=r.top-o.canvasRect.top;
    for(int y=std::max(0,-top);y<height && top+y<H;++y) {
        const std::uint32_t* from=source+y*width; std::uint32_t* to=o.canvasBits+(top+y)*W+left;
        for(int x=std::max(0,-left);x<width && left+x<W;++x) {
            const std::uint32_t s=from[x],alpha=s>>24;
            if(!alpha) continue;
            if(alpha==255) { to[x]=s; continue; }
            const std::uint32_t d=to[x],keep=255-alpha;
            auto channel=[&](int shift){ return ((s>>shift)&255)+(((d>>shift)&255)*keep+127)/255; };
            to[x]=channel(0)|channel(8)<<8|channel(16)<<16|0xFF000000u;
        }
    }
}
void PaintMapCar(Overlay& o,const DrawCommand& c) {
    const int width=c.r.right-c.r.left,height=c.r.bottom-c.r.top,stroke=std::max(1,static_cast<int>(c.shape[0]));
    const bool hollow=c.shape[1]!=0; HBITMAP bitmap=nullptr; const std::uint32_t* pixels=nullptr;
    for(const auto& marker:o.mapMarkers) if(marker.width==width && marker.height==height && marker.stroke==stroke && marker.color==c.color && marker.hollow==hollow) { bitmap=marker.bitmap; pixels=marker.bits; break; }
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
        GdiFlush(); pixels=static_cast<const std::uint32_t*>(bits);
        o.mapMarkers.push_back({width,height,stroke,c.color,hollow,bitmap,pixels});
    }
    if(o.canvasPending) { BlendMarker(o,c.r,pixels,width,height); return; }
    // Only the cars changed and the map was not repainted: blend directly.
    HDC source=CreateCompatibleDC(o.dc); const auto old=SelectObject(source,bitmap);
    AlphaBlend(o.dc,c.r.left,c.r.top,width,height,source,0,0,width,height,{AC_SRC_OVER,0,255,AC_SRC_ALPHA});
    SelectObject(source,old); DeleteDC(source);
}
