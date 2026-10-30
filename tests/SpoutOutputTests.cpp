#include "SpoutOutput.h"
#include "third_party/spout/SpoutDX.h"
#include <chrono>
#include <iostream>
#include <thread>
#include <algorithm>

// Actual D3D11 -> shared texture -> receiver -> readback. Requires a Windows GPU.
static bool CheckCornerRegion() {
    // Compare the fast corner restriction against the full Windows region oracle,
    // including tiny windows and scaled radii, for all pixels (not only the corners).
    for(int width:{1,9,32,328}) for(int height:{1,7,48,217}) for(int radius:{1,5,10,20}) {
        HRGN region=CreateRoundRectRgn(0,0,width+1,height+1,2*radius,2*radius);
        if(!region) return false;
        bool equal=true;
        const int extent=radius+1;
        for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
            const bool inside=PtInRegion(region,x,y)!=FALSE;
            const bool corner=(y<extent || y>=height-extent) && (x<extent || x>=width-extent);
            if(!corner && !inside) equal=false;
        }
        DeleteObject(region); if(!equal) return false;
    }
    return true;
}
static bool RoundTrip(rm::SpoutChannel& sender,spoutDX& receiver,unsigned width,unsigned height,std::uint32_t pixel) {
    std::vector<std::uint32_t> pixels(static_cast<size_t>(width)*height);
    for(int attempt=0;attempt<150;++attempt) {
        sender.Submit({width,height,std::vector<std::uint32_t>(pixels.size(),pixel)});
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const bool received=receiver.ReceiveImage(reinterpret_cast<unsigned char*>(pixels.data()),width,height);
        if(receiver.IsUpdated()) continue;
        if(received && receiver.GetWidth()==width && receiver.GetHeight()==height &&
           std::all_of(pixels.begin(),pixels.end(),[&](auto p){ return p==pixel; })) return true;
    }
    std::wcerr<<sender.Status()<<L"\n"; return false;
}
int main(int argc,char** argv) {
    if(!CheckCornerRegion()) { std::cerr<<"Corner mask equivalence: FAIL\n"; return 1; }
    std::cout<<"Corner mask equivalence (64 geometries): PASS\n";
    if(argc==2) {
        spoutDX receiver; receiver.SetReceiverName(argv[1]);
        std::vector<std::uint32_t> pixels;
        for(int attempt=0;attempt<150;++attempt) {
            receiver.ReceiveImage(pixels.empty()?nullptr:reinterpret_cast<unsigned char*>(pixels.data()),receiver.GetWidth(),receiver.GetHeight());
            if(receiver.IsUpdated()) pixels.assign(static_cast<size_t>(receiver.GetWidth())*receiver.GetHeight(),0);
            else if(!pixels.empty() && std::any_of(pixels.begin(),pixels.end(),[](auto p){return p>>24;})) {
                std::cout<<"Live source: "<<argv[1]<<", "<<receiver.GetWidth()<<"x"<<receiver.GetHeight()<<", nonempty alpha: PASS\n";
                for(unsigned y=0;y<std::min(12u,receiver.GetHeight());++y) {
                    for(unsigned x=0;x<std::min(12u,receiver.GetWidth());++x) std::cout<<((pixels[y*receiver.GetWidth()+x]>>24)?'#':'.');
                    std::cout<<'\n';
                }
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::cerr<<"Live source readback: FAIL\n"; return 1;
    }
    const auto name="RaceMate smoke "+std::to_string(GetCurrentProcessId());
    spoutDX receiver; receiver.SetReceiverName(name.c_str());
    auto sender=std::make_unique<rm::SpoutChannel>(name);
    bool ok=RoundTrip(*sender,receiver,64,32,0x80402010u);
    std::cout<<"BGRA and alpha round trip: "<<(ok?"PASS":"FAIL")<<"\n";
    bool resize=RoundTrip(*sender,receiver,128,48,0xff123456u); ok=ok && resize;
    std::cout<<"Resize round trip: "<<(resize?"PASS":"FAIL")<<"\n";
    // Two simultaneous senders exercise the shared device.
    spoutDX secondReceiver; secondReceiver.SetReceiverName((name+" second").c_str());
    auto second=std::make_unique<rm::SpoutChannel>(name+" second");
    bool multiple=RoundTrip(*second,secondReceiver,33,17,0x00112233u) && RoundTrip(*sender,receiver,128,48,0x7f123456u);
    ok=ok && multiple; std::cout<<"Multiple sources and zero alpha: "<<(multiple?"PASS":"FAIL")<<"\n";
    sender.reset(); second.reset();
    bool removed=false;
    for(int attempt=0;attempt<100;++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        receiver.ReceiveImage(nullptr,0,0); secondReceiver.ReceiveImage(nullptr,0,0);
        if(!receiver.IsConnected() && !secondReceiver.IsConnected()) { removed=true; break; }
    }
    ok=ok && removed; std::cout<<"Sender cleanup: "<<(removed?"PASS":"FAIL")<<"\n";
    // Re-enable after all sources closed (device was released).
    sender=std::make_unique<rm::SpoutChannel>(name);
    bool restart=RoundTrip(*sender,receiver,16,16,0xffffffffu);
    ok=ok && restart; std::cout<<"Restart after cleanup: "<<(restart?"PASS":"FAIL")<<"\n";
    {
        constexpr unsigned w=128,h=96;
        const auto dirtyName=name+" dirty"; rm::SpoutChannel dirtySender(dirtyName); spoutDX dirtyReceiver; dirtyReceiver.SetReceiverName(dirtyName.c_str());
        std::vector<std::uint32_t> raw(w*h,0xff203040u); rm::SpoutSettings settings;
        dirtySender.SubmitRegions(w,h,raw.data(),{},settings,true); dirtySender.WaitIdle();
        for(unsigned y=7;y<13;++y) for(unsigned x=9;x<14;++x) raw[y*w+x]=0xff123456u;
        dirtySender.SubmitRegions(w,h,raw.data(),{{9,7,14,13}},settings); dirtySender.WaitIdle();
        const auto stats=dirtySender.Statistics();
        const bool partial=stats.rawPixels==w*h+30 && stats.uploadBytes==w*h*4+120;
        for(unsigned i=0;i<500;++i) { const unsigned x=i%w,y=(i/w)%h; raw[y*w+x]=0xff000000u|i; dirtySender.SubmitRegions(w,h,raw.data(),{{x,y,x+1,y+1}},settings); }
        dirtySender.WaitIdle();
        std::vector<std::uint32_t> received(w*h); bool correct=false;
        for(int attempt=0;attempt<150;++attempt) {
            dirtySender.Resend(); dirtySender.WaitIdle();
            if(dirtyReceiver.ReceiveImage(reinterpret_cast<unsigned char*>(received.data()),w,h) && !dirtyReceiver.IsUpdated() && received==raw) { correct=true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ok=ok && partial && correct;
        std::cout<<"Dirty upload byte counts and coalesced queued-region readback: "<<(partial && correct?"PASS":"FAIL")<<"\n";
    }
    return ok?0:1;
}
