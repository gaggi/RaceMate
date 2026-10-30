// Fixed-step visible-window workload, with a file handshake for external GPU counters.
int RunRendererBenchmark() {
    const std::wstring root=L"build/qa/renderer-"+std::to_wstring(GetCurrentProcessId());
    try {
        wchar_t option[32]{};
        auto read=[&](const wchar_t* key){ option[0]=0; GetEnvironmentVariableW(key,option,32); return option; };
        const bool background=*read(L"RACEMATE_BENCH_BACKGROUND_ONLY")!=0;
        const bool output=*read(L"RACEMATE_BENCH_SPOUT")!=0;
        read(L"RACEMATE_BENCH_SINGLE"); const int single=*option?_wtoi(option):-1;
        read(L"RACEMATE_BENCH_SECONDS"); const int seconds=*option?std::clamp(_wtoi(option),1,60):20;
        const bool verify=*read(L"RACEMATE_BENCH_VERIFY")!=0;
        read(L"RACEMATE_BENCH_OPACITY"); const int opacity=*option?std::clamp(_wtoi(option),40,100):90;
        std::array<size_t,OverlayCount> checkedImages{},mismatchedPixels{};
        app.preview=true; app.edit=false; app.hidden=false; app.diagnostics=true;
        for(auto& layout:app.layouts) layout.name=L"Benchmark "+std::to_wstring(GetCurrentProcessId());
        // Assets are loaded before warmup; every run uses the same official geometry.
        const std::atomic_bool cancel=false;
        auto loaded=trackmap::Load(*trackmap::FindSource(353),std::filesystem::path(L"build/qa/renderer-track-cache"),cancel);
        if(!loaded.map || loaded.map.geometry.direction==0) throw std::runtime_error("Benchmark needs the prepared Lime Rock SVG geometry");
        trackMap.geometry=std::move(loaded.map.geometry); trackMap.ready=trackMap.preview=true; trackMap.id=353; ++trackMap.revision;
        PreviewData(0); InvalidateModel();
        // A stable, visible backdrop avoids comparing different content underneath
        // the transparent overlays. It is constant in every configuration.
        WNDCLASSW backdropClass{}; backdropClass.hInstance=app.instance; backdropClass.lpszClassName=L"RaceMate.BenchmarkBackdrop";
        backdropClass.lpfnWndProc=[](HWND w,UINT message,WPARAM wp,LPARAM lp)->LRESULT {
            if(message==WM_NCHITTEST) return HTTRANSPARENT;
            if(message==WM_PAINT) {
                PAINTSTRUCT paint{}; const auto dc=BeginPaint(w,&paint); RECT client{}; GetClientRect(w,&client);
                HBRUSH brushes[]={CreateSolidBrush(RGB(70,81,92)),CreateSolidBrush(RGB(48,56,64))};
                for(int y=0;y<client.bottom;y+=24) for(int x=0;x<client.right;x+=24) { RECT cell{x,y,x+24,y+24}; FillRect(dc,&cell,brushes[(x/24+y/24)%2]); }
                DeleteObject(brushes[0]); DeleteObject(brushes[1]); EndPaint(w,&paint); return 0;
            }
            return DefWindowProcW(w,message,wp,lp);
        };
        RegisterClassW(&backdropClass);
        struct Backdrop { HWND window=nullptr; ~Backdrop(){if(window) DestroyWindow(window);} } backdrop;
        backdrop.window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,backdropClass.lpszClassName,L"Renderer benchmark background",WS_POPUP,20,100,1340,385,nullptr,nullptr,app.instance,nullptr);
        ShowWindow(backdrop.window,SW_SHOWNOACTIVATE);
        for(auto& o:app.overlays) {
            const bool tested=o.number==1 && (o.kind==RelativeKind || o.kind==StandingsKind || o.kind==TrackMapKind) && (single<0 || o.kind==single);
            o.place={}; o.place.spoutPremultiplied=spoutPremultiplied; o.place.enabled=tested; o.place.spout=tested && output; o.place.backgroundOnly=background;
            o.place.opacity=opacity; o.place.rate=DefaultRates[o.kind]; o.place.rounded=true;
            o.place.x=o.kind==RelativeKind?40:(o.kind==StandingsKind?400:900); o.place.y=120; o.map={};
            if(tested) { Fit(o); ShowWindow(o.window,SW_SHOWNOACTIVATE); } else ShowWindow(o.window,SW_HIDE);
        }
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        auto ticks=[](){ LARGE_INTEGER v{}; QueryPerformanceCounter(&v); return v.QuadPart; };
        struct Timer { HANDLE value=nullptr; ~Timer(){if(value) CloseHandle(value);} } timer;
        timer.value=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        if(!timer.value) timer.value=CreateWaitableTimerW(nullptr,FALSE,nullptr);
        if(!timer.value) throw std::runtime_error("Benchmark timer creation failed");
        auto waitUntil=[&](LONGLONG deadline) {
            const auto remaining=deadline-ticks(); if(remaining<=0) return;
            LARGE_INTEGER due{}; due.QuadPart=-std::max<LONGLONG>(1,remaining*10000000/frequency.QuadPart);
            if(!SetWaitableTimer(timer.value,&due,0,nullptr,nullptr,FALSE) || WaitForSingleObject(timer.value,INFINITE)!=WAIT_OBJECT_0) throw std::runtime_error("Benchmark timer failed");
        };
        auto pump=[](){ MSG message{}; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { if(message.message!=WM_TIMER) { TranslateMessage(&message); DispatchMessageW(&message); } } };
        std::array<std::vector<ULONGLONG>,OverlayCount> threadCycles;
        std::array<std::vector<double>,OverlayCount> wallUs;
        std::array<unsigned,OverlayCount> calls{};
        std::ofstream slow(root+L"-outliers.csv");
        slow<<"step,kind,sequence,wall_us,thread_cycles,scene_us,drawing_us,composition_us,present_us,spout_preparation_us,buffer_create_us,map_cache_us\n";
        auto stage=[&](const Overlay& o,const char* name){return profiler.At(ProfileName(o,name)).ticks;};
        std::array<std::uint64_t,OverlayCount> sceneHash{}; sceneHash.fill(1469598103934665603ull);
        const bool traceAll=GetEnvironmentVariableW(L"RACEMATE_BENCH_TRACE_ALL",nullptr,0)!=0;
        auto frame=[&](int step,bool measure) {
            const double time=step*.05; app.now=100000+step*50; PreviewData(time);
            for(int i=0;i<10;++i) { auto& car=app.frame.cars[i]; car.pct=std::fmod(car.pct+.005*std::sin(time*(.7+i*.13))+1,1.); car.estTime=car.pct*PreviewLeaderPace; car.f2Time+=.5*std::sin(time*(.9+i*.1)); car.last+=.1*std::sin(time+i); }
            InvalidateModel();
            for(auto& o:app.overlays) if(o.place.enabled && step%(20/o.place.rate)==0) {
                const LONGLONG stagesBefore[]={stage(o,"  scene"),stage(o,"  drawing"),stage(o,"alpha composition"),stage(o,"present (UpdateLayeredWindow)"),stage(o,"Spout image preparation and queue"),stage(o,"buffer/fonts create"),stage(o,"map cache build")};
                ULONGLONG before=0,after=0; if(measure) QueryThreadCycleTime(GetCurrentThread(),&before); const auto start=ticks();
                RefreshOverlay(o); if(output) PublishSpout(o);
                const auto windowStart=ticks(); UpdateWindow(o.window); GdiFlush(); const auto windowTicks=ticks()-windowStart;
                if(measure) {
                    QueryThreadCycleTime(GetCurrentThread(),&after); threadCycles[o.kind].push_back(after-before); wallUs[o.kind].push_back((ticks()-start)*1e6/frequency.QuadPart); ++calls[o.kind];
                    if(traceAll || wallUs[o.kind].back()>5000) {
                        const LONGLONG stagesAfter[]={stage(o,"  scene"),stage(o,"  drawing"),stage(o,"alpha composition"),stage(o,"present (UpdateLayeredWindow)"),stage(o,"Spout image preparation and queue"),stage(o,"buffer/fonts create"),stage(o,"map cache build")};
                        slow<<step<<','<<o.kind<<','<<o.spoutSequence<<','<<wallUs[o.kind].back()<<','<<after-before;
                        for(int i=0;i<7;++i) slow<<','<<(stagesAfter[i]-stagesBefore[i]+(i==3?windowTicks:0))*1e6/frequency.QuadPart;
                        slow<<'\n';
                    }
                    auto hash=[&](const void* data,size_t size){ for(size_t i=0;i<size;++i) sceneHash[o.kind]=(sceneHash[o.kind]^static_cast<const BYTE*>(data)[i])*1099511628211ull; };
                    for(const auto& c:o.scene) { hash(&c.type,sizeof(c.type)); hash(&c.r,sizeof(c.r)); hash(&c.color,sizeof(c.color)); hash(&c.font,sizeof(c.font)); hash(&c.align,sizeof(c.align)); hash(c.shape.data(),sizeof(float)*c.shape.size()); hash(c.text.data(),c.text.size()*sizeof(wchar_t)); }
                }
                if(verify && background) {
                    std::vector<std::uint32_t> incremental(o.dibPixels,o.dibPixels+static_cast<size_t>(o.width)*o.height);
                    PaintBackgroundOpacity(o);
                    for(size_t i=0;i<incremental.size();++i) mismatchedPixels[o.kind]+=o.dibPixels[i]!=incremental[i];
                    ++checkedImages[o.kind];
                }
            }
        };
        profiler.on=true; profiler.autoWrite=false; profiler.entries.clear();
        const auto warm=ticks(); for(int i=0;i<60;++i) { frame(i,false); pump(); waitUntil(warm+(i+1)*frequency.QuadPart/20); }
        { std::ofstream warmStages(root+L"-warmup-stages.csv"); warmStages<<"stage,calls,total_us\n"; for(const auto& e:profiler.entries) warmStages<<'"'<<e.name<<"\","<<e.calls<<','<<e.ticks*1e6/frequency.QuadPart<<'\n'; }
        { std::ofstream warmWorker(root+L"-worker-warmup.csv"); warmWorker<<"kind,sequence,conversion_us,upload_us,send_us,total_us,texture_created,sender_created\n";
          for(auto& o:app.overlays) if(o.spout) {
            if(!o.spout->WaitIdle()) throw std::runtime_error("Spout warmup timed out"); const auto stats=o.spout->Statistics(true);
            for(const auto& e:stats.outliers) warmWorker<<o.kind<<','<<e.sequence<<','<<e.conversionUs<<','<<e.uploadUs<<','<<e.sendUs<<','<<e.totalUs<<','<<e.textureCreated<<','<<e.senderCreated<<'\n';
          }
        }
        std::ofstream(root+L".ready")<<"ready";
        while(GetFileAttributesW((root+L".go").c_str())==INVALID_FILE_ATTRIBUTES) { pump(); Sleep(5); }
        profiler.on=true; profiler.autoWrite=false; profiler.entries.clear();
        ULONGLONG processBefore=0,processAfter=0; QueryProcessCycleTime(GetCurrentProcess(),&processBefore);
        const auto cpuStart=ProcessCpuMicroseconds(); const auto start=ticks();
        for(int i=0;i<seconds*20;++i) { frame(i+60,true); pump(); waitUntil(start+(i+1)*frequency.QuadPart/20); }
        const double elapsed=(ticks()-start)/static_cast<double>(frequency.QuadPart); QueryProcessCycleTime(GetCurrentProcess(),&processAfter);
        const auto cpuUs=ProcessCpuMicroseconds()-cpuStart;
        std::ofstream report(root+L".csv"); report<<"kind,refreshes,scene_hash,median_thread_cycles,max_thread_cycles,median_wall_us,max_wall_us,elapsed,process_cycles,process_cpu_us\n";
        for(int kind:{RelativeKind,StandingsKind,TrackMapKind}) if(calls[kind]) {
            auto& cycles=threadCycles[kind]; auto& wall=wallUs[kind]; std::sort(cycles.begin(),cycles.end()); std::sort(wall.begin(),wall.end());
            report<<kind<<','<<calls[kind]<<','<<sceneHash[kind]<<','<<cycles[cycles.size()/2]<<','<<cycles.back()<<','<<wall[wall.size()/2]<<','<<wall.back()<<','<<elapsed<<','<<processAfter-processBefore<<','<<cpuUs<<'\n';
        }
        report.close();
        slow.close();
        std::ofstream stages(root+L"-stages.csv"); stages<<"stage,calls,total_us\n"; for(const auto& e:profiler.entries) stages<<'"'<<e.name<<"\","<<e.calls<<','<<e.ticks*1e6/frequency.QuadPart<<'\n'; stages.close();
        std::ofstream(root+L".done")<<"done";
        // Readback and diagnostics happen only after external counters have stopped.
        while(GetFileAttributesW((root+L".release").c_str())==INVALID_FILE_ATTRIBUTES) { pump(); Sleep(5); }
        std::ofstream validation(root+L"-validation.txt");
        std::ofstream worker(root+L"-worker.csv"),workerSlow(root+L"-worker-outliers.csv");
        worker<<"kind,frames,queued,raw_pixels,processed_pixels,upload_bytes,conversion_us,upload_us,send_us\n";
        workerSlow<<"kind,sequence,cycles,conversion_us,upload_us,send_us,total_us,texture_created,sender_created,retry\n";
        for(auto& o:app.overlays) if(o.place.enabled) {
            if(o.spout) {
                o.spout->WaitIdle(); const auto stats=o.spout->Statistics();
                worker<<o.kind<<','<<stats.frames<<','<<stats.queued<<','<<stats.rawPixels<<','<<stats.convertedPixels<<','<<stats.uploadBytes<<','<<stats.conversionUs<<','<<stats.uploadUs<<','<<stats.sendUs<<'\n';
                for(const auto& e:stats.outliers) workerSlow<<o.kind<<','<<e.sequence<<','<<e.cycles<<','<<e.conversionUs<<','<<e.uploadUs<<','<<e.sendUs<<','<<e.totalUs<<','<<e.textureCreated<<','<<e.senderCreated<<','<<e.retry<<'\n';
            }
            if(verify) validation<<o.kind<<": checked images="<<checkedImages[o.kind]<<", cumulative mismatched pixels="<<mismatchedPixels[o.kind]<<'\n';
            if(background) {
                std::vector<std::uint32_t> incremental(o.dibPixels,o.dibPixels+static_cast<size_t>(o.width)*o.height);
                PaintBackgroundOpacity(o); size_t mismatches=0;
                for(size_t i=0;i<incremental.size();++i) mismatches+=incremental[i]!=o.dibPixels[i];
                validation<<o.kind<<": partial/full mismatches="<<mismatches<<'\n';
            }
            if(output) {
                spoutDX receiver; receiver.SetReceiverName(SpoutSenderName(o).c_str());
                std::vector<std::uint32_t> received(static_cast<size_t>(o.width)*o.height);
                bool got=false;
                for(int attempt=0;attempt<100;++attempt) {
                    o.spout->Resend();
                    if(receiver.ReceiveImage(reinterpret_cast<unsigned char*>(received.data()),o.width,o.height) && !receiver.IsUpdated() && std::any_of(received.begin(),received.end(),[](auto p){return p>>24;})) { got=true; break; }
                    Sleep(10);
                }
                validation<<o.kind<<": shared Spout readback="<<(got?"PASS":"FAIL")<<'\n';
                if(!got) throw std::runtime_error("Spout readback failed");
                std::copy(received.begin(),received.end(),o.dibPixels);
                // Straight readback is premultiplied only for validation/BMP display after timing.
                if(!o.place.spoutPremultiplied) for(size_t i=0;i<received.size();++i) {
                    const auto p=o.dibPixels[i],a=p>>24;
                    o.dibPixels[i]=(a<<24)|((((p>>16)&255)*a/255)<<16)|((((p>>8)&255)*a/255)<<8)|((p&255)*a/255);
                }
            } else if(!PerPixel(o)) {
                for(size_t i=0;i<static_cast<size_t>(o.width)*o.height;++i) o.dibPixels[i]|=0xff000000u;
            }
            if(!output && !background) for(size_t i=0;i<static_cast<size_t>(o.width)*o.height;++i) {
                const auto p=o.dibPixels[i]; std::uint32_t out=0;
                for(unsigned shift:{0u,8u,16u,24u}) out|=(((p>>shift)&255)*90/100)<<shift;
                o.dibPixels[i]=out;
            }
            size_t invalid=0,opaque=0,transparent=0,opacityPixels=0;
            for(size_t i=0;i<static_cast<size_t>(o.width)*o.height;++i) { const auto p=o.dibPixels[i],a=p>>24; invalid+=((p&255)>a || ((p>>8)&255)>a || ((p>>16)&255)>a); opaque+=a==255; transparent+=a==0; opacityPixels+=a==229; }
            const bool corners=!(o.dibPixels[0]>>24) && !(o.dibPixels[o.width-1]>>24) && !(o.dibPixels[static_cast<size_t>(o.width)*(o.height-1)]>>24) && !(o.dibPixels[static_cast<size_t>(o.width)*o.height-1]>>24);
            validation<<o.kind<<": invalid premultiplied="<<invalid<<", opaque="<<opaque<<", transparent="<<transparent<<", alpha229="<<opacityPixels<<", corners="<<corners<<'\n';
            if(output && (invalid || !corners || opacityPixels<100 || (background && !opaque))) throw std::runtime_error("Spout alpha validation failed");
            BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{sizeof(info),o.width,-o.height,1,32,BI_RGB}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(info); file.bfSize=file.bfOffBits+o.width*o.height*4;
            std::ofstream image(root+L"-"+std::to_wstring(o.kind)+L".bmp",std::ios::binary); image.write(reinterpret_cast<char*>(&file),sizeof(file)); image.write(reinterpret_cast<char*>(&info),sizeof(info)); image.write(reinterpret_cast<char*>(o.dibPixels),static_cast<std::streamsize>(o.width)*o.height*4);
        }
        for(auto& o:app.overlays) DestroyOverlay(o); return 0;
    } catch(const std::exception& e) { std::ofstream(root+L".error")<<e.what(); }
      catch(HRESULT hr) { std::ofstream(root+L".error")<<"HRESULT "<<std::hex<<static_cast<unsigned>(hr); }
    return 2;
}
