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
std::wstring FooterValue(int metric,int kind=-1) {
    if(metric==17) return kind>=0 && kind<2?Titles[kind]:L"—";
    if(metric==0) return {};
    if(metric==11) { SYSTEMTIME time{}; GetLocalTime(&time); wchar_t text[20]; swprintf_s(text,L"%02d:%02d",time.wHour,time.wMinute); return text; }
    const auto& f=app.frame; const auto& s=app.session;
    if(!f.connected) return L"—";
    const int reference=ReferenceCar(f);
    const auto driver=std::find_if(s.drivers.begin(),s.drivers.end(),[&](const Driver& d){return d.index==reference;});
    const Car* car=reference>=0 && reference<MaxCars?&f.cars[reference]:nullptr;
    const int current=reference==f.player && f.lapCompleted>=0?f.lapCompleted+1:(car?car->lap:-1);
    switch(metric) {
    case 1: return s.track.empty()?L"—":Wide(s.track);
    case 2: {
        const int incidents=reference==f.player && f.incidentsKnown?f.incidents:(driver!=s.drivers.end()?driver->incidentCount:-1);
        return incidents>=0?std::to_wstring(incidents)+(s.type=="Race" && s.incidentLimit>0?L"/"+std::to_wstring(s.incidentLimit):L"")+L"x":L"—";
    }
    case 3: return L"Track "+FooterTemperature(f.trackTemperature);
    case 4: return L"Air "+FooterTemperature(f.airTemperature);
    case 5: return L"Air "+FooterTemperature(f.airTemperature)+L" · Track "+FooterTemperature(f.trackTemperature);
    case 6: return L"Left "+FooterDuration(f.timeLeftKnown?f.timeLeft:-1);
    case 7: return L"Elapsed "+FooterDuration(f.time);
    case 8: return L"Lap "+(current>0?std::to_wstring(current):L"—");
    case 9: case 10: {
        double distance=Plan().lapsToFinish;
        double progress=f.lapCompleted>=0?f.lapCompleted+f.lapPct:-1;
        if(reference!=f.player) {
            Frame watched=f; watched.player=reference; watched.lapCompleted=car?(car->completed>=0?car->completed:(car->lap>0?car->lap-1:-1)):-1; watched.lapPct=car?car->pct:0; watched.lastLap=car?car->last:-1; watched.bestLap=car?car->best:-1;
            const auto forecast=app.predictor.Predict(s,watched,app.predictionOptions);
            distance=forecast.noStop.valid?forecast.noStop.ownDistance:-1;
            progress=car && watched.lapCompleted>=0 && car->pct>=0?watched.lapCompleted+car->pct:-1;
        }
        if(metric==10) return distance>=0?L"~"+Format(distance)+L" laps":L"—";
        const int finish=s.totalLaps>0 && s.type!="Race"?s.totalLaps:(distance>=0 && progress>=0?static_cast<int>(std::ceil(progress+distance-1e-6)):-1);
        return L"Lap "+(current>0?std::to_wstring(current):L"—")+L" / "+(finish>=0?L"~"+std::to_wstring(std::max(current,finish)):L"—");
    }
    case 12: return L"Sim "+FooterDuration(f.timeOfDay,true);
    case 13: return s.type.empty()?L"—":Wide(s.type);
    case 14: { const wchar_t* labels[]={L"—",L"Dry",L"Mostly Dry",L"Damp",L"Lightly Wet",L"Wet",L"Very Wet",L"Extremely Wet"}; return f.trackWetness>=0 && f.trackWetness<8?labels[f.trackWetness]:L"—"; }
    case 15: return L"Wind "+(f.windSpeed>=0?Format(f.windSpeed*3.6,0)+L" km/h":L"—");
    case 16: {
        if(driver==s.drivers.end()) return L"SOF —";
        double sum=0; int count=0; const double scale=1600/std::log(2.0);
        for(const auto& d:s.drivers) if(d.classId==driver->classId && !d.paceCar && !d.spectator && d.rating>0) { sum+=std::exp(-d.rating/scale); ++count; }
        return count && sum>0?L"SOF "+std::to_wstring(static_cast<int>(std::lround(scale*std::log(count/sum)))):L"SOF —";
    }
    }
    return L"—";
}
