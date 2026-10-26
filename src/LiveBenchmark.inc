// --benchmark-live: the real app against RaceMateFakeIRacing (iRacing's shared memory and data-valid event at
// 60 Hz, 60 cars) for 30 s after a 3 s warm-up, at the production process priority. Reports CPU for the process
// and the main thread, ticks and telemetry frames per second, where the main thread spends its time, and for each
// overlay how evenly it refreshes and how old its data is when it does. Relative runs at 15 Hz and Track Map at
// 20 Hz, the uneven pair.
// CPU is counted in cycles (QueryProcessCycleTime): Windows' CPU times are sampled on its 15.6 ms clock, which
// misses short bursts that start right after a clock tick, as timer-driven work does.
struct RefreshLog { std::vector<double> at, age; };
std::array<RefreshLog,OverlayCount> liveLog;
bool liveLogging=false;
double QpcSeconds() { LARGE_INTEGER now{},frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency); return static_cast<double>(now.QuadPart)/frequency.QuadPart; }
ULONG64 ProcessCycles() { ULONG64 cycles=0; QueryProcessCycleTime(GetCurrentProcess(),&cycles); return cycles; }
ULONG64 MainThreadCycles() { ULONG64 cycles=0; QueryThreadCycleTime(GetCurrentThread(),&cycles); return cycles; }
// The processor's nominal clock, to put cycles into milliseconds; efficiency mode may run it slower.
double NominalMhz() {
    DWORD mhz=0,size=sizeof(mhz);
    RegGetValueW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",L"~MHz",RRF_RT_REG_DWORD,nullptr,&mhz,&size);
    return mhz?mhz:3000;
}
int RunLiveBenchmark() {
    std::ofstream report("build/qa/live-benchmark.txt");
    wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr,exe,MAX_PATH);
    const auto sim=std::filesystem::path(exe).parent_path()/L"RaceMateFakeIRacing.exe";
    // Names of its own: iRacing's service may already hold iRacing's (empty) shared memory.
    std::wstring command=L"\""+sim.wstring()+L"\" 40 Local\\RaceMate.Benchmark.Telemetry Local\\RaceMate.Benchmark.DataValid";
    ConnectBenchmarkTelemetry(L"Local\\RaceMate.Benchmark.Telemetry",L"Local\\RaceMate.Benchmark.DataValid");
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) { report<<"Could not start "<<sim.string()<<"\n"; return 2; }
    // As in production: lowest priority class and efficiency mode.
    SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS);
    PROCESS_POWER_THROTTLING_STATE throttling{PROCESS_POWER_THROTTLING_CURRENT_VERSION,PROCESS_POWER_THROTTLING_EXECUTION_SPEED,PROCESS_POWER_THROTTLING_EXECUTION_SPEED};
    SetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&throttling,sizeof(throttling));
    for(auto& o:app.overlays) {
        o.place.enabled=o.number==1;
        if(o.kind==RelativeKind) o.place.rate=15;
        if(o.kind==TrackMapKind) { o.place.rate=20; o.map.layout=CircleLayout; }
        Fit(o);
    }
    app.preview=false; app.hidden=false; app.edit=false;
    app.refreshHook=[](const Overlay& o) {
        if(!liveLogging || o.number!=1) return;
        const double now=QpcSeconds(); liveLog[o.kind].at.push_back(now); liveLog[o.kind].age.push_back(app.frame.connected?now-app.frame.time:-1);
    };
    profiler.on=true; profiler.autoWrite=false;
    Tick();
    auto pump=[](double seconds) {
        const double end=QpcSeconds()+seconds; MSG message{};
        while(QpcSeconds()<end) {
            MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    };
    pump(3);
    const bool connected=app.frame.connected;
    for(auto& log:liveLog) { log.at.clear(); log.age.clear(); }
    for(auto& e:profiler.entries) { e.ticks=0; e.calls=0; }
    const ULONG64 processBefore=ProcessCycles(), threadBefore=MainThreadCycles();
    const unsigned long long ticksBefore=app.ticks, framesBefore=app.framesProcessed;
    const double startTime=QpcSeconds(); liveLogging=true;
    pump(30);
    liveLogging=false;
    const double seconds=QpcSeconds()-startTime;
    const double mhz=NominalMhz();
    const double processMs=(ProcessCycles()-processBefore)/(mhz*1000)/seconds, threadMs=(MainThreadCycles()-threadBefore)/(mhz*1000)/seconds;
    char line[256];
    std::snprintf(line,sizeof(line),"RaceMate %s live benchmark, %.1f s against RaceMateFakeIRacing (60 Hz, 60 cars), connected: %s\n",RACEMATE_VERSION,seconds,connected?"yes":"no"); report<<line;
    std::snprintf(line,sizeof(line),"CPU in cycles at the nominal %.0f MHz: process %.2f ms/s (%.3f %% of one core); main thread %.2f ms/s, other threads %.2f ms/s\n",mhz,processMs,processMs/10,threadMs,processMs-threadMs); report<<line;
    std::snprintf(line,sizeof(line),"Main thread ticks: %.1f /s; telemetry frames processed: %.1f /s of 60\n\n",(app.ticks-ticksBefore)/seconds,(app.framesProcessed-framesBefore)/seconds); report<<line;
    report<<"Overlay           rate  refreshes/s  interval ms: wanted  mean   sd    min    max   within 4 ms   data age ms: mean   max\n";
    for(const auto& o:app.overlays) {
        if(o.number!=1 || !o.place.enabled) continue;
        const auto& log=liveLog[o.kind]; const int rate=RefreshRate(o); const double wanted=1000.0/rate;
        if(log.at.empty()) continue;
        std::vector<double> gaps; for(size_t i=1;i<log.at.size();++i) gaps.push_back((log.at[i]-log.at[i-1])*1000);
        double mean=0,sd=0,low=0,high=0; int close=0;
        if(!gaps.empty()) {
            for(double g:gaps) mean+=g; mean/=gaps.size();
            for(double g:gaps) { sd+=(g-mean)*(g-mean); if(std::abs(g-wanted)<=4) ++close; }
            sd=std::sqrt(sd/gaps.size()); low=*std::min_element(gaps.begin(),gaps.end()); high=*std::max_element(gaps.begin(),gaps.end());
        }
        double ageMean=0,ageMax=0; int ages=0; for(double a:log.age) if(a>=0) { ageMean+=a; ageMax=std::max(ageMax,a); ++ages; }
        if(ages) ageMean/=ages;
        char name[32]; WideCharToMultiByte(CP_UTF8,0,Titles[o.kind],-1,name,32,nullptr,nullptr);
        std::snprintf(line,sizeof(line),"%-16s %4d  %10.2f  %18.1f %6.1f %5.1f %6.1f %6.1f  %9.0f %%  %18.1f %6.1f\n",name,rate,log.at.size()/seconds,wanted,mean,sd,low,high,
            gaps.empty()?0.0:100.0*close/gaps.size(),ageMean*1000,ageMax*1000); report<<line;
    }
    // Wall time per part of the main thread's tick, from the --profile scopes.
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    report<<"\nMain thread, wall time per part:\n    ms/s     us/call   calls/s  part\n";
    for(const auto& e:profiler.entries) {
        std::snprintf(line,sizeof(line),"%8.3f  %10.1f  %8.1f  %s\n",e.ticks*1000.0/frequency.QuadPart/seconds,e.calls?e.ticks*1e6/frequency.QuadPart/e.calls:0,e.calls/seconds,e.name.c_str()); report<<line;
    }
    app.refreshHook=nullptr; profiler.on=false;
    WaitForSingleObject(process.hProcess,15000); TerminateProcess(process.hProcess,0);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return 0;
}
