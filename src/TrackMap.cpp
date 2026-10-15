#include "TrackMap.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
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
Result Prepare(std::string active,std::string finish,std::string pit) {
    Result result;
    try {
        auto& g=result.geometry; g.active=Parse(std::move(active)); g.finish=Parse(std::move(finish));
        if(!SameViewport(g.active,g.finish)) throw std::runtime_error("SVG layer viewports do not match");
        if(!pit.empty()) { try { g.pit=Parse(std::move(pit)); if(!SameViewport(g.active,g.pit)) g.pit={}; } catch(const std::exception&) { g.pit={}; } }
        struct Ring { const Path* path; double length; };
        std::vector<Ring> rings;
        for(const auto& shape:g.active.shapes) for(const auto& path:shape.paths)
            if(path.closed && path.points.size()>=4) { const auto d=Distances(path.points); if(d.back()>0) rings.push_back({&path,d.back()}); }
        std::sort(rings.begin(),rings.end(),[](const Ring& a,const Ring& b){return a.length>b.length;});
        if(rings.empty()) throw std::runtime_error("Active SVG has no closed circuit");
        if(rings.size()>2 && rings[2].length>rings[0].length*.2) throw std::runtime_error("Active SVG has multiple ambiguous circuits");
        const auto& boundary=rings[0].path->points; const auto d=Distances(boundary);
        const bool pair=rings.size()>=2 && rings[1].length/rings[0].length>.75 && Inside(boundary,rings[1].path->points.front());
        g.low={1e20,1e20}; g.high={-1e20,-1e20};
        for(int i=0;i<RouteSamples;++i) {
            Point p=Sample(boundary,d,d.back()*i/RouteSamples);
            if(pair) { double gap=0; const Point inner=Nearest(rings[1].path->points,p,&gap); if(gap>d.back()*.03) throw std::runtime_error("Circuit boundaries cannot be paired reliably"); p=(p+inner)*.5; }
            g.route.push_back(p);
        }
        g.route.push_back(g.route.front()); g.distances=Distances(g.route); g.length=g.distances.back(); g.centered=pair;
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
