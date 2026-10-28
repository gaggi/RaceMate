// Rasterize the actual road once, including filled SVG outlines and variable widths.
// Each covered pixel gets a split ID at layout changes; new times only recolour it.
HBITMAP SectorMapBitmap(Overlay& o,int width,int height,const std::uint32_t*& pixels) {
    const int sectors=SectorCount(),parts=o.map.sectorParts,count=sectors*parts;
    if(!o.map.sectors || !sectors || !o.mapPixels || (o.map.layout==MapLayout && !trackMap.geometry.direction)) return o.mapBitmap;
    const uint64_t layout=(app.preview?~uint64_t{0}-1:app.sectors.LayoutRevision())*4+parts;
    const float scene=SectorSceneRevision();
    const int style=o.map.sectorMarks*2+o.map.sectorLabels;
    if(o.sectorBitmap && !o.sectorPaths.empty() && o.sectorLayout==layout && o.sectorScene==scene && o.sectorStyle==style) { pixels=o.sectorPixels; return o.sectorBitmap; }
    const float scale=static_cast<float>(o.dpi*o.place.scale)/9600;
    const auto transform=o.map.layout==MapLayout?MapFit(trackMap.geometry,width,height,o.map):MapTransform{};
    const auto ring=MapShape(o,width,height);
    auto point=[&](double pct) { const auto p=o.map.layout==MapLayout?transform.Apply(trackMap.geometry.At(pct)):ring.At(pct); return Gdiplus::PointF(static_cast<float>(p.x),static_cast<float>(p.y)); };
    bool redraw=o.sectorStyle!=style; o.sectorStyle=style;
    if(o.sectorPaths.empty() || o.sectorLayout!=layout) {
        o.sectorPaths.clear(); o.sectorPaths.reserve(count); o.sectorColors.assign(count,CLR_INVALID); o.sectorLayout=layout;
        struct Edge { Gdiplus::PointF p; float dx,dy,length; uint16_t segment; };
        std::vector<Edge> edges;
        for(int s=0;s<sectors;++s) for(int part=0;part<parts;++part) {
            const double from=SectorStart(s,parts==3?part:0);
            const double to=part+1<parts?SectorStart(s,part+1):(s+1<sectors?SectorStart(s+1):1.);
            const int steps=std::max(2,static_cast<int>(std::ceil((to-from)*600)));
            std::vector<Gdiplus::PointF> path; path.reserve(steps+1);
            for(int i=0;i<=steps;++i) path.push_back(point(from+(to-from)*i/steps));
            for(int i=0;i<steps;++i) {
                const float dx=path[i+1].X-path[i].X,dy=path[i+1].Y-path[i].Y;
                edges.push_back({path[i],dx,dy,std::max(1e-10f,dx*dx+dy*dy),static_cast<uint16_t>(s*parts+part)});
            }
            o.sectorPaths.push_back(std::move(path));
        }
        std::vector<uint32_t> coverage(static_cast<size_t>(width)*height,0);
        {
            Gdiplus::Bitmap mask(width,height,width*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(coverage.data()));
            Gdiplus::Graphics g(&mask); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            if(o.map.layout==MapLayout) DrawMapLayer(g,trackMap.geometry.active,transform,RGB(255,255,255));
            else {
                Gdiplus::Pen road(Gdiplus::Color(255,255,255,255),static_cast<float>(std::max(2,MulDiv(6,o.dpi*o.place.scale,9600))));
                road.SetStartCap(Gdiplus::LineCapRound); road.SetEndCap(Gdiplus::LineCapRound);
                const float r=static_cast<float>(ring.Radius());
                if(o.map.layout==CircleLayout) g.DrawEllipse(&road,width/2.f-r,height/2.f-r,2*r,2*r);
                else g.DrawLine(&road,static_cast<float>(ring.inset),height/2.f,static_cast<float>(width-ring.inset),height/2.f);
            }
            g.Flush(Gdiplus::FlushIntentionSync);
        }
        o.sectorMask.clear();
        for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
            const auto alpha=static_cast<unsigned char>(coverage[y*width+x]>>24);
            if(!alpha) continue;
            float closest=std::numeric_limits<float>::max(); uint16_t split=0;
            for(const auto& e:edges) {
                const float dx=x+.5f-e.p.X,dy=y+.5f-e.p.Y,t=std::clamp((dx*e.dx+dy*e.dy)/e.length,0.f,1.f);
                const float distance=(dx-t*e.dx)*(dx-t*e.dx)+(dy-t*e.dy)*(dy-t*e.dy);
                if(distance<closest) { closest=distance; split=e.segment; }
            }
            o.sectorMask.push_back({static_cast<uint32_t>(y*width+x),split,alpha});
        }
        redraw=true;
    }
    if(redraw || o.sectorScene!=scene) {
        const int reference=ReferenceCar(app.frame);
        for(int i=0;i<count;++i) {
            const auto color=SectorPaceColor(reference,i/parts,parts==3?i%parts:-1);
            if(o.sectorColors[i]!=color) { o.sectorColors[i]=color; redraw=true; }
        }
        o.sectorScene=scene;
    }
    if(!o.sectorBitmap) {
        BITMAPINFO info{}; info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB}; void* bits=nullptr;
        o.sectorBitmap=CreateDIBSection(o.dc,&info,DIB_RGB_COLORS,&bits,nullptr,0); o.sectorPixels=static_cast<std::uint32_t*>(bits); redraw=true;
    }
    if(!o.sectorBitmap) return o.mapBitmap;
    if(redraw) {
        GdiFlush(); std::memcpy(o.sectorPixels,o.mapPixels,static_cast<size_t>(width)*height*4);
        for(const auto& p:o.sectorMask) {
            const auto color=o.sectorColors[p.segment]; const uint32_t a=p.alpha,d=o.sectorPixels[p.offset],keep=255-a;
            auto over=[&](uint32_t c,int shift) { return ((c*a+((d>>shift)&255)*keep+127)/255)<<shift; };
            o.sectorPixels[p.offset]=over(GetRValue(color),16)|over(GetGValue(color),8)|over(GetBValue(color),0)|((a+((d>>24)*keep+127)/255)<<24);
        }
        Gdiplus::Bitmap canvas(width,height,width*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(o.sectorPixels));
        Gdiplus::Graphics graphics(&canvas); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const auto dark=Gdiplus::Color(255,static_cast<BYTE>(Ink::Frame&255),static_cast<BYTE>((Ink::Frame>>8)&255),static_cast<BYTE>((Ink::Frame>>16)&255));
        auto normal=[&](Gdiplus::PointF p,Gdiplus::PointF q) {
            const float dx=q.X-p.X,dy=q.Y-p.Y,len=std::max(.001f,std::sqrt(dx*dx+dy*dy));
            return Gdiplus::PointF(-dy/len,dx/len);
        };
        auto reach=[&](Gdiplus::PointF p,Gdiplus::PointF n,float sign) {
            float last=3*scale;
            for(float d=0;d<40*scale;d+=1) {
                const int x=static_cast<int>(std::lround(p.X+n.X*d*sign)),y=static_cast<int>(std::lround(p.Y+n.Y*d*sign));
                if(x<0 || x>=width || y<0 || y>=height) break;
                const uint32_t offset=y*width+x;
                const auto at=std::lower_bound(o.sectorMask.begin(),o.sectorMask.end(),offset,[](const auto& a,uint32_t b){return a.offset<b;});
                if(at==o.sectorMask.end() || at->offset!=offset) { if(d>2) break; }
                else last=d;
            }
            return last+scale;
        };
        for(int i=0;i<count && o.map.sectorMarks;++i) {
            const bool official=i%parts==0;
            const auto& path=o.sectorPaths[i]; const auto p=path[0],n=normal(p,path[1]);
            const float a=reach(p,n,-1),b=reach(p,n,1);
            Gdiplus::Pen mark(dark,std::max(1.f,(official?2.f:1.f)*scale));
            graphics.DrawLine(&mark,p.X-n.X*a,p.Y-n.Y*a,p.X+n.X*b,p.Y+n.Y*b);
        }
        Gdiplus::FontFamily family(L"Segoe UI");
        Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
        Gdiplus::SolidBrush ink(Gdiplus::Color(255,232,237,242)); Gdiplus::Pen outline(dark,std::max(1.f,1.4f*scale));
        // Every road pixel, other parts of the circuit included: a label may touch none of them, nor another label.
        std::vector<unsigned char> road(static_cast<size_t>(width)*height,0);
        for(const auto& p:o.sectorMask) road[p.offset]=1;
        std::vector<Gdiplus::RectF> placed;
        auto free=[&](const Gdiplus::RectF& r) {
            if(r.X<0 || r.Y<0 || r.GetRight()>width || r.GetBottom()>height) return false;
            for(const auto& other:placed) if(other.IntersectsWith(r)) return false;
            for(int y=static_cast<int>(r.Y);y<static_cast<int>(std::ceil(r.GetBottom()));++y)
                for(int x=static_cast<int>(r.X);x<static_cast<int>(std::ceil(r.GetRight()));++x) if(road[static_cast<size_t>(y)*width+x]) return false;
            return true;
        };
        for(int s=0;s<std::min(sectors,12) && o.map.sectorLabels;++s) {
            const double start=SectorStart(s),end=s+1<sectors?SectorStart(s+1):1.;
            const float size=std::max(8.f,11.f*scale*o.map.labelScale/100),gap=3*scale;
            const auto text=L"S"+std::to_wstring(s+1); Gdiplus::GraphicsPath label;
            label.AddString(text.c_str(),static_cast<int>(text.size()),&family,Gdiplus::FontStyleBold,size,Gdiplus::PointF(0,0),&format);
            Gdiplus::RectF bounds; label.GetBounds(&bounds);
            const float half=std::sqrt(bounds.Width*bounds.Width+bounds.Height*bounds.Height)/2;
            // From the middle of the sector outwards along it, outside the circuit first, then inside, a little
            // further off each round: the first spot clear of every road and label wins.
            bool found=false; Gdiplus::PointF centre;
            for(float extra=0;extra<=24*scale && !found;extra+=4*scale)
                for(double along:{.5,.4,.6,.3,.7,.2,.8}) {
                    const double at=start+(end-start)*along;
                    const auto p=point(at),q=point(std::min(at+.001,1.)); auto n=normal(p,q);
                    if(n.X*(p.X-width/2.f)+n.Y*(p.Y-height/2.f)<0) { n.X=-n.X; n.Y=-n.Y; }
                    for(float side:{1.f,-1.f}) {
                        const Gdiplus::PointF d(n.X*side,n.Y*side);
                        const float offset=reach(p,d,1)+gap+half+extra;
                        const Gdiplus::PointF c(p.X+d.X*offset,p.Y+d.Y*offset);
                        const Gdiplus::RectF box(c.X-bounds.Width/2-gap/2,c.Y-bounds.Height/2-gap/2,bounds.Width+gap,bounds.Height+gap);
                        if(free(box)) { centre=c; found=true; placed.push_back(box); break; }
                    }
                    if(found) break;
                }
            if(!found) continue; // No free spot near this sector: no label rather than one on the road.
            Gdiplus::Matrix move; move.Translate(centre.X-bounds.X-bounds.Width/2,centre.Y-bounds.Y-bounds.Height/2); label.Transform(&move);
            if(o.map.labelContrast) graphics.DrawPath(&outline,&label);
            graphics.FillPath(&ink,&label);
        }
        if(o.map.layout==MapLayout) {
            if(o.map.startFinish) { auto onRoad=[&](int x,int y){ return road[static_cast<size_t>(y)*width+x]!=0; }; DrawStartFinish(graphics,o,transform,width,height,onRoad,onRoad,true,false); }
            DrawMapLabels(graphics,o,transform);
        }
        graphics.Flush(Gdiplus::FlushIntentionSync);
    }
    pixels=o.sectorPixels; return o.sectorBitmap;
}
