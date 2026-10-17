        const int W=TrackMapWidth, end=32+TrackMapBodyHeight;
        // Track and layout are in the header; the map takes the body, with one line under it for a warning or the details.
        const bool warning=trackMap.ready && !trackMap.geometry.direction;
        const bool note=!MapOnly(o) && (warning || o.map.details);
        const RECT area=MapOnly(o)?rect(24,48,W-48,208):rect(20,44,W-40,note?218:240);
        if(trackMap.ready) {
            RECT physical=area; physical.top-=titleOffset; physical.bottom-=titleOffset;
            out.push_back({DrawCommand::Map,Regular,0,physical,0,{static_cast<float>(trackMap.revision),static_cast<float>(MapStyle(o.map))}});
            if(trackMap.geometry.direction) {
                const auto transform=MapFit(trackMap.geometry,area.right-area.left,area.bottom-area.top,o.map);
                const int reference=ReferenceCar(app.frame);
                const auto own=std::find_if(app.session.drivers.begin(),app.session.drivers.end(),[&](const Driver& d){return d.index==reference;});
                // Markers first, numbers second: the cars share one canvas and no marker covers another car's number.
                auto drawCar=[&](const Driver& d,bool highlight,bool number) {
                    if(d.index<0 || d.index>=MaxCars || d.paceCar || d.spectator) return;
                    const auto& car=app.frame.cars[d.index];
                    if(!std::isfinite(car.pct) || car.pct<0 || car.pct>=1 || car.surface<0 || (!o.map.pit && car.pit)) return;
                    if(o.map.ownClass && own!=app.session.drivers.end() && d.classId!=own->classId) return;
                    const double progress=app.preview?std::fmod(app.now/90000.+d.index*.091,1.):car.pct;
                    const auto point=transform.Apply(trackMap.geometry.At(progress));
                    const int x=area.left+static_cast<int>(std::lround(point.x)), y=area.top+static_cast<int>(std::lround(point.y));
                    const int radius=px(o.map.numbers?11:6), stroke=std::max(1,px(highlight?2:1));
                    const COLORREF color=highlight?Ink::Yellow:ColorOf(d.classColor);
                    const RECT bounds{x-radius,y-radius-titleOffset,x+radius+1,y+radius+1-titleOffset};
                    if(number) { if(o.map.numbers) label(Wide(d.number),{x-radius,y-radius,x+radius+1,y+radius+1},car.pit?color:Ink::Panel,SmallBold,DT_CENTER); return; }
                    if(highlight) {
                        RECT halo=bounds; InflateRect(&halo,px(3),px(3));
                        out.push_back({DrawCommand::MapCar,Regular,0,halo,Ink::White,{static_cast<float>(std::max(1,px(1))),1}});
                    }
                    out.push_back({DrawCommand::MapCar,Regular,0,bounds,color,{static_cast<float>(stroke),car.pit?1.f:0.f}});
                };
                // Where the own car would rejoin after a stop now: a hollow accent ring with its class position.
                const auto& rejoin=PitExit();
                auto drawGhost=[&](bool number) {
                    if(!o.map.pitExit || !rejoin.valid) return;
                    const auto point=transform.Apply(trackMap.geometry.At(app.preview?std::fmod(app.now/90000.+reference*.091+.7,1.):rejoin.pct));
                    const int x=area.left+static_cast<int>(std::lround(point.x)), y=area.top+static_cast<int>(std::lround(point.y)), radius=px(11);
                    if(number) { label(L"P"+std::to_wstring(rejoin.classPosition),{x-radius,y-radius,x+radius+1,y+radius+1},Ink::Accent,SmallBold,DT_CENTER); return; }
                    out.push_back({DrawCommand::MapCar,Regular,0,{x-radius,y-radius-titleOffset,x+radius+1,y+radius+1-titleOffset},Ink::Accent,{static_cast<float>(std::max(1,px(2))),1}});
                };
                for(bool number:{false,true}) {
                    for(const auto& d:app.session.drivers) if(d.index!=reference) drawCar(d,false,number);
                    drawGhost(number);
                    if(own!=app.session.drivers.end()) drawCar(*own,true,number);
                }
            }
        } else label(trackMap.message,rect(20,MapOnly(o)?100:120,W-40,80),Ink::Muted,Regular,DT_CENTER);
        if(note) {
            std::wstring text=trackMap.message;
            if(!warning) {
                text=L"ID "+std::to_wstring(trackMap.id)+(trackMap.ready?(trackMap.cached?L" · local SVG cache":L" · SVG in memory"):L"")+L" · "+std::to_wstring(o.place.rate)+L" Hz";
                if(trackMap.ready) text+=(o.map.cornerNumbers || o.map.cornerNames) && std::none_of(trackMap.geometry.labels.begin(),trackMap.geometry.labels.end(),[&](const trackmap::Label& item){return ShowMapLabel(item,o.map);})?L" · no official labels for this selection":L" · position from SVG path length";
            }
            label(std::move(text),rect(14,266,W-28,18),warning?Ink::Yellow:Ink::Muted,Small,DT_CENTER);
        }
        else if(MapOnly(o) && warning) label(trackMap.message,rect(14,32,W-28,16),Ink::Yellow,Small,DT_CENTER);
        footer(end);
