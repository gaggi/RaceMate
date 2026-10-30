#include "SpoutPixels.h"
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <random>
using namespace rm;
// Deliberately simple full-frame oracle: original division/region loop.
std::vector<std::uint32_t> Reference(unsigned w,unsigned h,const std::vector<std::uint32_t>& raw,const SpoutSettings& s) {
    auto out=raw; const unsigned half=w/s.panels;
    HRGN region=s.radius?CreateRoundRectRgn(0,0,half+1,h+1,2*s.radius,2*s.radius):nullptr;
    for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
        auto& p=out[y*w+x]; unsigned a=s.perPixel?p>>24:255;
        if(!s.premultiplied && s.perPixel && a && a<255) {
            const unsigned b=std::min(255u,((p&255)*255+a/2)/a),g=std::min(255u,(((p>>8)&255)*255+a/2)/a),r=std::min(255u,(((p>>16)&255)*255+a/2)/a); p=b|(g<<8)|(r<<16);
        }
        if(region && !PtInRegion(region,x%half,y)) a=0;
        if(s.panels==2 && !(s.visiblePanels&(x<half?1:2))) a=0;
        if(s.premultiplied) p=a?(((p&255)*s.opacity/100)|((((p>>8)&255)*s.opacity/100)<<8)|((((p>>16)&255)*s.opacity/100)<<16)|((a*s.opacity/100)<<24)):0;
        else p=(p&0xffffffu)|((a*s.opacity/100)<<24);
    }
    if(region) DeleteObject(region); return out;
}
int main() {
    for(unsigned a=1;a<256;++a) for(unsigned c=0;c<256;++c) if(SpoutPixels::Unpremultiply(c,a)!=std::min(255u,(c*255+a/2)/a)) { std::cerr<<"reciprocal mismatch\n"; return 1; }
    std::mt19937 random(421); unsigned checks=0;
    for(auto dimensions:{std::pair{328u,217u},{472u,345u},{440u,296u},{18u,7u},{64u,48u}}) {
        auto [w,h]=dimensions;
        for(bool perPixel:{false,true}) for(bool pm:{false,true}) for(unsigned panels:{1u,2u}) for(unsigned visible:{0u,1u,2u,3u}) {
            SpoutPixels converter; std::vector<std::uint32_t> raw(w*h);
            SpoutSettings s; s.perPixel=perPixel; s.premultiplied=pm; s.panels=panels; s.visiblePanels=visible;
            for(unsigned step=0;step<12;++step) {
                const bool reset=step%3==0;
                if(reset) { s.opacity=step==0?100:(step==3?90:(step==6?40:70)); s.radius=step<6?0:10; }
                SpoutRect rect=reset?SpoutRect{0,0,w,h}:SpoutRect{random()%(w/2),random()%(h/2),0,0};
                if(!reset) { rect.right=std::min(w,rect.left+1+random()%31); rect.bottom=std::min(h,rect.top+1+random()%23); }
                for(unsigned y=rect.top;y<rect.bottom;++y) for(unsigned x=rect.left;x<rect.right;++x) {
                    unsigned a=perPixel?random()%256:255; unsigned r=random()%(a+1),g=random()%(a+1),b=random()%(a+1); raw[y*w+x]=(a<<24)|(r<<16)|(g<<8)|b;
                }
                converter.Update(w,h,raw.data(),{rect},s); if(converter.Image()!=Reference(w,h,raw,s)) { std::cerr<<"export mismatch "<<w<<"x"<<h<<" step="<<step<<" pp="<<perPixel<<" pm="<<pm<<" panels="<<panels<<" mask="<<visible<<"\n"; return 1; } ++checks;
            }
        }
    }
    std::cout<<"reciprocal exhaustive; "<<checks<<" full/partial/mode/opacity/corner/panel checks: PASS\n"; return 0;
}
