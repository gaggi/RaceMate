#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>
namespace rm {
struct SpoutRect { unsigned left=0,top=0,right=0,bottom=0; bool Empty() const { return left>=right || top>=bottom; } };
struct SpoutSettings {
    bool perPixel=true,premultiplied=true;
    unsigned opacity=100,radius=0,panels=1,visiblePanels=3;
    bool operator==(const SpoutSettings&) const=default;
};
// Persistent worker-owned export image. Input is BGRA, premultiplied when perPixel.
class SpoutPixels {
public:
    SpoutRect Update(unsigned width,unsigned height,const std::uint32_t* source,const std::vector<SpoutRect>& dirty,const SpoutSettings& settings);
    const std::vector<std::uint32_t>& Image() const { return image_; }
    unsigned Width() const { return width_; } unsigned Height() const { return height_; }
    static unsigned Unpremultiply(unsigned channel,unsigned alpha);
private:
    unsigned width_=0,height_=0;
    SpoutSettings settings_{};
    bool initialized_=false;
    std::array<unsigned,256> scale_{};
    std::vector<std::uint32_t> image_;
    std::vector<size_t> cornerZeros_;
    void BuildCorners();
};
}
