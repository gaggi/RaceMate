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
// Where north lies on the official map: TrackNorthOffset from the session info, until your car in its pit stall
// settles it. A settled turn is kept per track (all its layouts are drawn alike) in north.ini beside the SVGs.
struct NorthState { std::string track; int settled=-1; } north;
std::wstring NorthFile() { return (TrackMapCache()/L"north.ini").wstring(); }
void ForgetNorth() {
    if(!north.track.empty()) WritePrivateProfileStringW(L"North",Wide(north.track).c_str(),nullptr,NorthFile().c_str());
    north.settled=-1;
}
int MapNorthTurn() { return north.settled>=0?north.settled:NorthTurnFromOffset(app.session.trackNorthOffset); }
void RefreshPage();
// Your car in its pit stall stands along the pit lane: its YawNorth against the drawn pit lane beside the stall
// (the track there, without a pit drawing) settles north. Once settled, nothing more is measured on this track.
void UpdateNorth() {
    const std::string track=app.preview?std::string():trackmap::TrackFolder(app.session.trackId);
    if(track!=north.track) {
        north={}; north.track=track;
        if(!track.empty()) { std::error_code error; std::filesystem::create_directories(TrackMapCache(),error); north.settled=static_cast<int>(GetPrivateProfileIntW(L"North",Wide(track).c_str(),-1,NorthFile().c_str())); }
    }
    if(track.empty() || north.settled>=0 || !trackMap.ready || trackMap.preview || trackMap.id!=app.session.trackId) return;
    const auto& f=app.frame;
    if(!f.onTrack || f.replay || f.player<0 || f.player>=MaxCars || ReferenceCar(f)!=f.player) return;
    const auto& c=f.cars[f.player];
    if(c.surface!=1 || c.pct<0 || c.speed>.5) return;
    const auto& g=trackMap.geometry; double heading=g.PitHeading(c.pct);
    if(!std::isfinite(heading)) heading=g.Heading(c.pct);
    const int measured=NorthTurnFromHeadings(f.yawNorth,heading); if(measured<0) return;
    // Within 2 degrees of the session info, that stays; otherwise the pit box.
    const int formula=NorthTurnFromOffset(app.session.trackNorthOffset), apart=formula<0?360:std::abs(measured-formula)%360;
    north.settled=std::min(apart,360-apart)<=2?formula:measured;
    WritePrivateProfileStringW(L"North",Wide(track).c_str(),std::to_wstring(north.settled).c_str(),NorthFile().c_str());
    ++trackMap.revision; RefreshPage();
}
// A wind arrow while you watch another car live takes its heading from the official map, when its north is known.
bool WindArrowNeedsMap() {
    return !app.preview && MapNorthTurn()>=0 && std::any_of(app.overlays.begin(),app.overlays.end(),[](const Overlay& o){
        const auto& d=o.weather.display; return o.kind==WeatherKind && o.place.enabled && d[WindModule] && d[WindRose] && d[WindRelative]; });
}
// The reference car's compass heading from the official map: its direction of travel there, turned to north.
double MapHeading() {
    const int turn=MapNorthTurn(), car=ReferenceCar(app.frame);
    if(!trackMap.ready || trackMap.preview || turn<0 || car<0 || car>=MaxCars) return std::numeric_limits<double>::quiet_NaN();
    // Not in the world or in the pit stall: no direction of travel.
    const auto& c=app.frame.cars[car];
    if(c.pct<0 || c.surface<0 || c.surface==1) return std::numeric_limits<double>::quiet_NaN();
    constexpr double pi=3.141592653589793;
    return std::fmod(trackMap.geometry.Heading(c.pct)+turn*pi/180,2*pi);
}
void UpdateTrackMap() {
    // Only the official map needs the SVGs; circle and line work without them.
    const bool enabled=WindArrowNeedsMap() || std::any_of(app.overlays.begin(),app.overlays.end(),[](const Overlay& o){return o.kind==TrackMapKind && o.place.enabled && o.map.layout==MapLayout;});
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
    UpdateNorth();
}
struct MapTransform {
    double scale=1,x=0,y=0;
    double cosine=1,sine=0; // The drawing turned clockwise, for north up.
    void Turn(int degrees) { cosine=std::cos(degrees*3.141592653589793/180); sine=std::sin(degrees*3.141592653589793/180); }
    trackmap::Point Apply(trackmap::Point p) const { return {x+(p.x*cosine-p.y*sine)*scale,y+(p.x*sine+p.y*cosine)*scale}; }
};
bool ShowMapLabel(const trackmap::Label& label,const TrackMapSettings& settings) {
    return label.number?settings.cornerNumbers:settings.cornerNames;
}
int MapStyle(const TrackMapSettings& settings) {
    return (settings.background?1:0)|(settings.pitLane?2:0)|(settings.startFinish?4:0)
        |(settings.cornerNumbers?8:0)|(settings.cornerNames?16:0)|(settings.labelContrast?32:0)|(settings.labelScale<<6)|(settings.layout<<15)|(settings.whiteTrack?1<<17:0)
        |(settings.surroundings?1<<18:0)|(settings.allConfigs?1<<19:0)|(settings.northUp?1<<20:0);
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
    // North up turns the map when its north is known; bounds are taken after turning, labels stay upright.
    MapTransform transform; transform.Turn(settings.northUp?std::max(0,MapNorthTurn()):0);
    trackmap::Point low{std::numeric_limits<double>::max(),std::numeric_limits<double>::max()},high{-low.x,-low.y};
    auto extend=[&](trackmap::Point p) { low.x=std::min(low.x,p.x); low.y=std::min(low.y,p.y); high.x=std::max(high.x,p.x); high.y=std::max(high.y,p.y); };
    // Turned by an odd angle, the corners of the bounds would leave a margin: the drawing itself then.
    if(transform.sine==0) for(double x:{g.low.x,g.high.x}) for(double y:{g.low.y,g.high.y}) extend(transform.Apply({x,y}));
    else for(const auto* layer:{&g.active,&g.finish}) for(const auto& shape:layer->shapes) for(const auto& path:shape.paths) for(const auto p:path.points) extend(transform.Apply(p));
    // The other configurations shown whole; the surroundings may run off the edges.
    if(settings.allConfigs) for(const auto& shape:g.inactive.shapes) for(const auto& path:shape.paths) for(const auto p:path.points) extend(transform.Apply(p));
    for(const auto& label:g.labels) if(ShowMapLabel(label,settings)) {
        // Conservative text bounds also reserve room for names outside the road.
        const double size=label.size*settings.labelScale/100.,w=label.text.size()*size*.75;
        const double left=label.anchor==1?-w/2:(label.anchor==2?-w:0);
        const double cosine=std::cos(label.angle),sine=std::sin(label.angle);
        const auto at=transform.Apply(label.position);
        for(double x:{left-size*.1,left+w+size*.1}) for(double y:{-size*1.1,size*.3}) extend({at.x+x*cosine-y*sine,at.y+x*sine+y*cosine});
    }
    const double w=high.x-low.x,h=high.y-low.y;
    // Small inset protects the contrast outline against clipping at bitmap edges.
    width=std::max(8,width); height=std::max(8,height);
    const int inset=(settings.cornerNumbers || settings.cornerNames)?3:0;
    const double scale=std::min((width-2*inset)/w,(height-2*inset)/h);
    transform.scale=scale; transform.x=(width-w*scale)/2-low.x*scale; transform.y=(height-h*scale)/2-low.y*scale;
    return transform;
}
// A layer in one colour; with an edge, a black rim of that width goes around every shape first, so one shape's
// rim never cuts into the next.
void DrawMapLayer(Gdiplus::Graphics& graphics,const trackmap::Layer& layer,const MapTransform& transform,COLORREF color,float edge=0) {
    const Gdiplus::Color ink(255,GetRValue(color),GetGValue(color),GetBValue(color)), black(255,0,0,0);
    Gdiplus::SolidBrush brush(ink);
    std::vector<std::unique_ptr<Gdiplus::GraphicsPath>> paths;
    for(const auto& shape:layer.shapes) {
        auto path=std::make_unique<Gdiplus::GraphicsPath>(shape.evenOdd?Gdiplus::FillModeAlternate:Gdiplus::FillModeWinding);
        for(const auto& line:shape.paths) {
            std::vector<Gdiplus::PointF> points; points.reserve(line.points.size());
            for(const auto p:line.points) { const auto q=transform.Apply(p); points.emplace_back(static_cast<float>(q.x),static_cast<float>(q.y)); }
            if(points.size()<2) continue;
            path->StartFigure(); path->AddLines(points.data(),static_cast<int>(points.size()));
            if(line.closed) path->CloseFigure();
        }
        paths.push_back(std::move(path));
    }
    auto width=[&](const trackmap::Shape& shape){ return static_cast<float>(std::max(1.,shape.strokeWidth*transform.scale)); };
    if(edge>0) for(size_t i=0;i<paths.size();++i) {
        const auto& shape=layer.shapes[i];
        Gdiplus::Pen rim(black,(shape.strokeWidth>0?width(shape):0)+2*edge); rim.SetLineJoin(Gdiplus::LineJoinRound); rim.SetStartCap(Gdiplus::LineCapRound); rim.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawPath(&rim,paths[i].get());
    }
    for(size_t i=0;i<paths.size();++i) {
        const auto& shape=layer.shapes[i];
        if(shape.filled) graphics.FillPath(&brush,paths[i].get());
        if(shape.strokeWidth>0) { Gdiplus::Pen pen(ink,width(shape)); pen.SetLineJoin(Gdiplus::LineJoinRound); graphics.DrawPath(&pen,paths[i].get()); }
    }
}
void PaintMap(Overlay& o,const DrawCommand& c,HRGN dirty) {
    if(o.map.layout==MapLayout && !trackMap.ready) return;
    const int width=c.r.right-c.r.left,height=c.r.bottom-c.r.top;
    const int style=MapStyle(o.map);
    if(!o.mapBitmap || o.mapRevision!=trackMap.revision || o.mapWidth!=width || o.mapHeight!=height || o.mapStyle!=style) {
        if(o.mapBitmap) DeleteObject(o.mapBitmap);
        // A DIB, so a whole-map pass copies its rows straight into the buffer.
        BITMAPINFO info{}; info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB}; void* bits=nullptr;
        o.mapBitmap=CreateDIBSection(o.dc,&info,DIB_RGB_COLORS,&bits,nullptr,0); o.mapPixels=static_cast<const std::uint32_t*>(bits);
        o.mapWidth=width; o.mapHeight=height; o.mapRevision=trackMap.revision; o.mapStyle=style;
        if(!o.mapBitmap) return;
        {
            // Drawn with alpha: without background the empty map is transparent and every edge keeps its coverage.
            Gdiplus::Bitmap canvas(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));
            Gdiplus::Graphics graphics(&canvas); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            graphics.Clear(MapOnly(o)?Gdiplus::Color(0,0,0,0):Gdiplus::Color(255,12,15,19));
            if(o.map.layout!=MapLayout) {
                // The circuit as a ring or a bar in the map's color; start/finish as a white mark at 0 %.
                const auto shape=MapShape(o,width,height); const float stroke=static_cast<float>(std::max(2,MulDiv(6,o.dpi*o.place.scale,9600)));
                const float edge=o.map.whiteTrack?std::max(1.f,static_cast<float>(MulDiv(15,o.dpi*o.place.scale,9600))/10):0;
                Gdiplus::Pen rim(Gdiplus::Color(255,0,0,0),stroke+2*edge); rim.SetStartCap(Gdiplus::LineCapRound); rim.SetEndCap(Gdiplus::LineCapRound);
                Gdiplus::Pen road(o.map.whiteTrack?Gdiplus::Color(255,255,255,255):Gdiplus::Color(255,110,120,134),stroke); road.SetStartCap(Gdiplus::LineCapRound); road.SetEndCap(Gdiplus::LineCapRound);
                const float w=static_cast<float>(width),h=static_cast<float>(height),inset=static_cast<float>(shape.inset),r=static_cast<float>(shape.Radius());
                for(auto* pen:{&rim,&road}) {
                    if(pen==&rim && edge<=0) continue;
                    if(o.map.layout==CircleLayout) graphics.DrawEllipse(pen,w/2-r,h/2-r,2*r,2*r);
                    else graphics.DrawLine(pen,inset,h/2,w-inset,h/2);
                }
                if(o.map.startFinish) {
                    // On a white track the mark is black.
                    Gdiplus::Pen mark(o.map.whiteTrack?Gdiplus::Color(255,0,0,0):Gdiplus::Color(255,255,255,255),std::max(2.f,stroke/2));
                    if(o.map.layout==CircleLayout) graphics.DrawLine(&mark,w/2,h/2-r-stroke,w/2,h/2-r+stroke);
                    else for(float x:{inset,w-inset}) graphics.DrawLine(&mark,x,h/2-stroke*1.5f,x,h/2+stroke*1.5f);
                }
            } else {
                const auto transform=MapFit(trackMap.geometry,width,height,o.map);
                // Underneath: the surroundings, then the other configurations in a dim grey.
                if(o.map.surroundings) DrawMapLayer(graphics,trackMap.geometry.background,transform,RGB(34,40,48));
                if(o.map.allConfigs) DrawMapLayer(graphics,trackMap.geometry.inactive,transform,RGB(74,82,94));
                if(o.map.pitLane) DrawMapLayer(graphics,trackMap.geometry.pit,transform,Ink::Muted);
                // White with a black edge by default; start/finish keeps white and gets the same edge to stay visible on it.
                const float edge=o.map.whiteTrack?std::max(1.f,static_cast<float>(MulDiv(15,o.dpi*o.place.scale,9600))/10):0;
                DrawMapLayer(graphics,trackMap.geometry.active,transform,o.map.whiteTrack?Ink::White:RGB(110,120,134),edge);
                // The stripe is clear on the map; the arrow may lie outside its tight bounds.
                if(o.map.startFinish) DrawMapLayer(graphics,trackMap.geometry.finish,transform,Ink::White,edge);
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
        GdiFlush();
    }
    // The whole map in this pass: its rows straight into the buffer, and the cars and numbers after it too.
    RECT box{}; const bool whole=!dirty || (GetRgnBox(dirty,&box)==SIMPLEREGION && box.left<=c.r.left && box.top<=c.r.top && box.right>=c.r.right && box.bottom>=c.r.bottom);
    o.direct=o.pixels && o.mapPixels && whole;
    if(o.direct) {
        GdiFlush(); // GDI may still be drawing the background.
        const int left=std::max(0,static_cast<int>(-c.r.left)), right=std::min(width,o.width-static_cast<int>(c.r.left));
        for(int y=std::max(0,static_cast<int>(-c.r.top));y<height && c.r.top+y<o.height && right>left;++y)
            std::memcpy(o.pixels+(c.r.top+y)*o.width+c.r.left+left,o.mapPixels+y*width+left,(right-left)*sizeof(std::uint32_t));
        return;
    }
    HDC source=CreateCompatibleDC(o.dc); const auto old=SelectObject(source,o.mapBitmap);
    BitBlt(o.dc,c.r.left,c.r.top,width,height,source,0,0,SRCCOPY);
    SelectObject(source,old); DeleteDC(source);
}
// Premultiplied source over the buffer pixels: a few hundred pixels per car instead of an AlphaBlend call each.
void Blend(Overlay& o,const RECT& r,const std::uint32_t* source,int width,int height) {
    const int left=r.left,top=r.top;
    const int first=std::max(0,-left),last=std::min(width,o.width-left);
    const __m128i zero=_mm_setzero_si128(),opaque=_mm_set1_epi32(255),full=_mm_set1_epi16(255),half=_mm_set1_epi16(128);
    // Each 16-bit channel times the pixel's 255 - alpha, divided by 255 the same way as below.
    auto scale=[&](__m128i channels,__m128i alpha){ __m128i v=_mm_add_epi16(_mm_mullo_epi16(channels,_mm_sub_epi16(full,alpha)),half); return _mm_srli_epi16(_mm_add_epi16(v,_mm_srli_epi16(v,8)),8); };
    for(int y=std::max(0,-top);y<height && top+y<o.height;++y) {
        const std::uint32_t* from=source+y*width; std::uint32_t* to=o.pixels+(top+y)*o.width+left;
        int x=first;
        // Four pixels at a time (SSE2, on every x64 processor): the same results as one at a time.
        for(;x+4<=last;x+=4) {
            const __m128i s=_mm_loadu_si128(reinterpret_cast<const __m128i*>(from+x)),alpha=_mm_srli_epi32(s,24);
            if(_mm_movemask_epi8(_mm_cmpeq_epi32(alpha,zero))==0xFFFF) continue;
            if(_mm_movemask_epi8(_mm_cmpeq_epi32(alpha,opaque))==0xFFFF) { _mm_storeu_si128(reinterpret_cast<__m128i*>(to+x),s); continue; }
            const __m128i d=_mm_loadu_si128(reinterpret_cast<const __m128i*>(to+x)),pair=_mm_or_si128(alpha,_mm_slli_epi32(alpha,16));
            const __m128i low=scale(_mm_unpacklo_epi8(d,zero),_mm_unpacklo_epi32(pair,pair)),high=scale(_mm_unpackhi_epi8(d,zero),_mm_unpackhi_epi32(pair,pair));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(to+x),_mm_add_epi8(s,_mm_packus_epi16(low,high)));
        }
        for(;x<last;++x) {
            const std::uint32_t s=from[x],alpha=s>>24;
            if(!alpha) continue;
            if(alpha==255) { to[x]=s; continue; }
            // Red with blue and alpha with green, two channels at a time; (x+128+((x+128)>>8))>>8 is x/255 rounded.
            const std::uint32_t d=to[x],keep=255-alpha;
            std::uint32_t rb=(d&0x00FF00FFu)*keep+0x00800080u; rb=((rb+((rb>>8)&0x00FF00FFu))>>8)&0x00FF00FFu;
            std::uint32_t ag=((d>>8)&0x00FF00FFu)*keep+0x00800080u; ag=((ag+((ag>>8)&0x00FF00FFu))>>8)&0x00FF00FFu;
            // Alpha blends too, so a transparent buffer gets the coverage of each edge.
            to[x]=s+(rb|ag<<8);
        }
    }
}
// Into the pixels in a whole-map pass, else through GDI with the pass's clipping.
void PutMapBitmap(Overlay& o,const RECT& r,HBITMAP bitmap,const std::uint32_t* pixels,int width,int height) {
    if(o.direct) { Blend(o,r,pixels,width,height); return; }
    HDC source=CreateCompatibleDC(o.dc); const auto old=SelectObject(source,bitmap);
    AlphaBlend(o.dc,r.left,r.top,width,height,source,0,0,width,height,{AC_SRC_OVER,0,255,AC_SRC_ALPHA});
    SelectObject(source,old); DeleteDC(source);
}
// A car number or PIT in the overlay's font, centred in its box.
void DrawMapText(Gdiplus::Graphics& graphics,const Overlay& o,const DrawCommand& c,int width,int height) {
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::Font font(o.dc,o.fonts[c.font]);
    // Typographic: no padding around the text, so three digits fit where two did.
    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic()); format.SetAlignment(Gdiplus::StringAlignmentCenter); format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap|Gdiplus::StringFormatFlagsNoClip);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255,GetRValue(c.color),GetGValue(c.color),GetBValue(c.color)));
    graphics.DrawString(c.text.c_str(),static_cast<int>(c.text.size()),&font,Gdiplus::RectF(0,0,static_cast<float>(width),static_cast<float>(height)),&format,&brush);
}
// Text without a marker under it, such as PIT; drawn once, then blended.
void PaintMapLabel(Overlay& o,const DrawCommand& c) {
    const int width=c.r.right-c.r.left,height=c.r.bottom-c.r.top;
    if(width<=0 || height<=0) return;
    const Overlay::MapText* text=nullptr;
    for(const auto& item:o.mapTexts) if(item.width==width && item.height==height && item.color==c.color && item.text==c.text) { text=&item; break; }
    if(!text) {
        BITMAPINFO info{}; info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB};
        void* bits=nullptr; HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!bitmap) return;
        {
            Gdiplus::Bitmap target(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));
            Gdiplus::Graphics graphics(&target); graphics.Clear(Gdiplus::Color(0,0,0,0));
            DrawMapText(graphics,o,c,width,height);
        }
        GdiFlush();
        // Numbers are few; a new size or colour set starts over.
        if(o.mapTexts.size()>=MaxCars*4) { for(const auto& item:o.mapTexts) DeleteObject(item.bitmap); o.mapTexts.clear(); }
        o.mapTexts.push_back({c.text,c.color,width,height,bitmap,static_cast<const std::uint32_t*>(bits)});
        text=&o.mapTexts.back();
    }
    PutMapBitmap(o,c.r,text->bitmap,text->bits,width,height);
}
// A marker, and the number on it when given: drawn together once, then blended as one.
void PaintMapCar(Overlay& o,const DrawCommand& c,const DrawCommand* label) {
    const int width=c.r.right-c.r.left,height=c.r.bottom-c.r.top,stroke=std::max(1,static_cast<int>(c.shape[0]));
    const bool hollow=c.shape[1]!=0; HBITMAP bitmap=nullptr; const std::uint32_t* pixels=nullptr;
    const COLORREF textColor=label?label->color:0;
#ifdef RACEMATE_DIAGNOSTICS
    const LONGLONG started=commandTiming?DiagnosticTicks():0; long long compared=0;
#endif
    for(auto& marker:o.mapMarkers) {
#ifdef RACEMATE_DIAGNOSTICS
        ++compared;
#endif
        if(marker.width==width && marker.height==height && marker.stroke==stroke && marker.color==c.color && marker.hollow==hollow
            && marker.textColor==textColor && (label?marker.text==label->text:marker.text.empty())) { bitmap=marker.bitmap; pixels=marker.bits; marker.used=o.mapPass; break; }
    }
#ifdef RACEMATE_DIAGNOSTICS
    const LONGLONG searched=commandTiming?DiagnosticTicks():0;
    if(commandTiming) { ++markerStats.calls; markerStats.compared+=compared; markerStats.lookup+=searched-started; }
#endif
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
            if(label) DrawMapText(graphics,o,*label,width,height);
        }
        // With numbers each car has its own; positions change hands, so the cache keeps what the last pass drew
        // and lets the rest go. Scale/DPI changes clear the entire cache in Overlay::Free().
        if(o.mapMarkers.size()>=MaxCars*4+4) {
            std::erase_if(o.mapMarkers,[&](const Overlay::MapMarker& marker){ const bool old=marker.used+1<o.mapPass; if(old) DeleteObject(marker.bitmap); return old; });
            if(o.mapMarkers.size()>=MaxCars*4+4) { for(const auto& marker:o.mapMarkers) DeleteObject(marker.bitmap); o.mapMarkers.clear(); }
        }
        GdiFlush(); pixels=static_cast<const std::uint32_t*>(bits);
        o.mapMarkers.push_back({width,height,stroke,c.color,hollow,label?label->text:std::wstring(),textColor,bitmap,pixels,o.mapPass});
#ifdef RACEMATE_DIAGNOSTICS
        if(commandTiming) { ++markerStats.created; markerStats.create+=DiagnosticTicks()-searched; }
#endif
    }
#ifdef RACEMATE_DIAGNOSTICS
    const LONGLONG blending=commandTiming?DiagnosticTicks():0;
    if(commandTiming) { markerStats.direct+=o.direct; markerStats.pixels+=static_cast<double>(width)*height; }
#endif
    PutMapBitmap(o,c.r,bitmap,pixels,width,height);
#ifdef RACEMATE_DIAGNOSTICS
    if(commandTiming) { GdiFlush(); markerStats.blend+=DiagnosticTicks()-blending; }
#endif
}
