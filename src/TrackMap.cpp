#include "TrackMap.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <memory>
#include <map>
#include <sstream>
#include <numbers>
#include <queue>
#include <stdexcept>
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define NANOSVG_IMPLEMENTATION
#include "third_party/nanosvg/nanosvg.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace rm::trackmap {
namespace {
constexpr size_t MaxSvgBytes=1024*1024, MaxPoints=100000;
constexpr int RouteSamples=2048;
Point operator+(Point a,Point b) { return {a.x+b.x,a.y+b.y}; }
Point operator-(Point a,Point b) { return {a.x-b.x,a.y-b.y}; }
Point operator*(Point a,double b) { return {a.x*b,a.y*b}; }
double Dot(Point a,Point b) { return a.x*b.x+a.y*b.y; }
double Cross(Point a,Point b) { return a.x*b.y-a.y*b.x; }
double Length(Point a) { return std::hypot(a.x,a.y); }
Point Unit(Point a) { const double n=Length(a); return n>0?a*(1/n):Point{}; }
double Wrap(double n) { return n-std::floor(n); }
void AddPoint(Path& path,Point p,size_t& count) {
    if(!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x)>1e7 || std::abs(p.y)>1e7 || ++count>MaxPoints)
        throw std::runtime_error("SVG geometry exceeds supported limits");
    if(path.points.empty() || Length(p-path.points.back())>1e-5) path.points.push_back(p);
}
void Flatten(Path& path,Point a,Point b,Point c,Point d,int depth,size_t& count) {
    // Control-point distance to the chord; adaptive rather than a fixed count per curve.
    const Point chord=d-a;
    const double n=Length(chord), flat=n>1e-8?std::max(std::abs(Cross(b-a,chord)),std::abs(Cross(c-a,chord)))/n:std::max(Length(b-a),Length(c-a));
    if(depth>=12 || flat<=.15) { AddPoint(path,d,count); return; }
    const Point ab=(a+b)*.5, bc=(b+c)*.5, cd=(c+d)*.5, abc=(ab+bc)*.5, bcd=(bc+cd)*.5, mid=(abc+bcd)*.5;
    Flatten(path,a,ab,abc,mid,depth+1,count); Flatten(path,mid,bcd,cd,d,depth+1,count);
}
Layer Parse(std::string svg) {
    if(svg.empty() || svg.size()>MaxSvgBytes || svg.find("<svg")==std::string::npos)
        throw std::runtime_error("Missing or invalid SVG");
    std::unique_ptr<NSVGimage,decltype(&nsvgDelete)> image(nsvgParse(svg.data(),"px",96),nsvgDelete);
    if(!image || !std::isfinite(image->width) || !std::isfinite(image->height) || image->width<=0 || image->height<=0)
        throw std::runtime_error("SVG has no valid viewport");
    Layer layer{image->width,image->height}; size_t count=0;
    for(const NSVGshape* s=image->shapes;s;s=s->next) {
        if(!(s->flags&NSVG_FLAGS_VISIBLE)) continue;
        Shape shape; shape.evenOdd=s->fillRule==NSVG_FILLRULE_EVENODD; shape.filled=s->fill.type!=NSVG_PAINT_NONE;
        shape.strokeWidth=s->stroke.type==NSVG_PAINT_NONE?0:s->strokeWidth;
        for(const NSVGpath* p=s->paths;p;p=p->next) {
            if(p->npts<4 || p->npts>static_cast<int>(MaxPoints)) continue;
            Path path; path.closed=p->closed!=0;
            auto point=[&](int i) { return Point{p->pts[2*i],p->pts[2*i+1]}; };
            AddPoint(path,point(0),count);
            for(int i=0;i+3<p->npts;i+=3) Flatten(path,point(i),point(i+1),point(i+2),point(i+3),0,count);
            if(path.closed) AddPoint(path,path.points.front(),count);
            if(path.points.size()>=2) shape.paths.push_back(std::move(path));
        }
        if(!shape.paths.empty()) layer.shapes.push_back(std::move(shape));
    }
    if(layer.shapes.empty()) throw std::runtime_error("SVG contains no supported paths");
    return layer;
}
#include "TrackMapLabels.inc"
std::vector<double> Distances(const std::vector<Point>& p) {
    std::vector<double> d(p.size());
    for(size_t i=1;i<p.size();++i) d[i]=d[i-1]+Length(p[i]-p[i-1]);
    return d;
}
Point Sample(const std::vector<Point>& p,const std::vector<double>& d,double distance) {
    const auto it=std::upper_bound(d.begin(),d.end(),distance);
    const size_t i=std::clamp<size_t>(static_cast<size_t>(it-d.begin()),1,d.size()-1);
    const double span=d[i]-d[i-1];
    return p[i-1]+(p[i]-p[i-1])*(span>0?(distance-d[i-1])/span:0);
}
Point Nearest(const std::vector<Point>& points,Point p,double* distance=nullptr) {
    Point best{}; double minimum=std::numeric_limits<double>::max();
    for(size_t i=1;i<points.size();++i) {
        const Point a=points[i-1], delta=points[i]-a;
        const double n=Dot(delta,delta), t=n>0?std::clamp(Dot(p-a,delta)/n,0.,1.):0;
        const Point q=a+delta*t; const double error=Dot(q-p,q-p);
        if(error<minimum) { minimum=error; best=q; }
    }
    if(distance) *distance=std::sqrt(minimum);
    return best;
}
bool Inside(const std::vector<Point>& ring,Point p) {
    bool inside=false;
    for(size_t i=1;i<ring.size();++i) {
        const Point a=ring[i-1], b=ring[i];
        if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x) inside=!inside;
    }
    return inside;
}
// A bridge can be drawn as a gap in a filled ribbon. Its one closed polygon
// walks down one road edge, around an end cap and back along the other edge;
// using that contour as the lap sends cars backwards halfway through the lap.
// Recover the ribbon's medial path in a bounded, temporary raster instead.
// This runs only during preparation; drawing still uses the original SVG.
std::vector<Point> RibbonRoute(const Path& outline) {
    Point low{1e20,1e20},high{-1e20,-1e20};
    for(Point p:outline.points) { low.x=std::min(low.x,p.x); low.y=std::min(low.y,p.y); high.x=std::max(high.x,p.x); high.y=std::max(high.y,p.y); }
    const double span=std::max(high.x-low.x,high.y-low.y);
    if(span<=0) throw std::runtime_error("Degenerate ribbon");
    double twiceArea=0;
    for(size_t i=1;i<outline.points.size();++i) twiceArea+=Cross(outline.points[i-1],outline.points[i]);
    const double averageWidth=std::abs(twiceArea)/Distances(outline.points).back();
    if(averageWidth<=0 || averageWidth>span*.08) throw std::runtime_error("Filled shape is not a thin racing ribbon");
    constexpr int Resolution=2048, Margin=3;
    const double scale=std::min((Resolution-2*Margin-1)/span,24/averageWidth);
    const int width=static_cast<int>(std::ceil((high.x-low.x)*scale))+2*Margin+1;
    const int height=static_cast<int>(std::ceil((high.y-low.y)*scale))+2*Margin+1;
    std::vector<Point> polygon; polygon.reserve(outline.points.size());
    for(Point p:outline.points) polygon.push_back({(p.x-low.x)*scale+Margin,(p.y-low.y)*scale+Margin});
    std::vector<unsigned char> mask(static_cast<size_t>(width)*height);
    std::vector<int> pixels; std::vector<double> crossings;
    for(int y=Margin;y<height-Margin;++y) {
        crossings.clear(); const double scan=y+.5;
        for(size_t i=1;i<polygon.size();++i) {
            const Point a=polygon[i-1],b=polygon[i];
            if((a.y>scan)!=(b.y>scan)) crossings.push_back(a.x+(scan-a.y)*(b.x-a.x)/(b.y-a.y));
        }
        std::sort(crossings.begin(),crossings.end());
        for(size_t i=1;i<crossings.size();i+=2) {
            const int from=std::max(Margin,static_cast<int>(std::ceil(crossings[i-1]-.5)));
            const int to=std::min(width-Margin-1,static_cast<int>(std::floor(crossings[i]-.5)));
            for(int x=from;x<=to;++x) { const int index=y*width+x; mask[index]=1; pixels.push_back(index); }
        }
    }
    if(pixels.empty()) throw std::runtime_error("Ribbon has no interior");
    // Zhang-Suen thinning preserves 8-connected topology and endpoints. Visit
    // only foreground pixels, rather than scanning the whole image every pass.
    const int neighbors[]={-width,-width+1,1,width+1,width,width-1,-1,-width-1};
    std::vector<int> remove;
    bool changed=true; int passes=0;
    while(changed && passes++<Resolution) {
        changed=false;
        for(int phase=0;phase<2;++phase) {
            remove.clear();
            for(int index:pixels) {
                std::array<int,8> n{}; int count=0,transitions=0;
                for(int k=0;k<8;++k) { n[k]=mask[index+neighbors[k]]; count+=n[k]; }
                if(count<2 || count>6) continue;
                for(int k=0;k<8;++k) transitions+=!n[k] && n[(k+1)%8];
                if(transitions!=1) continue;
                if(!phase?(n[0]*n[2]*n[4] || n[2]*n[4]*n[6]):(n[0]*n[2]*n[6] || n[0]*n[4]*n[6])) continue;
                remove.push_back(index);
            }
            if(!remove.empty()) {
                changed=true;
                for(int index:remove) mask[index]=0;
                pixels.erase(std::remove_if(pixels.begin(),pixels.end(),[&](int index){return !mask[index];}),pixels.end());
            }
        }
    }
    if(changed || pixels.size()<16) throw std::runtime_error("Ribbon centerline could not be resolved");
    std::vector<int> node(mask.size(),-1);
    for(size_t i=0;i<pixels.size();++i) node[pixels[i]]=static_cast<int>(i);
    struct Walk { int farthest=-1; size_t reached=0; std::vector<int> parent; std::vector<double> distances; };
    auto walk=[&](const std::vector<int>& starts) {
        Walk result; result.farthest=starts.front(); result.parent.assign(pixels.size(),-1);
        auto& distances=result.distances; distances.assign(pixels.size(),std::numeric_limits<double>::infinity());
        using Entry=std::pair<double,int>;
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
        for(int start:starts) { distances[start]=0; queue.emplace(0,start); }
        while(!queue.empty()) {
            const auto [distance,current]=queue.top(); queue.pop();
            if(distance>distances[current]) continue;
            ++result.reached;
            if(distance>distances[result.farthest]) result.farthest=current;
            for(int k=0;k<8;++k) {
                const int next=node[pixels[current]+neighbors[k]];
                if(next<0) continue;
                const double candidate=distance+(k%2?std::numbers::sqrt2:1.);
                if(candidate<distances[next]) { distances[next]=candidate; result.parent[next]=current; queue.emplace(candidate,next); }
            }
        }
        return result;
    };
    const auto first=walk({0});
    if(first.reached!=pixels.size()) throw std::runtime_error("Ribbon contains disconnected centerlines");
    const auto diameter=walk({first.farthest});
    std::vector<Point> path; std::vector<int> pathNodes;
    for(int current=diameter.farthest;current>=0;current=diameter.parent[current]) {
        const int index=pixels[current];
        path.push_back({low.x+(index%width+.5-Margin)/scale,low.y+(index/width+.5-Margin)/scale});
        pathNodes.push_back(current);
    }
    // A ribbon is essentially one path. Reject junctions/large loops rather than
    // silently keeping only one lobe of a crossing or an unrelated piece.
    const auto coverage=walk(pathNodes);
    if(coverage.distances[coverage.farthest]>averageWidth*scale*2) throw std::runtime_error("Ribbon centerline has ambiguous branches");
    // Simplify raster stair steps before measuring length; otherwise diagonal
    // sections would artificially inflate the lap distance.
    std::vector<unsigned char> keep(path.size()); keep.front()=keep.back()=1;
    std::vector<std::pair<size_t,size_t>> work{{0,path.size()-1}};
    const double tolerance=1.1/scale;
    while(!work.empty()) {
        const auto [from,to]=work.back(); work.pop_back();
        const Point a=path[from],delta=path[to]-a; const double length2=Dot(delta,delta);
        double maximum=tolerance*tolerance; size_t best=from;
        for(size_t i=from+1;i<to;++i) {
            const double t=length2>0?std::clamp(Dot(path[i]-a,delta)/length2,0.,1.):0;
            const Point difference=path[i]-(a+delta*t); const double error=Dot(difference,difference);
            if(error>maximum) { maximum=error; best=i; }
        }
        if(best!=from) { keep[best]=1; work.emplace_back(from,best); work.emplace_back(best,to); }
    }
    std::vector<Point> simplified;
    for(size_t i=0;i<path.size();++i) if(keep[i]) simplified.push_back(path[i]);
    const auto distance=Distances(simplified); const double length=distance.back();
    const Point gap=simplified.front()-simplified.back(); const double gapLength=Length(gap);
    const double window=std::min(length*.03,std::max(averageWidth*2,12/scale));
    const Point enter=simplified.back()-Sample(simplified,distance,length-window);
    const Point leave=Sample(simplified,distance,window)-simplified.front();
    if(gapLength<=0 || gapLength>length*.08 || gapLength>span*.2 || Dot(Unit(gap),Unit(enter))<.65 || Dot(Unit(gap),Unit(leave))<.65)
        throw std::runtime_error("Ribbon ends do not form an unambiguous bridge gap");
    simplified.push_back(simplified.front());
    return simplified;
}
struct Axis { Point center, axis; double major=0, minor=0; };
Axis Principal(const Path& path) {
    const auto d=Distances(path.points); if(d.back()<=0) return {};
    constexpr int N=128; std::array<Point,N> points{}; Point center{};
    for(int i=0;i<N;++i) { points[i]=Sample(path.points,d,d.back()*i/N); center=center+points[i]*(1./N); }
    double xx=0,xy=0,yy=0;
    for(const Point p:points) { const Point v=p-center; xx+=v.x*v.x; xy+=v.x*v.y; yy+=v.y*v.y; }
    const double angle=.5*std::atan2(2*xy,xx-yy); const Point axis{std::cos(angle),std::sin(angle)}, normal{-axis.y,axis.x};
    double lo=1e20,hi=-1e20,sideLo=1e20,sideHi=-1e20;
    for(const Point p:path.points) { const Point v=p-center; const double a=Dot(v,axis), b=Dot(v,normal); lo=std::min(lo,a); hi=std::max(hi,a); sideLo=std::min(sideLo,b); sideHi=std::max(sideHi,b); }
    // The bounds center is less biased by the long edge of an arrowhead than its centroid.
    return {center+axis*((lo+hi)/2)+normal*((sideLo+sideHi)/2),axis,hi-lo,sideHi-sideLo};
}
Point ArrowDirection(const Path& path) {
    const Axis a=Principal(path);
    if(a.major<1 || a.minor<a.major*.12 || a.major/a.minor>5) return {};
    // A tip is a sharp corner near the arrow's longitudinal axis; the wide head's
    // other corners are deliberately excluded. Rounded shaft ends cannot win.
    double best=1.75; Point tip{}; bool found=false;
    const auto& p=path.points; const size_t n=p.size()-1; const Point normal{-a.axis.y,a.axis.x};
    for(size_t i=0;i<n;++i) {
        if(std::abs(Dot(p[i]-a.center,normal))>a.minor*.20) continue;
        const Point left=Unit(p[(i+n-1)%n]-p[i]), right=Unit(p[(i+1)%n]-p[i]);
        const double angle=std::acos(std::clamp(Dot(left,right),-1.,1.));
        if(angle<best && std::abs(Dot(p[i]-a.center,a.axis))>a.major*.3) { best=angle; tip=p[i]; found=true; }
    }
    return found?Unit(a.axis*(Dot(tip-a.center,a.axis)>0?1.:-1.)):Point{};
}
bool SameViewport(const Layer& a,const Layer& b) { return std::abs(a.width-b.width)<.1 && std::abs(a.height-b.height)<.1; }
const Source Sources[]={
#include "TrackMapSources.inc"
};
}
std::string TrackFolder(int trackId) {
    const auto* source=FindSource(trackId); if(!source) return {};
    // ".../tracks_indianapolis/448-indianapolis-2022-road" belongs to "indianapolis".
    std::string_view track(source->folder); const auto at=track.find("/tracks_"); if(at==std::string_view::npos) return {};
    track.remove_prefix(at+8); return std::string(track.substr(0,track.find('/')));
}
std::string NormalizeName(std::string_view name) {
    std::string result;
    for(unsigned char c:name) {
        if(c>='A' && c<='Z') c=static_cast<unsigned char>(c+'a'-'A');
        if((c>='a' && c<='z') || (c>='0' && c<='9')) result+=static_cast<char>(c);
    }
    return result;
}
const Source* FindSource(int trackId,std::string_view name) {
    const auto it=std::lower_bound(std::begin(Sources),std::end(Sources),trackId,[](const Source& s,int id){return s.id<id;});
    if(it==std::end(Sources) || it->id!=trackId || (!name.empty() && NormalizeName(name)!=NormalizeName(it->slug))) return nullptr;
    return it;
}
Point Geometry::At(double fraction,int overrideDirection,double offset) const {
    if(route.size()<2 || length<=0 || !std::isfinite(fraction) || !std::isfinite(offset)) return {};
    const int sign=overrideDirection?overrideDirection:direction;
    return Sample(route,distances,Wrap(start/length+sign*(fraction+offset))*length);
}
double Geometry::Heading(double fraction) const {
    if(!direction) return std::numeric_limits<double>::quiet_NaN();
    // Three map units either side: wide enough to smooth the sampling, narrow enough for hairpins.
    const double step=length>0?3/length:0;
    const Point a=At(fraction-step), b=At(fraction+step);
    if(!std::isfinite(fraction) || (a.x==b.x && a.y==b.y)) return std::numeric_limits<double>::quiet_NaN();
    const double heading=std::atan2(b.x-a.x,a.y-b.y);
    return heading<0?heading+2*3.141592653589793:heading;
}
Point Geometry::PitAt(double fraction) const {
    const Point track=At(fraction);
    Point best=track; double minimum=std::numeric_limits<double>::max();
    for(const auto& path:pitPaths) { double d=0; const auto p=Nearest(path,track,&d); if(d<minimum) { minimum=d; best=p; } }
    // Farther than a twelfth of the map from the track: not the pit lane beside it (the pit road leaves the circuit elsewhere).
    const double limit=std::max(high.x-low.x,high.y-low.y)/12;
    return minimum<=limit?best:track;
}
double Geometry::PitHeading(double fraction) const {
    constexpr double NaN=std::numeric_limits<double>::quiet_NaN(), Turn=2*3.141592653589793;
    const Point track=At(fraction); const double extent=std::max(high.x-low.x,high.y-low.y);
    Point at{}; double minimum=std::numeric_limits<double>::max();
    for(const auto& path:pitPaths) { double d=0; const auto q=Nearest(path,track,&d); if(d<minimum) { minimum=d; at=q; } }
    // As PitAt: farther than a twelfth of the map, the pit lane is not beside this point.
    if(pitPaths.empty() || minimum>extent/12) return NaN;
    // The long axis of the drawing within a fiftieth of the map around that point: pit lanes are drawn as bands or
    // as dashes with rounded ends, whose single edges point every way, but whose extent follows the lane.
    const double radius=extent/50; Point centre{}; int count=0;
    std::vector<Point> near;
    for(const auto& path:pitPaths) for(size_t i=1;i<path.size();++i) {
        // Points every unit along the edges, so long straight edges weigh by their length.
        const Point a=path[i-1], delta=path[i]-a; const int steps=std::max(1,static_cast<int>(std::sqrt(Dot(delta,delta))));
        for(int s=0;s<steps;++s) { const Point p=a+delta*(static_cast<double>(s)/steps); if(Dot(p-at,p-at)<=radius*radius) { near.push_back(p); centre=centre+p; ++count; } }
    }
    if(count<2) return NaN;
    centre=centre*(1./count);
    double xx=0,xy=0,yy=0; for(const auto& p:near) { const Point d=p-centre; xx+=d.x*d.x; xy+=d.x*d.y; yy+=d.y*d.y; }
    // Direction of the larger eigenvector of the covariance; a round blob has none.
    const double axis=std::atan2(2*xy,xx-yy)/2, spread=std::hypot(xx-yy,2*xy);
    if(spread<.2*(xx+yy)) return NaN;
    const Point delta{std::cos(axis),std::sin(axis)};
    double heading=std::atan2(delta.x,-delta.y);
    // An outline runs both ways: the way that goes along with the track beside it.
    const double travel=Heading(fraction);
    if(std::isfinite(travel) && std::abs(std::remainder(heading-travel,Turn))>Turn/4) heading+=Turn/2;
    return std::fmod(std::fmod(heading,Turn)+Turn,Turn);
}
Result Prepare(std::string active,std::string finish,std::string pit,std::string turns,std::string background,std::string inactive) {
    Result result;
    try {
        auto& g=result.geometry; g.active=Parse(std::move(active)); g.finish=Parse(std::move(finish));
        try { g.labels=ParseLabels(std::move(turns),g.active); } catch(const std::exception&) { g.labels.clear(); }
        if(!SameViewport(g.active,g.finish)) throw std::runtime_error("SVG layer viewports do not match");
        // Optional layers: one that fails or does not fit the map is left out, never the map.
        auto optional=[&](std::string text,Layer& layer) {
            if(text.empty()) return;
            try { layer=Parse(std::move(text)); if(!SameViewport(g.active,layer)) layer={}; } catch(const std::exception&) { layer={}; }
        };
        optional(std::move(pit),g.pit); optional(std::move(background),g.background); optional(std::move(inactive),g.inactive);
        for(const auto& shape:g.pit.shapes) for(const auto& path:shape.paths) if(path.points.size()>=2) {
            auto points=path.points; if(path.closed) points.push_back(points.front());
            g.pitPaths.push_back(std::move(points));
        }
        struct Ring { const Path* path; double length; bool filled; };
        std::vector<Ring> rings;
        for(const auto& shape:g.active.shapes) for(const auto& path:shape.paths)
            if(path.closed && path.points.size()>=4) { const auto d=Distances(path.points); if(d.back()>0) rings.push_back({&path,d.back(),shape.filled}); }
        std::sort(rings.begin(),rings.end(),[](const Ring& a,const Ring& b){return a.length>b.length;});
        if(rings.empty()) throw std::runtime_error("Active SVG has no closed circuit");
        if(rings.size()>2 && rings[2].length>rings[0].length*.2) throw std::runtime_error("Active SVG has multiple ambiguous circuits");
        const bool ribbon=rings.size()==1 && rings[0].filled;
        const auto ribbonPath=ribbon?RibbonRoute(*rings[0].path):std::vector<Point>{};
        const auto& boundary=ribbon?ribbonPath:rings[0].path->points; const auto d=Distances(boundary);
        const bool pair=rings.size()>=2 && rings[1].length/rings[0].length>.75 && Inside(boundary,rings[1].path->points.front());
        g.low={1e20,1e20}; g.high={-1e20,-1e20};
        for(int i=0;i<RouteSamples;++i) {
            Point p=Sample(boundary,d,d.back()*i/RouteSamples);
            if(pair) { double gap=0; const Point inner=Nearest(rings[1].path->points,p,&gap); if(gap>d.back()*.03) throw std::runtime_error("Circuit boundaries cannot be paired reliably"); p=(p+inner)*.5; }
            g.route.push_back(p);
        }
        g.route.push_back(g.route.front()); g.distances=Distances(g.route); g.length=g.distances.back(); g.centered=pair || ribbon; g.bridged=ribbon;
        for(const auto& shape:g.active.shapes) for(const auto& path:shape.paths) for(Point p:path.points) {
            g.low.x=std::min(g.low.x,p.x); g.low.y=std::min(g.low.y,p.y); g.high.x=std::max(g.high.x,p.x); g.high.y=std::max(g.high.y,p.y);
        }
        if(g.length<=0 || g.high.x<=g.low.x || g.high.y<=g.low.y) throw std::runtime_error("Degenerate circuit");
        double bestLine=1e20; Point arrow{}; bool startFound=false;
        for(const auto& shape:g.finish.shapes) for(const auto& path:shape.paths) {
            const Axis a=Principal(path);
            if(a.major<=0) continue;
            if(a.minor<a.major*.23) {
                // Intersect the stripe's long axis with the circuit, keeping only
                // intersections inside the actual marker, not its infinite extension.
                const Point from=a.center-a.axis*(a.major*.6), ray=a.axis*(a.major*1.2);
                for(size_t i=1;i<g.route.size();++i) {
                    const Point delta=g.route[i]-g.route[i-1], rel=g.route[i-1]-from; const double cross=Cross(ray,delta);
                    if(std::abs(cross)<1e-9) continue;
                    const double t=Cross(rel,delta)/cross, u=Cross(rel,ray)/cross;
                    if(t<0 || t>1 || u<0 || u>1) continue;
                    const double score=std::abs(t-.5)*a.major;
                    if(score<bestLine) { bestLine=score; g.start=g.distances[i-1]+u*Length(delta); startFound=true; }
                }
            } else { const Point candidate=ArrowDirection(path); if(Length(candidate)>.5) arrow=candidate; }
        }
        if(!startFound) throw std::runtime_error("Start/finish stripe could not be located on the circuit");
        if(Length(arrow)>.5) {
            const Point before=Sample(g.route,g.distances,Wrap((g.start-2)/g.length)*g.length), after=Sample(g.route,g.distances,Wrap((g.start+2)/g.length)*g.length);
            const double alignment=Dot(Unit(after-before),arrow);
            if(std::abs(alignment)>.65) g.direction=alignment>0?1:-1;
        }
        // An unresolved direction still renders the official map, but never guesses car positions.
    } catch(const std::exception& e) { result.geometry={}; result.error=e.what(); }
    return result;
}
}
