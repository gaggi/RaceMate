// The composition as it was before it skipped the areas without foreground and outline: the oracle the faster one
// must match pixel for pixel.
bool PaintBackgroundOpacityReference(Overlay& o,HRGN& dirty) {
    if(!o.dibPixels || !o.blackMatte.Ensure(o.width,o.height) || !o.whiteMatte.Ensure(o.width,o.height)) return false;
    GrowForOutline(o,dirty);
    struct { void Stop() {} } passes;
    auto full=std::move(o.scene);
    o.scene.clear();
    for(const auto& c:full) if(c.background) o.scene.push_back(c);
    const size_t count=static_cast<size_t>(o.width)*o.height;
    std::vector<RECT> rectangles;
    if(dirty) {
        std::vector<BYTE> data(GetRegionData(dirty,0,nullptr));
        if(!data.empty() && GetRegionData(dirty,static_cast<DWORD>(data.size()),reinterpret_cast<RGNDATA*>(data.data()))) {
            const auto* region=reinterpret_cast<const RGNDATA*>(data.data()); const auto* rects=reinterpret_cast<const RECT*>(region->Buffer);
            rectangles.assign(rects,rects+region->rdh.nCount);
        }
    } else rectangles.push_back({0,0,o.width,o.height});
    auto eachPixel=[&](auto action) {
        if(!dirty) { for(size_t i=0;i<count;++i) action(i); return; }
        for(const auto& r:rectangles) for(int y=std::max(0L,r.top);y<std::min(static_cast<LONG>(o.height),r.bottom);++y) for(int x=std::max(0L,r.left);x<std::min(static_cast<LONG>(o.width),r.right);++x) action(static_cast<size_t>(y)*o.width+x);
    };
    if(dirty) eachPixel([&](size_t i){o.dibPixels[i]=0;}); else std::fill(o.dibPixels,o.dibPixels+count,0);
    PaintScene(o,dirty); GdiFlush();
    // Text is left to its own ClearType pass below; the mattes take the other foreground.
    o.scene.clear(); for(const auto& c:full) if(!c.background && c.type!=DrawCommand::Text) o.scene.push_back(c);
    const auto dc=o.dc; auto* pixels=o.pixels; auto* dib=o.dibPixels;
    o.foregroundMatte=true; o.pixels=nullptr;
    for(auto* matte:{&o.blackMatte,&o.whiteMatte}) {
        const bool white=matte==&o.whiteMatte;
        if(dirty) eachPixel([&](size_t i){matte->pixels[i]=white?0xffffffffu:0xff000000u;}); else std::fill(matte->pixels,matte->pixels+count,white?0xffffffffu:0xff000000u);
        o.dc=matte->dc; o.dibPixels=matte->pixels;
        PaintScene(o,dirty); GdiFlush();
    }
    o.dc=dc; o.pixels=pixels; o.dibPixels=dib; o.foregroundMatte=false;
    passes.Stop();
    struct { void Stop() {} } compose;
    const bool transparent=TransparentContent(o);
    const unsigned baseAlpha=transparent?(app.edit?64u:0u):255u;
    const unsigned backgroundAlpha=baseAlpha*o.place.opacity/100;
    // The composed image goes into the black matte; the same scene opaque, as the text will be drawn on, into the
    // white one and the buffer.
    eachPixel([&](size_t i) {
        const auto black=o.blackMatte.pixels[i],white=o.whiteMatte.pixels[i],back=dib[i];
        unsigned foregroundAlpha=0;
        for(unsigned shift:{0u,8u,16u}) foregroundAlpha=std::max(foregroundAlpha,255u-static_cast<unsigned>(std::clamp(static_cast<int>((white>>shift)&255)-static_cast<int>((black>>shift)&255),0,255)));
        const unsigned alpha=foregroundAlpha+(backgroundAlpha*(255-foregroundAlpha)+127)/255;
        std::uint32_t result=alpha<<24, opaque=0;
        for(unsigned shift:{0u,8u,16u}) {
            unsigned b=(back>>shift)&255;
            if(transparent && baseAlpha) b=std::min(255u,(b*255+baseAlpha/2)/baseAlpha);
            const unsigned ink=(black>>shift)&255;
            result|=std::min(255u,ink+(b*backgroundAlpha*(255-foregroundAlpha)+32512)/65025)<<shift;
            opaque|=std::min(255u,ink+(b*(255-foregroundAlpha)+127)/255)<<shift;
        }
        o.blackMatte.pixels[i]=result; o.whiteMatte.pixels[i]=dib[i]=opaque;
    });
    // ClearType text over the opaque scene, as without background opacity. Each channel's share of the text color
    // there is its coverage; laid with it over the composed image, the text keeps its ClearType weight and stays
    // opaque, while the faded background shows between its strokes. The coverage and text color wait in textCover
    // and the white matte, for the outline around the strokes.
    compose.Stop();
    if(o.textCover.size()!=count) o.textCover.assign(count,0);
    o.scene.clear(); for(const auto& c:full) if(c.type==DrawCommand::Text && !c.background) o.scene.push_back(c);
    if(!o.scene.empty()) {
        
        PaintScene(o,dirty); GdiFlush();
        for(const auto& c:o.scene) {
            const COLORREF color=ForegroundInk(o,c.color);
            const int ink[3]={GetBValue(color),GetGValue(color),GetRValue(color)};
            for(const auto& area:rectangles) {
                RECT r{}; if(!IntersectRect(&r,&area,&c.r)) continue;
                for(LONG y=std::max(0L,r.top);y<std::min(static_cast<LONG>(o.height),r.bottom);++y) for(LONG x=std::max(0L,r.left);x<std::min(static_cast<LONG>(o.width),r.right);++x) {
                    const size_t i=static_cast<size_t>(y)*o.width+x;
                    const auto drawn=dib[i],under=o.whiteMatte.pixels[i];
                    if(o.textCover[i] || ((drawn^under)&0xffffffu)==0) continue;
                    // One coverage for the pixel, the mean of the channels': per channel, ClearType's color fringes
                    // would need an alpha of their own, and the channels short of the pixel's alpha turn dark at
                    // the stroke edges. A channel where text and ground hardly differ only counts when none differ more.
                    int sum=0,channels=0,steepest=0,widest=0;
                    for(int k=0;k<3;++k) {
                        const int span=ink[k]-static_cast<int>((under>>(8*k))&255);
                        const int cover=span?std::clamp((static_cast<int>((drawn>>(8*k))&255)-static_cast<int>((under>>(8*k))&255))*255/span,0,255):0;
                        if(std::abs(span)>widest) { widest=std::abs(span); steepest=cover; }
                        if(std::abs(span)>=24) { sum+=cover; ++channels; }
                    }
                    if(!widest) continue;
                    o.textCover[i]=static_cast<std::uint8_t>(channels?(sum+channels/2)/channels:steepest);
                    o.whiteMatte.pixels[i]=color;
                }
            }
        }
    }
    o.scene=std::move(full);
    // Around the strokes, a dark outline in the panel's color, only where the faded background shows: the strokes
    // keep their weight, and light text stays readable over a light game. It fades in as the opacity goes down.
    const unsigned outline=static_cast<unsigned>(std::clamp((100-o.place.opacity)*153/60,0,153));
    const auto coverAt=[&](LONG x,LONG y)->unsigned { return x<0 || y<0 || x>=o.width || y>=o.height?0u:o.textCover[static_cast<size_t>(y)*o.width+x]; };
    for(const auto& r:rectangles) for(LONG y=std::max(0L,r.top);y<std::min(static_cast<LONG>(o.height),r.bottom);++y) for(LONG x=std::max(0L,r.left);x<std::min(static_cast<LONG>(o.width),r.right);++x) {
        const size_t i=static_cast<size_t>(y)*o.width+x;
        std::uint32_t composed=o.blackMatte.pixels[i];
        if(outline && (composed>>24)<255) {
            const unsigned edge=std::max({coverAt(x-1,y),coverAt(x+1,y),coverAt(x,y-1),coverAt(x,y+1)});
            const unsigned corner=std::max({coverAt(x-1,y-1),coverAt(x+1,y-1),coverAt(x-1,y+1),coverAt(x+1,y+1)})*181/255;
            if(const unsigned shade=std::max(edge,corner)*outline/255) {
                std::uint32_t shaded=(shade+(((composed>>24)*(255-shade))+127)/255)<<24;
                constexpr unsigned panel=Ink::Panel, ground[3]={(panel>>16)&255,(panel>>8)&255,panel&255};
                for(int k=0;k<3;++k) shaded|=((shade*ground[k]+(255-shade)*((composed>>(8*k))&255)+127)/255)<<(8*k);
                composed=shaded;
            }
        }
        if(const unsigned cover=o.textCover[i]) {
            const COLORREF color=o.whiteMatte.pixels[i]; const unsigned ink[3]={GetBValue(color),GetGValue(color),GetRValue(color)};
            const unsigned alpha=cover+(((composed>>24)*(255-cover))+127)/255;
            std::uint32_t result=alpha<<24;
            for(int k=0;k<3;++k) result|=std::min(alpha,(cover*ink[k]+(255-cover)*((composed>>(8*k))&255)+127)/255)<<(8*k);
            composed=result;
        }
        dib[i]=composed;
    }
    eachPixel([&](size_t i){ o.textCover[i]=0; });
    o.backgroundComposed=true; return true;
}
// Independent full-image oracle across changing scene, size, DPI, opacity and corners: the partial composition, the
// whole one and the reference composition must agree pixel for pixel.
int RunAlphaRegression() {
    std::ofstream report("build/qa/gdi-alpha-regression.txt");
    app.preview=true; app.edit=false; app.hidden=false; app.diagnostics=true;
    const std::atomic_bool cancel=false;
    auto loaded=trackmap::Load(*trackmap::FindSource(353),L"build/qa/renderer-track-cache",cancel);
    if(!loaded.map) { report<<"map unavailable: FAIL\n"; return 2; }
    trackMap.geometry=std::move(loaded.map.geometry); trackMap.ready=trackMap.preview=true; ++trackMap.revision;
    // UnionOf builds a region from overlapping rectangles in one call: the same as combining them one by one.
    unsigned unions=0,unequalUnions=0; std::uint32_t seed=12345;
    auto random=[&](int range){ seed=seed*1664525u+1013904223u; return static_cast<int>((seed>>8)%static_cast<unsigned>(range)); };
    for(int round=0;round<500;++round) {
        std::vector<RECT> rects(1+random(300));
        for(RECT& r:rects) { const int x=random(400)-20,y=random(400)-20; r={x,y,x+random(40),y+random(40)}; }
        HRGN fast=UnionOf(rects),slow=CreateRectRgn(0,0,0,0);
        for(const RECT& r:rects) { HRGN part=CreateRectRgnIndirect(&r); CombineRgn(slow,slow,part,RGN_OR); DeleteObject(part); }
        unequalUnions+=!fast || !EqualRgn(fast,slow); ++unions; if(fast) DeleteObject(fast); DeleteObject(slow);
    }
    report<<"region unions="<<unions<<", unequal="<<unequalUnions<<": "<<(unequalUnions?"FAIL":"PASS")<<'\n';
    unsigned images=0; size_t mismatches=0,referenceMismatches=0;
    for(int kind=0;kind<OverlayCount;++kind) for(int layout:{MapLayout,CircleLayout}) {
        if(kind!=TrackMapKind && layout!=MapLayout) continue;
        auto& o=First(kind); o.place.backgroundOnly=true; o.place.spout=false; o.map={}; o.map.layout=layout;
        for(bool background:{true,false}) {
        if(kind!=TrackMapKind && !background) continue; o.map.background=background;
        for(bool rounded:{false,true}) for(int scale:{70,130}) for(int dpi:{96,144}) for(int opacity:{40,70,100}) {
            o.place.rounded=rounded; o.place.scale=scale; o.place.opacity=opacity;
            o.Free();
            for(int frame=0;frame<20;++frame) {
                PreviewData(3+frame*.17); app.now=100000+frame*170; InvalidateModel();
                const int width=MulDiv(OverlayWidth(o),dpi*scale,9600),height=MulDiv(BaseHeight(o),dpi*scale,9600);
                if(width<=0 || height<=0) break;
                const bool resized=EnsureBuffer(o,width,height,dpi);
                std::vector<DrawCommand> scene; BuildScene(o,width,height,dpi,scene);
                HRGN changed=nullptr;
                if(!resized && scene.size()==o.scene.size()) {
                    changed=CreateRectRgn(0,0,0,0);
                    for(size_t i=0;i<scene.size();++i) if(!(scene[i]==o.scene[i])) for(const RECT* r:{&scene[i].r,&o.scene[i].r}) {
                        HRGN part=CreateRectRgnIndirect(r); CombineRgn(changed,changed,part,RGN_OR); DeleteObject(part);
                    }
                }
                o.scene=std::move(scene); PaintBackgroundOpacity(o,changed); if(changed) DeleteObject(changed);
                std::vector<std::uint32_t> partial(o.dibPixels,o.dibPixels+static_cast<size_t>(width)*height);
                PaintBackgroundOpacity(o);
                size_t wrong=0; for(size_t i=0;i<partial.size();++i) wrong+=partial[i]!=o.dibPixels[i];
                HRGN whole=nullptr; PaintBackgroundOpacityReference(o,whole);
                size_t off=0; for(size_t i=0;i<partial.size();++i) off+=partial[i]!=o.dibPixels[i];
                mismatches+=wrong; referenceMismatches+=off; ++images;
                if(wrong || off) report<<"kind="<<kind<<" layout="<<layout<<" background="<<background<<" rounded="<<rounded<<" scale="<<scale<<" dpi="<<dpi<<" opacity="<<opacity<<" frame="<<frame<<" partial/whole pixels="<<wrong<<" reference pixels="<<off<<'\n';
            }
        }
        }
        o.Free();
    }
    report<<"images="<<images<<", partial/whole mismatched pixels="<<mismatches<<", reference mismatched pixels="<<referenceMismatches<<": "<<(mismatches || referenceMismatches?"FAIL":"PASS")<<'\n';
    return mismatches || referenceMismatches || unequalUnions?2:0;
}
