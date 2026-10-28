// Shared view of the one telemetry-side timing model. No histories or per-frame allocation here.
int SectorCount() { return app.preview?3:app.sectors.Count(); }
double SectorStart(int sector,int part=0) {
    return app.preview?(sector+part/3.)/3.:app.sectors.Start(sector*3+part);
}
double SectorDuration(int car,int sector,int part=-1) {
    if(app.preview) {
        if(car<0) return -1;
        const double time=17.5+sector*.7+(car-3)*((sector%2)?-.12:.09);
        return part<0?time:time/3+(part-1)*.15*(car-3);
    }
    if(!app.frame.connected || View(app.frame)==ViewMode::Replay) return -1;
    return app.sectors.Latest(car,sector,part);
}
COLORREF SectorPaceColor(int car,int sector,int part=-1) {
    using Pace=SectorTiming::Pace;
    const auto status=app.preview?static_cast<Pace>(1+(sector+(part<0?0:part)+std::max(0,car))%3):
        (!app.frame.connected || View(app.frame)==ViewMode::Replay?Pace::Unknown:app.sectors.Status(car,sector,part));
    switch(status) {
    case Pace::PersonalBest:return Ink::Green;
    case Pace::SessionBest:return RGB(205,100,255);
    case Pace::Slower:return Ink::Yellow;
    default:return Ink::Muted;
    }
}
float SectorSceneRevision() {
    const int reference=ReferenceCar(app.frame);
    const std::array<uint64_t,7> key{app.sectors.LayoutRevision(),static_cast<uint64_t>(reference+1),app.sectors.CarRevision(reference),app.sectors.RecordRevision(),
        app.preview,app.frame.connected,View(app.frame)==ViewMode::Replay};
    static std::array<uint64_t,7> previous{}; static unsigned revision=0;
    if(key!=previous) { previous=key; ++revision; }
    return static_cast<float>(revision);
}
// Table columns: index i over SDK sectors (parts 1) or their mini-sectors (parts 3), titled S2 or S2.3.
std::wstring SectorColumnTitle(int i,int parts) {
    return parts==3?L"S"+std::to_wstring(i/3+1)+L"."+std::to_wstring(i%3+1):L"S"+std::to_wstring(i+1);
}
double SectorColumnTime(int car,int i,int parts) { return parts==3?SectorDuration(car,i/3,i%3):SectorDuration(car,i); }
COLORREF SectorColumnColor(int car,int i,int parts) { return parts==3?SectorPaceColor(car,i/3,i%3):SectorPaceColor(car,i); }
const std::wstring& SectorMapCaption() {
    static const std::wstring text=L"Green: PB · Purple: class best · Yellow: slower";
    return text;
}
