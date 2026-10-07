        int y=40;
        for(int element:app.weatherOrder) if(app.weatherDisplay[element]) {
            const int h=WeatherHeights[element];
            Fill(dc,rect(8,y,224,h),RGB(30,34,41));
            if(element<2) {
                Label(dc,element==0?L"Track Temp":L"Air Temp",rect(20,y,112,h),white,o.font);
                Label(dc,FooterTemperature(element==0?app.frame.trackTemperature:app.frame.airTemperature),rect(132,y,88,h),white,o.bold,DT_RIGHT);
            } else if(element==2) {
                Label(dc,L"Wind",rect(20,y,110,32),white,o.font);
                Label(dc,L"km/h",rect(166,y,54,32),muted,o.font,DT_RIGHT);
                const double angle=app.weatherDisplay[5]?RelativeWindAngle(app.frame):std::numeric_limits<double>::quiet_NaN();
                const int cy=y+106;
                // Use float geometry and a true circular arc, with no intermediate pixel rounding.
                // Reset the GDI viewport while drawing in physical client coordinates.
                SetViewportOrgEx(dc,0,0,nullptr);
                {
                    const float scale=static_cast<float>(dpi*o.place.scale)/9600;
                    Gdiplus::Graphics graphics(dc); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                    const float centerX=120*scale,centerY=cy*scale-titleOffset,radius=65*scale;
                    Gdiplus::Pen ring(Gdiplus::Color(255,232,234,237),4*scale); ring.SetStartCap(Gdiplus::LineCapRound); ring.SetEndCap(Gdiplus::LineCapRound);
                    if(std::isfinite(angle)) graphics.DrawArc(&ring,centerX-radius,centerY-radius,2*radius,2*radius,static_cast<float>((angle+.3)*180/3.141592653589793-90),static_cast<float>(360-.6*180/3.141592653589793));
                    else graphics.DrawEllipse(&ring,centerX-radius,centerY-radius,2*radius,2*radius);
                    if(std::isfinite(angle)) {
                        auto point=[&](double bearing,double r) {return Gdiplus::PointF(centerX+static_cast<float>(std::sin(bearing)*r)*scale,centerY-static_cast<float>(std::cos(bearing)*r)*scale);};
                        Gdiplus::PointF arrow[]={point(angle-.15,71),point(angle,56),point(angle+.15,71)};
                        Gdiplus::Pen pen(Gdiplus::Color(255,246,204,66),4*scale); pen.SetStartCap(Gdiplus::LineCapRound); pen.SetEndCap(Gdiplus::LineCapRound); pen.SetLineJoin(Gdiplus::LineJoinRound);
                        graphics.DrawLines(&pen,arrow,3);
                    }
                }
                SetViewportOrgEx(dc,0,-titleOffset,nullptr);
                Label(dc,Format(app.frame.windSpeed*3.6,0),rect(65,cy-24,110,48),white,o.valueFont?o.valueFont:o.bold,DT_CENTER);
                if(app.weatherDisplay[5] && !std::isfinite(angle) && app.frame.windSpeed!=0) Label(dc,L"Direction —",rect(16,y+169,208,21),muted,o.font,DT_CENTER);
            } else if(element==3) {
                Label(dc,FooterValue(14),rect(20,y,200,h),white,o.font,DT_CENTER);
            } else {
                std::wstring usage=Wide(app.session.trackUsage); if(usage.empty()) usage=L"Track Usage —"; else usage[0]=static_cast<wchar_t>(towupper(usage[0]));
                Label(dc,usage,rect(20,y,200,h),white,o.font,DT_CENTER);
            }
            y+=h+8;
        }
