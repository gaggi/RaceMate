#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace rm::trackmap {
struct Point { double x=0, y=0; };
struct Path { std::vector<Point> points; bool closed=false; };
struct Shape { std::vector<Path> paths; bool evenOdd=false; double strokeWidth=0; bool filled=true; };
struct Layer { double width=0, height=0; std::vector<Shape> shapes; };
// Text from the official turns layer; coordinates share the map's SVG viewport.
struct Label {
    Point position;
    std::string text;
    double size=36, angle=0;
    int anchor=0; // start, middle, end
    bool number=false;
};
struct Geometry {
    Layer active, finish, pit;
    // Optional official layers: the drawing around the track, and the track's other configurations.
    Layer background, inactive;
    std::vector<Label> labels;
    std::vector<Point> route;
    std::vector<double> distances;
    Point low, high;
    double length=0, start=0;
    int direction=0;
    bool centered=false, bridged=false;
    Point At(double fraction, int directionOverride=0, double offset=0) const;
    // Direction of travel at this lap fraction in radians, clockwise from the top of the map; NaN without a direction.
    double Heading(double fraction) const;
    // The pit-lane drawing as outlines; a car in the pits is placed on the nearest of their edges to its lap position.
    std::vector<std::vector<Point>> pitPaths;
    // The nearest point of the drawn pit lane to the track at this lap fraction; the track point where the pit lane is far away.
    Point PitAt(double fraction) const;
    // The drawn pit lane's direction beside this lap fraction, as Heading: where a car in its pit stall points.
    // NaN without a pit drawing near.
    double PitHeading(double fraction) const;
};
struct Result {
    Geometry geometry;
    std::string error;
    explicit operator bool() const { return error.empty() && geometry.length>0; }
};
// SVGs are parsed once on a worker. No telemetry learning or track-specific corrections.
Result Prepare(std::string active, std::string finish, std::string pit={}, std::string turns={}, std::string background={}, std::string inactive={});
struct Source { int id; const char* slug; const char* folder; };
const Source* FindSource(int trackId, std::string_view trackName={});
// The track a layout belongs to, by its folder on the CDN: "indianapolis" for 448. All layouts of a track are
// drawn turned alike, so what is learned about one map's north holds for the others. Empty when unknown.
std::string TrackFolder(int trackId);
std::string NormalizeName(std::string_view name);
}
