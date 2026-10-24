#include "Core.h"
#include "TrackMapLoader.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <numbers>
#include <sstream>

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
std::string BridgeRibbon() {
    // An original figure-eight ribbon with a gap around one of its two
    // traversals of the crossing. One polygon, two end caps, no inner ring.
    constexpr int N=512; constexpr double Gap=.09, HalfWidth=3;
    std::vector<Point> left,right;
    for(int i=0;i<=N;++i) {
        const double t=Gap+(2*std::numbers::pi-2*Gap)*i/N;
        const Point p{110+90*std::sin(t),80+55*std::sin(2*t)};
        const Point tangent{90*std::cos(t),110*std::cos(2*t)}; const double length=std::hypot(tangent.x,tangent.y);
        const Point normal{-tangent.y/length*HalfWidth,tangent.x/length*HalfWidth};
        left.push_back({p.x+normal.x,p.y+normal.y}); right.push_back({p.x-normal.x,p.y-normal.y});
    }
    std::ostringstream svg; svg<<"<path d=\"";
    for(size_t i=0;i<left.size();++i) svg<<(i?"L":"M")<<left[i].x<<","<<left[i].y;
    for(auto it=right.rbegin();it!=right.rend();++it) svg<<"L"<<it->x<<","<<it->y;
    svg<<"Z\"/>"; return svg.str();
}
void BridgeTests() {
    const auto active=Svg(BridgeRibbon());
    const auto finish=Svg("<line x1=\"190\" y1=\"80\" x2=\"210\" y2=\"80\" stroke=\"red\"/><g transform=\"translate(175 180) rotate(-90)\">"+Arrow+"</g>");
    const auto map=Prepare(active,finish);
    if(!map) throw std::runtime_error("bridge ribbon: "+map.error);
    const auto& g=map.geometry;
    Require(g.centered && g.bridged && g.direction!=0,"bridge ribbon has a centerline rather than a perimeter lap");
    double expected=0; Point previous{110,80};
    for(int i=1;i<=4096;++i) { const double t=2*std::numbers::pi*i/4096; const Point p{110+90*std::sin(t),80+55*std::sin(2*t)}; expected+=std::hypot(p.x-previous.x,p.y-previous.y); previous=p; }
    Require(std::abs(g.length/expected-1)<.025,"one lap uses the ribbon centerline length, not both road edges");
    Require(std::hypot(g.At(0).x-200,g.At(0).y-80)<2,"start/finish remains aligned on the recovered ribbon");
    Require(g.At(.5).x<24,"half a lap reaches the opposite lobe instead of returning along the same road edge");
    Require(std::hypot(g.At(.25).x-110,g.At(.25).y-80)<3 && std::hypot(g.At(.75).x-110,g.At(.75).y-80)<3,"both crossing traversals are present in one lap");
    Require(g.At(.26).x<g.At(.24).x && g.At(.76).x>g.At(.74).x,"crossing traversals follow their respective branches without turning back");
    Require(std::hypot(g.At(.749).x-g.At(.751).x,g.At(.749).y-g.At(.751).y)<g.length*.003,"car motion stays continuous across the visual bridge gap");
    Require(map.geometry.active.shapes.size()==1 && map.geometry.active.shapes[0].paths.size()==1,"original gapped SVG remains unchanged for drawing");
    const auto rotated=Prepare(Svg(BridgeRibbon(),"translate(230 0) rotate(90)"),Svg("<line x1=\"190\" y1=\"80\" x2=\"210\" y2=\"80\" stroke=\"red\"/><g transform=\"translate(175 180) rotate(-90)\">"+Arrow+"</g>","translate(230 0) rotate(90)"));
    Require(rotated && rotated.geometry.bridged && std::abs(rotated.geometry.length/g.length-1)<.01,"bridge recovery is independent of SVG orientation");
    const auto openFinish=Svg("<line x1=\"100\" y1=\"35\" x2=\"100\" y2=\"55\" stroke=\"red\"/>"+Arrow);
    Require(!Prepare(Svg("<rect x=\"20\" y=\"40\" width=\"180\" height=\"6\"/>"),openFinish),"ordinary open straight ribbon is not joined into an invented circuit");
    Require(!Prepare(Svg("<path d=\"M20 77H107V20H113V77H200V83H20Z\"/>"),Svg("<line x1=\"100\" y1=\"70\" x2=\"100\" y2=\"90\" stroke=\"red\"/>"+Arrow)),"ambiguous junction cannot silently lose a branch");
}
void LabelTests() {
    const auto text=Svg("<style>.corner{font-size:20px;} .hidden{display:none;}</style>"
        "<g transform=\"translate(10 20)\"><text transform=\"matrix(1 0 0 1 30 40)\" class=\"corner\">12</text>"
        "<text x=\"60\" y=\"70\" text-anchor=\"middle\">Moss &#x2018;S&#x2019; &amp; Co</text>"
        "<text class=\"hidden\">99</text><g visibility=\"hidden\"><text>98</text></g></g>");
    const auto map=Prepare(Svg(Band),Svg(Stripe+Arrow),{},text);
    Require(map && map.geometry.labels.size()==2,"official text layer and CSS visibility are supported");
    const auto& number=map.geometry.labels[0]; const auto& name=map.geometry.labels[1];
    Require(number.number && number.text=="12" && number.size==20 && std::abs(number.position.x-40)<.01 && std::abs(number.position.y-60)<.01,"number, CSS font size and nested SVG transforms");
    Require(!name.number && name.anchor==1 && name.text=="Moss ‘S’ & Co","UTF-8, XML numeric/named entities and text anchor");
    const auto plain=Prepare(Svg(Band),Svg(Stripe+Arrow));
    Require(plain.geometry.length==map.geometry.length && plain.geometry.start==map.geometry.start && plain.geometry.At(.37).x==map.geometry.At(.37).x,"labels do not affect the route or car projection");
    const auto spans=Prepare(Svg(Band),Svg(Stripe+Arrow),{},Svg("<text x=\"10\" y=\"30\" font-size=\"16\"><tspan>Hair</tspan><tspan x=\"10\" dy=\"18\">Pin</tspan></text>"));
    Require(spans && spans.geometry.labels.size()==2 && spans.geometry.labels[1].text=="Pin" && spans.geometry.labels[1].position.y==48,"positioned multiline tspan labels");
    auto mismatch=text; mismatch.replace(mismatch.find("240 180"),7,"240 190");
    const auto wrong=Prepare(Svg(Band),Svg(Stripe+Arrow),{},mismatch);
    Require(wrong && wrong.geometry.labels.empty(),"different turns viewport is ignored without losing map");
    Require(Prepare(Svg(Band),Svg(Stripe+Arrow),{},"<html>missing</html>").geometry.labels.empty(),"missing optional labels do not remove map");
    {
        // The background and other-configuration layers: parsed when present, left out when broken, never the map.
        const auto layers=Prepare(Svg(Band),Svg(Stripe+Arrow),{},{},Svg("<rect x=\"0\" y=\"0\" width=\"240\" height=\"180\"/>"),Svg("<rect x=\"30\" y=\"30\" width=\"60\" height=\"20\"/>"));
        Require(layers && !layers.geometry.background.shapes.empty() && !layers.geometry.inactive.shapes.empty(),"background and other configurations are parsed");
        Require(layers.geometry.length==plain.geometry.length && layers.geometry.At(.37).x==plain.geometry.At(.37).x,"extra layers do not affect the route");
        const auto broken=Prepare(Svg(Band),Svg(Stripe+Arrow),{},{},"<html>missing</html>",mismatch);
        Require(broken && broken.geometry.background.shapes.empty() && broken.geometry.inactive.shapes.empty(),"broken or misfitting extra layers are left out without losing the map");
    }
    Require(Prepare(Svg(Band),Svg(Stripe+Arrow),{},std::string(1024*1024+1,'x')).geometry.labels.empty(),"oversized labels rejected independently");
    std::string many,deep;
    for(int i=0;i<300;++i) many+="<text x=\"10\" y=\"20\">1</text>";
    Require(Prepare(Svg(Band),Svg(Stripe+Arrow),{},Svg(many)).geometry.labels.size()==256,"label count is bounded");
    for(int i=0;i<80;++i) deep+="<g>";
    deep+="<text>1</text>";
    for(int i=0;i<80;++i) deep+="</g>";
    Require(Prepare(Svg(Band),Svg(Stripe+Arrow),{},Svg(deep)).geometry.labels.empty(),"excessive nesting is rejected safely");
}
void PitLaneTests() {
    // A pit lane drawn just inside the start straight: a car in the pits sits on it there, elsewhere on the track.
    const auto map=Prepare(Svg(Band),Svg(Stripe+Arrow),Svg("<rect x=\"60\" y=\"24\" width=\"80\" height=\"2\"/>"));
    Require(map && !map.geometry.pitPaths.empty(),"pit drawing kept as outlines");
    const auto& g=map.geometry; const auto start=g.PitAt(0);
    Require(std::abs(start.x-100)<12 && start.y>=23.5 && start.y<=26.5,"a car in the pits at 0 % sits on the pit lane beside the straight");
    const auto far=g.PitAt(.5), track=g.At(.5);
    Require(far.x==track.x && far.y==track.y,"far from the pit lane the car stays on the track");
    const auto plain=Prepare(Svg(Band),Svg(Stripe+Arrow));
    Require(plain && plain.geometry.PitAt(0).x==plain.geometry.At(0).x && plain.geometry.PitAt(0).y==plain.geometry.At(0).y,"without a pit drawing the track point");
    // Where a car in its pit stall points: along the drawn pit lane, the way the track runs beside it.
    constexpr double pi=std::numbers::pi;
    Require(std::abs(g.PitHeading(0)-pi/2)<.02 && std::isnan(g.PitHeading(.5)) && std::isnan(plain.geometry.PitHeading(0)),"pit lane direction beside the stall");
    const auto angled=Prepare(Svg(Band),Svg(Stripe+Arrow),Svg("<rect x=\"60\" y=\"24\" width=\"80\" height=\"2\" transform=\"rotate(10 100 25)\"/>"));
    Require(angled && std::abs(angled.geometry.PitHeading(0)-(pi/2+10*pi/180))<.02,"a pit lane drawn at an angle gives that angle");
}
void UnitTests() {
    LabelTests();
    PitLaneTests();
    BridgeTests();
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
    const auto band=Prepare(Svg(Band),Svg(Stripe+Arrow));
    Require(band && std::abs(band.geometry.Heading(0)-std::numbers::pi/2)<.05 && std::abs(band.geometry.Heading(.5)-3*std::numbers::pi/2)<.05,"heading follows the direction of travel, clockwise from the top of the map");
    Require(TrackFolder(448)=="indianapolis" && TrackFolder(253)==TrackFolder(249) && TrackFolder(999999).empty(),"layouts know their track by its CDN folder");
    const auto* source=FindSource(353);
    const std::atomic_bool cancelled=true;
    Require(!Load(*source,"unused-cache",cancelled).map,"cancelled loads perform no work");
    const auto cache=std::filesystem::temp_directory_path()/("racemate-map-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto directory=cache/"353";
    std::filesystem::create_directories(directory);
    { std::ofstream(directory/"source.txt",std::ios::binary)<<source->folder<<"\n"; std::ofstream(directory/"active.svg",std::ios::binary)<<Svg(Band); std::ofstream(directory/"start-finish.svg",std::ios::binary)<<Svg(Stripe+Arrow);
      // Already asked once for the background and other configurations, which this layout does not have.
      std::ofstream(directory/"layers-attempt.txt",std::ios::binary)<<source->folder<<"\n"; }
    const std::atomic_bool running=false;
    const auto cached=Load(*source,cache,running);
    Require(cached.map && cached.fromCache && cached.cached,"valid identity-tagged cache loads without a network call");
    { std::ofstream(directory/"turns.svg",std::ios::binary)<<Svg("<text x=\"20\" y=\"30\">1</text><text x=\"40\" y=\"50\">Bend</text>"); }
    const auto labeled=Load(*source,cache,running,true);
    Require(labeled.fromCache && labeled.map && labeled.map.geometry.labels.size()==2,"cached corner labels load without network");
    Require(Load(*source,cache,running).map.geometry.labels.empty(),"disabled labels do not parse the optional text layer");
    std::filesystem::remove(directory/"turns.svg");
    { std::ofstream(directory/"turns-attempt.txt",std::ios::binary)<<source->folder<<"\n"; }
    const auto missingLabels=Load(*source,cache,running,true);
    Require(missingLabels.fromCache && missingLabels.map && missingLabels.map.geometry.labels.empty(),"negative cache keeps geometry available without repeated failed downloads");
    std::error_code error; std::filesystem::remove_all(cache,error);
}
int AssetCheck(const std::filesystem::path& directory) {
    const auto result=Prepare(Read(directory/L"active.svg"),Read(directory/L"start-finish.svg"),Read(directory/L"pitroad.svg"),Read(directory/L"turns.svg"));
    if(!result) throw std::runtime_error(result.error);
    const auto& g=result.geometry;
    std::cout<<"centered="<<g.centered<<" bridged="<<g.bridged<<" direction="<<g.direction<<" length="<<g.length<<" start="<<g.start/g.length<<" labels="<<g.labels.size()<<"\n";
    for(double pct:{0.,.01,.1,.25,.5,.75,.99}) { const auto p=g.At(pct); std::cout<<pct<<": "<<p.x<<", "<<p.y<<"  heading "<<g.Heading(pct)*180/std::numbers::pi<<", pit "<<g.PitHeading(pct)*180/std::numbers::pi<<"\n"; }
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
