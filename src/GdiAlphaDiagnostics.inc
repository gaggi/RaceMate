// Independent full-image oracle across changing scene, size, DPI, opacity and corners.
int RunAlphaRegression() {
    std::ofstream report("build/qa/gdi-alpha-regression.txt");
    app.preview=true; app.edit=false; app.hidden=false; app.diagnostics=true;
    const std::atomic_bool cancel=false;
    auto loaded=trackmap::Load(*trackmap::FindSource(353),L"build/qa/renderer-track-cache",cancel);
    if(!loaded.map) { report<<"map unavailable: FAIL\n"; return 2; }
    trackMap.geometry=std::move(loaded.map.geometry); trackMap.ready=trackMap.preview=true; ++trackMap.revision;
    unsigned images=0; size_t mismatches=0;
    for(int kind:{RelativeKind,StandingsKind,TrackMapKind}) {
        auto& o=First(kind); o.place.backgroundOnly=true; o.place.spout=false; o.map={};
        for(bool background:{true,false}) {
        if(kind!=TrackMapKind && !background) continue; o.map.background=background;
        for(bool rounded:{false,true}) for(int scale:{70,130}) for(int dpi:{96,144}) for(int opacity:{40,70,100}) {
            o.place.rounded=rounded; o.place.scale=scale; o.place.opacity=opacity;
            o.Free();
            for(int frame=0;frame<20;++frame) {
                PreviewData(3+frame*.17); app.now=100000+frame*170; InvalidateModel();
                const int width=MulDiv(OverlayWidth(o),dpi*scale,9600),height=MulDiv(BaseHeight(o),dpi*scale,9600);
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
                mismatches+=wrong; ++images;
                if(wrong) report<<"kind="<<kind<<" rounded="<<rounded<<" scale="<<scale<<" dpi="<<dpi<<" opacity="<<opacity<<" frame="<<frame<<" pixels="<<wrong<<'\n';
            }
        }
        }
        o.Free();
    }
    report<<"images="<<images<<", mismatched pixels="<<mismatches<<": "<<(mismatches?"FAIL":"PASS")<<'\n';
    return mismatches?2:0;
}
