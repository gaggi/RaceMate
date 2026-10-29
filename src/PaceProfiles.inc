// Class laps kept between sessions as start references for the Relative and Traffic: one file per layout and
// class beside settings.ini. Only a start: every lap of the current session comes first, and the pace window
// scales a stored lap to how the car drives now. A file is checked against format, layout, track length and point
// count before use; it is rewritten only when the session sets a faster clean lap for that class.
struct PaceStore {
    std::string key;           // Session and layout the references were loaded for.
    std::map<int,double> laps; // Class ID: lap time on disk (or loaded).
    ULONGLONG checked=0;
} paceStore;
std::filesystem::path PaceProfileFile(const Session& s,int classId) {
    return std::filesystem::path(app.config).parent_path()/L"pace"/L"v1"/(std::to_wstring(s.trackId)+L"-"+std::to_wstring(classId)+L".txt");
}
std::vector<int> SessionClasses(const Session& s) {
    std::vector<int> classes;
    for(const auto& d:s.drivers) if(!d.paceCar && !d.spectator && std::find(classes.begin(),classes.end(),d.classId)==classes.end()) classes.push_back(d.classId);
    return classes;
}
constexpr const char* PaceFormat="RaceMate pace profile 1";
// Written to a temporary file first, so a crash never leaves half a profile.
bool WritePaceFile(const std::filesystem::path& file,const Session& s,double lap,const std::vector<double>& times,const std::string& car,int wetness) {
    std::error_code error; std::filesystem::create_directories(file.parent_path(),error);
    auto temporary=file; temporary+=L".tmp";
    {
        std::ofstream out(temporary,std::ios::trunc); out.precision(9);
        out<<PaceFormat<<'\n'<<s.trackName<<" / "<<s.trackConfig<<'\n'<<s.trackId<<' '<<s.trackMeters<<' '<<times.size()<<' '<<lap<<' '
           <<wetness<<' '<<static_cast<long long>(std::time(nullptr))<<' '<<(car.empty()?"-":car)<<'\n';
        for(double t:times) out<<t<<'\n';
        if(!out) return false;
    }
    std::filesystem::rename(temporary,file,error);
    return !error;
}
// Another format, layout, length or resolution: not this track as it is now.
bool ReadPaceFile(const std::filesystem::path& file,const Session& s,int points,double& lap,std::vector<double>& times) {
    std::ifstream in(file); if(!in) return false;
    std::string format,layout; std::getline(in,format); std::getline(in,layout);
    int track=-1,count=0,wetness=-1; double metres=0; long long saved=0; std::string car;
    in>>track>>metres>>count>>lap>>wetness>>saved; std::getline(in,car); // The car to the end of the line: car paths may hold spaces.
    if(!in || format!=PaceFormat || layout!=s.trackName+" / "+s.trackConfig || track!=s.trackId || std::abs(metres-s.trackMeters)>s.trackMeters*.005 || count!=points) return false;
    times.assign(count,0); for(auto& t:times) in>>t;
    return static_cast<bool>(in);
}
void LoadPaceProfiles(const Session& s) {
    const int points=app.passing.Points();
    if(app.diagnostics || s.trackId<0 || !(s.trackMeters>0) || !points) return;
    const std::string key=app.passing.SessionKey()+"|"+std::to_string(s.trackId);
    if(key==paceStore.key) return;
    paceStore=PaceStore{key};
    for(int classId:SessionClasses(s)) {
        double lap=0; std::vector<double> times;
        if(ReadPaceFile(PaceProfileFile(s,classId),s,points,lap,times) && app.passing.AddReference(classId,lap,std::move(times))) paceStore.laps[classId]=lap;
    }
}
void SavePaceProfiles(const Session& s) {
    if(app.diagnostics || s.trackId<0 || paceStore.key.empty()) return;
    const ULONGLONG now=GetTickCount64(); if(now-paceStore.checked<5000) return; paceStore.checked=now;
    for(int classId:SessionClasses(s)) {
        double lap=0; std::vector<double> times;
        if(!app.passing.ClassLap(classId,lap,times)) continue;
        const auto known=paceStore.laps.find(classId);
        if(known!=paceStore.laps.end() && !(lap<known->second*.999)) continue;
        std::string car; const int fastest=app.passing.ClassProfile(classId); for(const auto& d:s.drivers) if(d.index==fastest) car=d.carPath;
        if(WritePaceFile(PaceProfileFile(s,classId),s,lap,times,car,app.frame.trackWetness)) paceStore.laps[classId]=lap;
    }
}
