        const int W=TrackMapWidth, end=32+TrackMapBodyHeight;
        const std::wstring title=app.preview?L"Lime Rock Park · Grand Prix · sample cars":Wide(app.session.track+(app.session.trackConfig.empty()?"":" · "+app.session.trackConfig));
        if(!MapOnly(o)) label(title,rect(14,39,W-28,22),white,Bold);
        if(o.map.details && !MapOnly(o)) {
            std::wstring detail=L"ID "+std::to_wstring(trackMap.id);
            if(trackMap.ready) detail+=trackMap.cached?L" · local SVG cache":L" · SVG in memory";
            detail+=L" · "+std::to_wstring(o.place.rate)+L" Hz";
            label(detail,rect(14,61,W-28,18),Ink::Muted,Small);
        }
        const RECT area=rect(24,MapOnly(o)?48:92,W-48,208);
        if(trackMap.ready) {
            RECT physical=area; physical.top-=titleOffset; physical.bottom-=titleOffset;
            out.push_back({DrawCommand::Map,Regular,0,physical,0,{static_cast<float>(trackMap.revision),static_cast<float>(MapStyle(o.map))}});
            if(trackMap.geometry.direction) {
                const auto transform=MapFit(trackMap.geometry,area.right-area.left,area.bottom-area.top,o.map);
                const int reference=ReferenceCar(app.frame);
                const auto own=std::find_if(app.session.drivers.begin(),app.session.drivers.end(),[&](const Driver& d){return d.index==reference;});
                auto drawCar=[&](const Driver& d,bool highlight) {
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
                    if(highlight) {
                        RECT halo=bounds; InflateRect(&halo,px(3),px(3));
                        out.push_back({DrawCommand::MapCar,Regular,0,halo,Ink::White,{static_cast<float>(std::max(1,px(1))),1}});
                    }
                    out.push_back({DrawCommand::MapCar,Regular,0,bounds,color,{static_cast<float>(stroke),car.pit?1.f:0.f}});
                    if(o.map.numbers) label(Wide(d.number),{x-radius,y-radius,x+radius+1,y+radius+1},car.pit?color:Ink::Panel,SmallBold,DT_CENTER);
                };
                for(const auto& d:app.session.drivers) if(d.index!=reference) drawCar(d,false);
                if(own!=app.session.drivers.end()) drawCar(*own,true);
            }
        } else label(trackMap.message,rect(20,MapOnly(o)?100:130,W-40,80),Ink::Muted,Regular,DT_CENTER);
        const bool warning=trackMap.ready && !trackMap.geometry.direction;
        if(!MapOnly(o)) label(trackMap.ready?(warning?trackMap.message:L"FOCUS: white ring · CLASS: color · PIT: hollow"):L"Official iRacing SVGs · prototype",rect(14,307,W-28,18),warning?Ink::Yellow:Ink::Muted,Small,DT_CENTER);
        else if(warning) label(trackMap.message,rect(14,32,W-28,16),Ink::Yellow,Small,DT_CENTER);
        if(trackMap.ready && o.map.details && !warning && !MapOnly(o)) label((o.map.cornerNumbers || o.map.cornerNames) && std::none_of(trackMap.geometry.labels.begin(),trackMap.geometry.labels.end(),[&](const trackmap::Label& item){return ShowMapLabel(item,o.map);})?L"No official labels available for this selection":L"Approximate position from SVG path length",rect(14,330,W-28,18),Ink::Muted,Small,DT_CENTER);
        footer(end);
