#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "SpoutPixels.h"

// CPU BGRA images; caller chooses straight or premultiplied alpha. No worker conversion.
// A channel owns at most one pending frame. All DirectX/Spout work stays off the UI thread.
namespace rm {
class SpoutWorker;
class SpoutChannel {
public:
    struct Frame { unsigned width=0,height=0; std::vector<std::uint32_t> pixels; };
    explicit SpoutChannel(std::string name);
    ~SpoutChannel();
    SpoutChannel(const SpoutChannel&)=delete;
    SpoutChannel& operator=(const SpoutChannel&)=delete;
    void Submit(Frame frame);
    std::uint64_t SubmitRegions(unsigned width,unsigned height,const std::uint32_t* source,const std::vector<SpoutRect>& dirty,const SpoutSettings& settings,bool full=false);
    void Resend();
    bool WaitIdle(unsigned milliseconds=2000);
    struct Event { std::uint64_t sequence=0,cycles=0; double conversionUs=0,uploadUs=0,sendUs=0,totalUs=0; bool textureCreated=false,senderCreated=false,retry=false; };
    struct Stats { std::uint64_t frames=0,queued=0,rawPixels=0,convertedPixels=0,uploadBytes=0; double conversionUs=0,uploadUs=0,sendUs=0; std::vector<Event> outliers; };
    Stats Statistics(bool reset=false) const;
    std::wstring Status() const;
private:
    struct State;
    std::shared_ptr<State> state_;
    std::shared_ptr<SpoutWorker> worker_;
    friend class SpoutWorker;
};
}
