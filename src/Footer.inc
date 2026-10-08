// Footer values are also used as repaint keys: unchanged displayed values do not repaint.
std::wstring FooterDuration(double seconds,bool clock=false) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>=604800) return L"—";
    const int value=static_cast<int>(seconds); wchar_t text[40];
    if(clock) swprintf_s(text,L"%02d:%02d",value/3600%24,value/60%60);
    else if(value>=3600) swprintf_s(text,L"%d:%02d:%02d",value/3600,value/60%60,value%60);
    else swprintf_s(text,L"%d:%02d",value/60,value%60);
    return text;
}
std::wstring FooterTemperature(double value) {
    if(!std::isfinite(value)) return L"—";
    wchar_t text[40]; swprintf_s(text,L"%.0f°C",value); return text;
}
// Strength of field with iRacing's exponential formula; cars receives the class entrants. -1 without ratings.
int ClassSof(int classId,int& cars) {
    double sum=0; int rated=0; const double scale=1600/std::log(2.0); cars=0;
    for(const auto& d:app.session.drivers) if(d.classId==classId && !d.paceCar && !d.spectator) {
        ++cars; if(d.rating>0) { sum+=std::exp(-d.rating/scale); ++rated; }
    }
    return rated && sum>0?static_cast<int>(std::lround(scale*std::log(rated/sum))):-1;
}
std::wstring FooterValue(int metric,int kind=-1) {
    if(metric==OverlayNameMetric) return kind>=0 && IsTable(kind)?Titles[kind]:L"—";
    if(metric==NoMetric) return {};
    if(metric==LocalTimeMetric) { SYSTEMTIME time{}; GetLocalTime(&time); wchar_t text[20]; swprintf_s(text,L"%02d:%02d",time.wHour,time.wMinute); return text; }
    const auto& f=app.frame; const auto& s=app.session;
    if(!f.connected) return L"—";
    const int reference=ReferenceCar(f);
    const auto driver=std::find_if(s.drivers.begin(),s.drivers.end(),[&](const Driver& d){return d.index==reference;});
    const Car* car=reference>=0 && reference<MaxCars?&f.cars[reference]:nullptr;
    const int current=reference==f.player && f.lapCompleted>=0?f.lapCompleted+1:(car?car->lap:-1);
    switch(metric) {
    case TrackMetric: return s.track.empty()?L"—":Wide(s.track);
    case IncidentsMetric: {
        const int incidents=reference==f.player && f.incidentsKnown?f.incidents:(driver!=s.drivers.end()?driver->incidentCount:-1);
        return incidents>=0?std::to_wstring(incidents)+(s.type=="Race" && s.incidentLimit>0?L"/"+std::to_wstring(s.incidentLimit):L"")+L"x":L"—";
    }
    case TrackTempMetric: return L"Track "+FooterTemperature(f.trackTemperature);
    case AirTempMetric: return L"Air "+FooterTemperature(f.airTemperature);
    case TemperaturesMetric: return L"Air "+FooterTemperature(f.airTemperature)+L" · Track "+FooterTemperature(f.trackTemperature);
    case TimeRemainingMetric: return L"Left "+FooterDuration(f.timeLeftKnown?f.timeLeft:-1);
    case TimeElapsedMetric: return L"Elapsed "+FooterDuration(f.time);
    case CurrentLapMetric: return L"Lap "+(current>0?std::to_wstring(current):L"—");
    case FinalLapMetric: case LapsRemainingMetric: {
        double distance=Plan().lapsToFinish;
        double progress=f.lapCompleted>=0?f.lapCompleted+f.lapPct:-1;
        if(reference!=f.player) {
            Frame watched=f; watched.player=reference; watched.lapCompleted=car?(car->completed>=0?car->completed:(car->lap>0?car->lap-1:-1)):-1; watched.lapPct=car?car->pct:0; watched.lastLap=car?car->last:-1; watched.bestLap=car?car->best:-1;
            const auto forecast=app.predictor.Predict(s,watched);
            distance=forecast.finish.valid?forecast.finish.ownDistance:-1;
            progress=car && watched.lapCompleted>=0 && car->pct>=0?watched.lapCompleted+car->pct:-1;
        }
        if(metric==LapsRemainingMetric) return distance>=0?L"~"+Format(distance)+L" laps":L"—";
        const int finish=s.totalLaps>0 && s.type!="Race"?s.totalLaps:(distance>=0 && progress>=0?static_cast<int>(std::ceil(progress+distance-1e-6)):-1);
        return L"Lap "+(current>0?std::to_wstring(current):L"—")+L" / "+(finish>=0?L"~"+std::to_wstring(std::max(current,finish)):L"—");
    }
    case SimTimeMetric: return L"Sim "+FooterDuration(f.timeOfDay,true);
    case SessionTypeMetric: return s.type.empty()?L"—":Wide(s.type);
    case WetnessMetric: { const wchar_t* labels[]={L"—",L"Dry",L"Mostly Dry",L"Damp",L"Lightly Wet",L"Wet",L"Very Wet",L"Extremely Wet"}; return f.trackWetness>=0 && f.trackWetness<8?labels[f.trackWetness]:L"—"; }
    case WindMetric: return L"Wind "+(f.windSpeed>=0?Format(f.windSpeed*3.6,0)+L" km/h":L"—");
    case SofMetric: {
        if(driver==s.drivers.end()) return L"SOF —";
        int cars=0; const int sof=ClassSof(driver->classId,cars);
        return sof>0?L"SOF "+std::to_wstring(sof):L"SOF —";
    }
    }
    return L"—";
}
