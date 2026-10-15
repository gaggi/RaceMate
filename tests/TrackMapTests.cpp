#include "Core.h"
#include "TrackMapLoader.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>

using namespace rm::trackmap;
namespace {
void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
std::string Read(const std::filesystem::path& path) { std::ifstream stream(path,std::ios::binary); return {std::istreambuf_iterator<char>(stream),{}}; }
std::string Svg(std::string content,const std::string& transform={}) {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 240 180\"><g transform=\""+transform+"\">"+content+"</g></svg>";
}
const std::string Band="<path fill-rule=\"evenodd\" d=\"M10 10H210V110H10Z M20 20V100H200V20Z\"/>";
const std::string Stripe="<rect x=\"98\" y=\"2\" width=\"4\" height=\"26\"/>";
const std::string Arrow="<path d=\"M80 45H110V35L135 50L110 65V55H80Z\"/>";
void UnitTests() {
    const auto session=rm::ParseSession("WeekendInfo:\n TrackID: 353\n TrackName: limerock 2019 gp\n TrackConfigName: Grand Prix\n",0);
    Require(session.trackId==353 && session.trackName=="limerock 2019 gp" && session.trackConfig=="Grand Prix","layout identity parsed from WeekendInfo");
    Require(FindSource(353,session.trackName) && FindSource(166,"okayama full") && FindSource(167,"okayama short"),"known exact layout sources");
    Require(!FindSource(353,"limerock 2019 chicanes") && !FindSource(999999) && !FindSource(-1),"wrong and unknown layouts never pick another map");
    auto map=Prepare(Svg(Band),Svg(Stripe+Arrow));
    if(!map) throw std::runtime_error(map.error);
    auto& g=map.geometry;
    Require(g.centered && g.direction==1,"derive center path and forward arrow without track-specific data");
    const auto start=g.At(0), later=g.At(.01), end=g.At(1);
    Require(std::abs(start.x-100)<.5 && std::abs(start.y-15)<.5,"zero percent crosses the start stripe at the middle of the track");
    Require(later.x>start.x && std::abs(later.y-start.y)<.5,"progress follows the direction arrow");
    Require(std::hypot(start.x-end.x,start.y-end.y)<1e-7,"one lap wraps exactly to start");
    const auto before=g.At(.9999), after=g.At(.0001);
    Require(std::hypot(before.x-after.x,before.y-after.y)<g.length*.001,"lap wrap does not travel the long way around");
    const auto reverse=Prepare(Svg(Band),Svg(Stripe+"<g transform=\"translate(200 0) scale(-1 1)\">"+Arrow+"</g>"));
    Require(reverse && reverse.geometry.direction==-1 && reverse.geometry.At(.01).x<reverse.geometry.At(0).x,"reverse arrow changes direction");
    const std::string transform="translate(230 0) rotate(90)";
    const auto rotated=Prepare(Svg(Band,transform),Svg(Stripe+Arrow,transform));
    Require(rotated && rotated.geometry.direction==1 && rotated.geometry.At(.01).y>rotated.geometry.At(0).y,"transforms apply consistently to map, line and arrow");
    const auto unknown=Prepare(Svg(Band),Svg(Stripe));
    Require(unknown && unknown.geometry.direction==0,"missing arrow leaves direction explicitly unresolved");
    Require(!Prepare(Svg(Band),Svg(Arrow)),"no start stripe means no fabricated zero point");
    Require(!Prepare("<html>denied</html>",Svg(Stripe+Arrow)),"non SVG download rejected");
    Require(!Prepare(Svg("<path d=\"M10 10H100\"/>"),Svg(Stripe+Arrow)),"open racing path is not silently closed");
    Require(!Prepare(std::string(1024*1024+1,'x'),Svg(Stripe+Arrow)),"oversized assets rejected");
    auto mismatched=Svg(Stripe+Arrow); mismatched.replace(mismatched.find("240 180"),7,"240 190");
    Require(!Prepare(Svg(Band),mismatched),"different layer viewports cannot be overlaid");
    const auto optional=Prepare(Svg(Band),Svg(Stripe+Arrow),"<html>missing pitroad</html>");
    Require(optional && optional.geometry.pit.shapes.empty(),"pit layer failure does not discard the active circuit");
    const auto line=Prepare(Svg(Band),Svg("<line x1=\"100\" y1=\"2\" x2=\"100\" y2=\"28\" stroke=\"red\"/>"+Arrow));
    Require(line && line.geometry.direction==1 && std::abs(line.geometry.At(0).x-100)<.1,"stroked line start marker is supported too");
    const auto stroke=Prepare(Svg("<rect x=\"10\" y=\"10\" width=\"200\" height=\"100\" fill=\"none\" stroke=\"white\"/>"),Svg(Stripe+Arrow));
    Require(stroke && !stroke.geometry.centered && stroke.geometry.direction==1,"a single closed stroked centerline needs no paired boundaries");
    const auto* source=FindSource(353);
    const std::atomic_bool cancelled=true;
    Require(!Load(*source,"unused-cache",cancelled).map,"cancelled loads perform no work");
    const auto cache=std::filesystem::temp_directory_path()/("racemate-map-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto directory=cache/"353";
    std::filesystem::create_directories(directory);
    { std::ofstream(directory/"source.txt",std::ios::binary)<<source->folder<<"\n"; std::ofstream(directory/"active.svg",std::ios::binary)<<Svg(Band); std::ofstream(directory/"start-finish.svg",std::ios::binary)<<Svg(Stripe+Arrow); }
    const std::atomic_bool running=false;
    const auto cached=Load(*source,cache,running);
    Require(cached.map && cached.fromCache && cached.cached,"valid identity-tagged cache loads without a network call");
    for(const char* file:{"source.txt","active.svg","start-finish.svg"}) std::filesystem::remove(directory/file);
    std::filesystem::remove(directory); std::filesystem::remove(cache);
}
int AssetCheck(const std::filesystem::path& directory) {
    const auto result=Prepare(Read(directory/L"active.svg"),Read(directory/L"start-finish.svg"),Read(directory/L"pitroad.svg"));
    if(!result) throw std::runtime_error(result.error);
    const auto& g=result.geometry;
    std::cout<<"centered="<<g.centered<<" direction="<<g.direction<<" length="<<g.length<<" start="<<g.start/g.length<<"\n";
    for(double pct:{0.,.01,.1,.25,.5,.75,.99}) { const auto p=g.At(pct); std::cout<<pct<<": "<<p.x<<", "<<p.y<<"\n"; }
    Require(g.direction!=0,"official start/finish arrow resolves direction");
    return 0;
}
}
int main(int argc,char** argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--assets") return AssetCheck(argv[2]);
        if(argc==4 && std::string(argv[1])=="--load") {
            const auto* source=FindSource(std::stoi(argv[2])); Require(source!=nullptr,"known source required");
            const std::atomic_bool cancelled=false; auto result=Load(*source,argv[3],cancelled);
            if(!result.map) throw std::runtime_error(result.map.error);
            std::cout<<"fromCache="<<result.fromCache<<" cached="<<result.cached<<" direction="<<result.map.geometry.direction<<"\n";
            return result.map.geometry.direction?0:2;
        }
        UnitTests(); std::cout<<"PASS: track identity, SVG projection, direction, transforms, wrap and invalid assets\n";
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<"\n"; return 1; }
}
