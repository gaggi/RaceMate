#include "SpoutOutput.h"
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif
#include "third_party/spout/SpoutDX.h"
#include <wrl/client.h>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace rm {
using Clock=std::chrono::steady_clock;
struct SpoutChannel::State {
    std::string name;
    Frame pending;
    std::vector<SpoutRect> dirty;
    std::vector<std::uint32_t> spare;
    SpoutSettings options{},lastInput{};
    unsigned inputWidth=0,inputHeight=0;
    bool initialized=false,hasPending=false,closed=false,resend=false,processing=false;
    std::uint64_t sequence=0;
    std::wstring status=L"Waiting for first frame";
    Stats stats;
};
class SpoutWorker {
public:
    std::mutex mutex; std::condition_variable wake;
    std::vector<std::shared_ptr<SpoutChannel::State>> channels;
    static std::shared_ptr<SpoutWorker> Get() { static auto worker=std::make_shared<SpoutWorker>(); return worker; }
    void Add(const std::shared_ptr<SpoutChannel::State>& s) {
        std::lock_guard lock(mutex); channels.push_back(s); if(!thread.joinable()) thread=std::thread([this]{Run();}); wake.notify_one();
    }
    ~SpoutWorker() { { std::lock_guard lock(mutex); stopping=true; } wake.notify_one(); if(thread.joinable()) thread.join(); }
private:
    struct Output {
        SpoutPixels pixels;
        std::unique_ptr<spoutDX> sender;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Clock::time_point retry{};
    };
    std::thread thread; bool stopping=false;
    static double Now() { LARGE_INTEGER t{},f{}; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f); return t.QuadPart*1e6/f.QuadPart; }
    void Run() {
        std::unique_ptr<spoutDX> device;
        std::map<SpoutChannel::State*,Output> outputs;
        for(;;) {
            std::unique_lock lock(mutex);
            auto deadline=Clock::time_point::max(); bool ready=false;
            for(const auto& s:channels) {
                auto it=outputs.find(s.get());
                if(s->closed || s->hasPending || s->resend) ready=true;
                if(it!=outputs.end() && it->second.retry!=Clock::time_point{} && !it->second.pixels.Image().empty()) deadline=std::min(deadline,it->second.retry);
            }
            if(!ready && !stopping) {
                if(deadline==Clock::time_point::max()) wake.wait(lock); else wake.wait_until(lock,deadline);
            }
            if(stopping) break;
            auto current=channels;
            for(const auto& s:current) {
                if(s->closed) { lock.unlock(); outputs.erase(s.get()); lock.lock(); std::erase(channels,s); continue; }
                auto it=outputs.find(s.get());
                const bool retry=it!=outputs.end() && it->second.retry!=Clock::time_point{} && Clock::now()>=it->second.retry;
                if(!s->hasPending && !s->resend && !retry) continue;
                SpoutChannel::Frame frame; std::vector<SpoutRect> dirty; const auto options=s->options; const auto sequence=s->sequence;
                if(s->hasPending) {
                    frame.width=s->pending.width; frame.height=s->pending.height; frame.pixels.swap(s->pending.pixels);
                    s->pending.pixels.swap(s->spare); dirty.swap(s->dirty); s->hasPending=false;
                }
                s->resend=false; s->processing=true;
                lock.unlock();
                auto& out=outputs[s.get()]; SpoutChannel::Event event; event.sequence=sequence; event.retry=retry;
                const auto start=Now(); ULONGLONG c0=0,c1=0; QueryThreadCycleTime(GetCurrentThread(),&c0);
                SpoutRect bounds{}; std::uint64_t converted=0,uploaded=0; bool ok=false;
                try {
                    if(!frame.pixels.empty()) {
                        const auto begin=Now(); bounds=out.pixels.Update(frame.width,frame.height,frame.pixels.data(),dirty,options); event.conversionUs=Now()-begin;
                        for(const auto r:dirty) converted+=static_cast<std::uint64_t>(r.right-r.left)*(r.bottom-r.top);
                    }
                    if(!out.pixels.Image().empty() && Clock::now()>=out.retry) {
                        if(!device) { auto candidate=std::make_unique<spoutDX>(); if(candidate->OpenDirectX11()) device=std::move(candidate); }
                        if(device) {
                            const auto begin=Now();
                            const unsigned w=out.pixels.Width(),h=out.pixels.Height();
                            D3D11_TEXTURE2D_DESC existing{}; if(out.texture) out.texture->GetDesc(&existing);
                            if(!out.texture || existing.Width!=w || existing.Height!=h) {
                                out.texture.Reset(); D3D11_TEXTURE2D_DESC desc{}; desc.Width=w; desc.Height=h; desc.MipLevels=desc.ArraySize=1;
                                desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                                D3D11_SUBRESOURCE_DATA initial{out.pixels.Image().data(),w*4,0};
                                if(FAILED(device->GetDX11Device()->CreateTexture2D(&desc,&initial,&out.texture))) throw std::runtime_error("texture allocation failed");
                                event.textureCreated=true; uploaded=static_cast<std::uint64_t>(w)*h*4;
                            } else if(!bounds.Empty()) {
                                D3D11_BOX box{bounds.left,bounds.top,0,bounds.right,bounds.bottom,1};
                                device->GetDX11Context()->UpdateSubresource(out.texture.Get(),0,&box,out.pixels.Image().data()+static_cast<size_t>(bounds.top)*w+bounds.left,w*4,0);
                                uploaded=static_cast<std::uint64_t>(bounds.right-bounds.left)*(bounds.bottom-bounds.top)*4;
                            }
                            event.uploadUs=Now()-begin;
                            const auto send=Now();
                            if(!out.sender) {
                                out.sender=std::make_unique<spoutDX>(); out.sender->SetSenderName(s->name.c_str()); out.sender->SetSenderFormat(DXGI_FORMAT_B8G8R8A8_UNORM);
                                if(!out.sender->OpenDirectX11(device->GetDX11Device())) throw std::runtime_error("sender device unavailable");
                                event.senderCreated=true;
                            }
                            ok=out.sender->SendTexture(out.texture.Get()); event.sendUs=Now()-send;
                            if(FAILED(device->GetDX11Device()->GetDeviceRemovedReason())) {
                                // Keep the CPU export images, invalidate every GPU texture/sender.
                                for(auto& pair:outputs) { pair.second.sender.reset(); pair.second.texture.Reset(); pair.second.retry=Clock::now()+std::chrono::seconds(2); }
                                device.reset(); ok=false;
                            }
                        }
                        if(ok) out.retry={}; else { out.sender.reset(); out.texture.Reset(); out.retry=Clock::now()+std::chrono::seconds(2); }
                    }
                } catch(...) { out.sender.reset(); out.texture.Reset(); out.retry=Clock::now()+std::chrono::seconds(2); }
                QueryThreadCycleTime(GetCurrentThread(),&c1); event.cycles=c1-c0; event.totalUs=Now()-start;
                lock.lock();
                s->spare.swap(frame.pixels); s->processing=false;
                auto& stats=s->stats; ++stats.frames; stats.convertedPixels+=converted; stats.uploadBytes+=uploaded;
                stats.conversionUs+=event.conversionUs; stats.uploadUs+=event.uploadUs; stats.sendUs+=event.sendUs;
                if(event.totalUs>5000 || event.textureCreated || event.senderCreated) { if(stats.outliers.size()>=512) stats.outliers.erase(stats.outliers.begin()); stats.outliers.push_back(event); }
                s->status=ok?L"Spout source ready":L"Spout unavailable; retrying (use the same GPU as OBS)";
                wake.notify_all();
            }
            if(channels.empty()) { lock.unlock(); outputs.clear(); device.reset(); }
        }
        outputs.clear(); device.reset();
    }
};
SpoutChannel::SpoutChannel(std::string name):state_(std::make_shared<State>()),worker_(SpoutWorker::Get()) { state_->name=std::move(name); worker_->Add(state_); }
SpoutChannel::~SpoutChannel() { { std::lock_guard lock(worker_->mutex); state_->closed=true; } worker_->wake.notify_one(); }
void SpoutChannel::Submit(Frame frame) { if(frame.pixels.size()!=static_cast<size_t>(frame.width)*frame.height) return; SubmitRegions(frame.width,frame.height,frame.pixels.data(),{},SpoutSettings{},true); }
std::uint64_t SpoutChannel::SubmitRegions(unsigned w,unsigned h,const std::uint32_t* source,const std::vector<SpoutRect>& regions,const SpoutSettings& settings,bool full) {
    if(!w || !h || !source) return 0;
    std::lock_guard lock(worker_->mutex); auto& s=*state_;
    full=full || !s.initialized || s.inputWidth!=w || s.inputHeight!=h || !(settings==s.lastInput);
    if(regions.empty() && !full) return s.sequence;
    s.pending.width=w; s.pending.height=h; s.pending.pixels.resize(static_cast<size_t>(w)*h);
    std::vector<SpoutRect> dirty=full?std::vector<SpoutRect>{{0,0,w,h}}:regions;
    if(full) s.dirty.clear();
    for(auto r:dirty) {
        r.left=std::min(r.left,w); r.right=std::min(r.right,w); r.top=std::min(r.top,h); r.bottom=std::min(r.bottom,h); if(r.Empty()) continue;
        for(unsigned y=r.top;y<r.bottom;++y) std::memcpy(s.pending.pixels.data()+static_cast<size_t>(y)*w+r.left,source+static_cast<size_t>(y)*w+r.left,(r.right-r.left)*4);
        s.stats.rawPixels+=static_cast<std::uint64_t>(r.right-r.left)*(r.bottom-r.top);
        const bool covered=std::any_of(s.dirty.begin(),s.dirty.end(),[&](const SpoutRect& old){return old.left<=r.left && old.top<=r.top && old.right>=r.right && old.bottom>=r.bottom;});
        if(!covered) { std::erase_if(s.dirty,[&](const SpoutRect& old){return r.left<=old.left && r.top<=old.top && r.right>=old.right && r.bottom>=old.bottom;}); s.dirty.push_back(r); }
    }
    if(s.dirty.size()>128) {
        HRGN unionRegion=CreateRectRgn(0,0,0,0);
        for(const auto& r:s.dirty) { HRGN part=CreateRectRgn(r.left,r.top,r.right,r.bottom); CombineRgn(unionRegion,unionRegion,part,RGN_OR); DeleteObject(part); }
        std::vector<BYTE> data(GetRegionData(unionRegion,0,nullptr));
        if(!data.empty() && GetRegionData(unionRegion,static_cast<DWORD>(data.size()),reinterpret_cast<RGNDATA*>(data.data()))) {
            const auto* region=reinterpret_cast<const RGNDATA*>(data.data()); const auto* rects=reinterpret_cast<const RECT*>(region->Buffer); s.dirty.clear();
            for(DWORD i=0;i<region->rdh.nCount;++i) s.dirty.push_back({static_cast<unsigned>(rects[i].left),static_cast<unsigned>(rects[i].top),static_cast<unsigned>(rects[i].right),static_cast<unsigned>(rects[i].bottom)});
        }
        DeleteObject(unionRegion);
    }
    s.options=settings; s.lastInput=settings; s.inputWidth=w; s.inputHeight=h; s.initialized=true; s.hasPending=true; ++s.sequence; ++s.stats.queued;
    worker_->wake.notify_one(); return s.sequence;
}
void SpoutChannel::Resend() { { std::lock_guard lock(worker_->mutex); if(state_->initialized) state_->resend=true; } worker_->wake.notify_one(); }
bool SpoutChannel::WaitIdle(unsigned ms) { std::unique_lock lock(worker_->mutex); return worker_->wake.wait_for(lock,std::chrono::milliseconds(ms),[&]{return !state_->hasPending && !state_->processing;}); }
SpoutChannel::Stats SpoutChannel::Statistics(bool reset) const { std::lock_guard lock(worker_->mutex); auto stats=state_->stats; if(reset) state_->stats={}; return stats; }
std::wstring SpoutChannel::Status() const { std::lock_guard lock(worker_->mutex); return state_->status; }
}
