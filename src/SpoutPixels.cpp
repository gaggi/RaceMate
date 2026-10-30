#include "SpoutPixels.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
namespace rm {
static const auto Reciprocal=[] { std::array<std::uint64_t,256> r{}; for(unsigned a=1;a<256;++a) r[a]=((1ull<<32)+a-1)/a; return r; }();
inline unsigned UndoAlpha(unsigned channel,unsigned alpha) {
    return alpha?std::min(255u,static_cast<unsigned>(((channel*255+(alpha>>1))*Reciprocal[alpha])>>32)):channel;
}
unsigned SpoutPixels::Unpremultiply(unsigned channel,unsigned alpha) {
    return UndoAlpha(channel,alpha);
}
void SpoutPixels::BuildCorners() {
    cornerZeros_.clear(); if(!settings_.radius || !width_ || !height_) return;
    const unsigned panels=std::clamp(settings_.panels,1u,2u),half=width_/panels,extent=std::min(settings_.radius+1,std::max(half,height_));
    HRGN region=CreateRoundRectRgn(0,0,half+1,height_+1,2*settings_.radius,2*settings_.radius);
    for(unsigned panel=0;panel<panels;++panel) for(unsigned y=0;y<height_;++y) {
        if(y>=extent && y+extent<height_) continue;
        for(unsigned x=0;x<half;++x) if((x<extent || x+extent>=half) && !PtInRegion(region,x,y)) cornerZeros_.push_back(static_cast<size_t>(y)*width_+panel*half+x);
    }
    if(region) DeleteObject(region);
}
SpoutRect SpoutPixels::Update(unsigned width,unsigned height,const std::uint32_t* source,const std::vector<SpoutRect>& dirty,const SpoutSettings& settings) {
    const bool geometry=!initialized_ || width_!=width || height_!=height || settings.radius!=settings_.radius || settings.panels!=settings_.panels;
    if(!initialized_ || settings.opacity!=settings_.opacity) for(unsigned c=0;c<256;++c) scale_[c]=c*std::min(settings.opacity,100u)/100;
    width_=width; height_=height; settings_=settings; image_.resize(static_cast<size_t>(width)*height);
    if(geometry) BuildCorners(); initialized_=true;
    SpoutRect bounds{width,height,0,0};
    const unsigned opaque=scale_[255]<<24;
    for(auto r:dirty) {
        r.left=std::min(r.left,width); r.right=std::min(r.right,width); r.top=std::min(r.top,height); r.bottom=std::min(r.bottom,height); if(r.Empty()) continue;
        bounds.left=std::min(bounds.left,r.left); bounds.top=std::min(bounds.top,r.top); bounds.right=std::max(bounds.right,r.right); bounds.bottom=std::max(bounds.bottom,r.bottom);
        for(unsigned y=r.top;y<r.bottom;++y) {
            const auto* from=source+static_cast<size_t>(y)*width+r.left; auto* to=image_.data()+static_cast<size_t>(y)*width+r.left; const unsigned count=r.right-r.left;
            if(settings.perPixel && settings.premultiplied && settings.opacity==100) { std::memcpy(to,from,count*4); continue; }
            if(!settings.perPixel && !settings.premultiplied) { for(unsigned x=0;x<count;++x) to[x]=(from[x]&0xffffffu)|opaque; continue; }
            if(settings.premultiplied) {
                for(unsigned x=0;x<count;++x) { const auto p=from[x]; const unsigned a=settings.perPixel?p>>24:255; to[x]=a?(scale_[p&255]|(scale_[(p>>8)&255]<<8)|(scale_[(p>>16)&255]<<16)|(scale_[a]<<24)):0; }
            } else {
                for(unsigned x=0;x<count;++x) { const auto p=from[x]; const unsigned a=p>>24; to[x]=UndoAlpha(p&255,a)|(UndoAlpha((p>>8)&255,a)<<8)|(UndoAlpha((p>>16)&255,a)<<16)|(scale_[a]<<24); }
            }
        }
    }
    // Cached corner indices touch only the corner areas, never the interior loop.
    if(settings.premultiplied) for(size_t i:cornerZeros_) image_[i]=0;
    else for(size_t i:cornerZeros_) image_[i]&=0xffffffu;
    if(settings.panels==2 && settings.visiblePanels!=3) {
        const unsigned half=width/2;
        for(auto r:dirty) for(unsigned y=r.top;y<std::min(r.bottom,height);++y) for(unsigned panel=0;panel<2;++panel) if(!(settings.visiblePanels&(1u<<panel))) {
            const unsigned left=std::max(r.left,panel*half),right=std::min(r.right,(panel+1)*half); if(left>=right) continue;
            auto* row=image_.data()+static_cast<size_t>(y)*width;
            if(settings.premultiplied) std::fill(row+left,row+right,0u); else for(unsigned x=left;x<right;++x) row[x]&=0xffffffu;
        }
    }
    return bounds;
}
}
