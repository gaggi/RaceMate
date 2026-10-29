        const int W=TrackMapWidthOf(o.map);
        // Track and layout are in the header; the map takes the body, with one line under it for a warning or the details.
        const bool svg=o.map.layout==MapLayout, line=o.map.layout==LineLayout;
        const bool warning=svg && trackMap.ready && !trackMap.geometry.direction;
        const bool note=!MapOnly(o) && (warning || o.map.details || o.map.sectors);
        const int end=32+TrackMapBody(o.map,MapOnly(o),note);
        // Circle and line place cars by lap fraction alone: 0 % at the top or left, clockwise or to the right.
        const bool live=app.preview || app.frame.connected;
        const RECT area=line?(MapOnly(o)?rect(24,40,W-48,48):rect(24,40,W-48,40))
            :(MapOnly(o)?rect(24,48,W-48,208):rect(20,44,W-40,note?218:240));
        const bool ready=svg?trackMap.ready:live;
        if(ready) {
            RECT physical=area; physical.top-=titleOffset; physical.bottom-=titleOffset;
            out.push_back({DrawCommand::Map,Regular,0,physical,0,{static_cast<float>(trackMap.revision),static_cast<float>(MapStyle(o.map)),
                o.map.sectors?SectorSceneRevision():0.f,static_cast<float>(o.map.sectorMarks*2+o.map.sectorLabels),
                static_cast<float>(o.map.sectors?o.map.sectorParts:0)}});
            if(!svg || trackMap.geometry.direction) {
                const auto transform=svg?MapFit(trackMap.geometry,area.right-area.left,area.bottom-area.top,o.map):MapTransform{};
                const auto ring=MapShape(o,area.right-area.left,area.bottom-area.top);
                auto place=[&](double pct) {
                    const auto point=svg?transform.Apply(trackMap.geometry.At(pct)):ring.At(pct);
                    return POINT{area.left+static_cast<int>(std::lround(point.x)),area.top+static_cast<int>(std::lround(point.y))};
                };
                const int reference=ReferenceCar(app.frame);
                const auto own=std::find_if(app.session.drivers.begin(),app.session.drivers.end(),[&](const Driver& d){return d.index==reference;});
                // A marker, or the number on it.
                auto drawCar=[&](const Driver& d,bool highlight) {
                    if(d.index<0 || d.index>=MaxCars || d.paceCar || d.spectator) return;
                    const auto& car=app.frame.cars[d.index];
                    if(!std::isfinite(car.pct) || car.pct<0 || car.pct>=1 || car.surface<0 || (!o.map.pit && car.pit)) return;
                    if(o.map.ownClass && own!=app.session.drivers.end() && d.classId!=own->classId) return;
                    // In the pits on the drawn pit lane, when it is shown; otherwise at its lap position on the track.
                    const double lap=car.pct;
                    POINT p=place(lap);
                    if(svg && o.map.pitLane && car.pit) { const auto q=transform.Apply(trackMap.geometry.PitAt(lap)); p={area.left+static_cast<int>(std::lround(q.x)),area.top+static_cast<int>(std::lround(q.y))}; }
                    const int x=p.x, y=p.y;
                    // A number wider than the usual marker, such as three digits, gets a marker that fits it.
                    // Car number, or the position in class or overall; an unknown position leaves the marker empty.
                    const int standing=o.map.markerText==1?car.classPosition:car.position;
                    auto text=!o.map.numbers?std::wstring():(o.map.markerText==0?Wide(d.number):(standing>0?std::to_wstring(standing):std::wstring()));
                    const int radius=o.map.numbers?std::max(px(11),(measure(text,SmallBold)+px(7))/2):px(6), stroke=std::max(1,px(highlight?2:1));
                    const COLORREF color=highlight?Ink::Yellow:ColorOf(d.classColor);
                    const RECT bounds{x-radius,y-radius-titleOffset,x+radius+1,y+radius+1-titleOffset};
                    if(highlight) {
                        RECT halo=bounds; InflateRect(&halo,px(3),px(3));
                        out.push_back({DrawCommand::MapCar,Regular,0,halo,Ink::White,{static_cast<float>(std::max(1,px(1))),1}});
                    }
                    out.push_back({DrawCommand::MapCar,Regular,0,bounds,color,{static_cast<float>(stroke),car.pit?1.f:0.f}});
                    if(!text.empty()) out.push_back({DrawCommand::MapLabel,SmallBold,DT_CENTER,bounds,car.pit?color:Ink::Panel,{},std::move(text)});
                };
                // Where the own car would rejoin after a stop now: a hollow accent ring marked PIT.
                static const PitRejoin none; // Worked out only while the ring is shown.
                const auto& rejoin=o.map.pitExit?PitExit():none;
                auto drawGhost=[&](bool number) {
                    if(!o.map.pitExit || !rejoin.valid) return;
                    const auto p=place(rejoin.pct);
                    const int x=p.x, y=p.y, radius=px(11);
                    if(number) { out.push_back({DrawCommand::MapLabel,SmallBold,DT_CENTER,{x-radius-px(4),y-radius-titleOffset,x+radius+px(5),y+radius+1-titleOffset},Ink::Accent,{},L"PIT"}); return; }
                    out.push_back({DrawCommand::MapCar,Regular,0,{x-radius,y-radius-titleOffset,x+radius+1,y+radius+1-titleOffset},Ink::Accent,{static_cast<float>(std::max(1,px(2))),1}});
                };
                // Each car whole, marker and number together, from the back of the race to the front: the leader
                // covers the car behind it, and so on. The PIT ring and the car you drive or watch come last.
                std::vector<const Driver*> stack;
                for(const auto& d:app.session.drivers) if(d.index!=reference && d.index>=0 && d.index<MaxCars) stack.push_back(&d);
                auto rank=[](const Driver* d){ const int p=app.frame.cars[d->index].position; return p>0?p:MaxCars+1; };
                std::stable_sort(stack.begin(),stack.end(),[&](const Driver* a,const Driver* b){ return rank(a)>rank(b); });
                for(const auto* d:stack) drawCar(*d,false);
                drawGhost(false); drawGhost(true);
                if(own!=app.session.drivers.end()) drawCar(*own,true);
            }
        } else label(svg?trackMap.message:L"Waiting for iRacing",line?rect(20,40,W-40,40):rect(20,MapOnly(o)?100:120,W-40,80),Ink::Muted,Regular,DT_CENTER);
        if(note) {
            std::wstring text=trackMap.message;
            if(!warning && o.map.sectors) {
                text=SectorMapCaption();
            } else if(!warning) {
                if(!svg) text=L"Lap position  ·  "+std::to_wstring(o.place.rate)+L" Hz";
                else {
                    text=L"ID "+std::to_wstring(trackMap.id)+(trackMap.ready?(trackMap.cached?L" · local SVG cache":L" · SVG in memory"):L"")+L" · "+std::to_wstring(o.place.rate)+L" Hz";
                    if(trackMap.ready) text+=(o.map.cornerNumbers || o.map.cornerNames) && std::none_of(trackMap.geometry.labels.begin(),trackMap.geometry.labels.end(),[&](const trackmap::Label& item){return ShowMapLabel(item,o.map);})?L" · no official labels for this selection":L" · position from SVG path length";
                }
            }
            label(std::move(text),line?rect(14,82,W-28,18):rect(14,266,W-28,18),warning?Ink::Yellow:Ink::Muted,Small,DT_CENTER);
        }
        // Without background text needs alpha too, like the car numbers.
        else if(MapOnly(o) && warning) { RECT r=rect(14,32,W-28,16); r.top-=titleOffset; r.bottom-=titleOffset; out.push_back({DrawCommand::MapLabel,Small,DT_CENTER,r,Ink::Yellow,{},trackMap.message}); }
        footer(end);
