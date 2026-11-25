#include "bed.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace kinexis {
namespace {
constexpr double pi = 3.14159265358979323846;
void require(bool ok, const std::string& message) {
    if (!ok) throw std::invalid_argument(message);
}
void finite(double n) { require(std::isfinite(n), "non-finite number"); }
void bounded(double n, double lo, double hi, const std::string& name) {
    finite(n);
    require(n >= lo && n <= hi, name + " outside travel/tilt limits");
}
double determinant(const Machine& m) {
    const auto& a = m.actuators;
    return (a[1].x-a[0].x)*(a[2].y-a[0].y)-(a[2].x-a[0].x)*(a[1].y-a[0].y);
}
void check_pose(const Machine& m, Pose p) {
    finite(p.lift);
    // atan(tan(limit)) may reconstruct just beyond an exact boundary. Accept
    // numerical angular residuals without altering the requested orientation.
    constexpr double angular_epsilon=1e-10;
    bounded(p.pitch, -m.pitch_limit-angular_epsilon, m.pitch_limit+angular_epsilon, "pitch");
    bounded(p.roll, -m.roll_limit-angular_epsilon, m.roll_limit+angular_epsilon, "roll");
}
Vec3 cross(Vec3 a, Vec3 b) {
    return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
void rate(double previous, double next, double dt, double limit, const std::string& name) {
    double speed = std::abs(next-previous)/dt;
    finite(speed);
    require(speed <= limit + 1e-9, name + " exceeds speed limit");
}
}
void validate(const Machine& m) {
    finite(m.pivot_x); finite(m.pivot_y);
    require(std::isfinite(m.pitch_limit) && m.pitch_limit > 0 && m.pitch_limit < 80, "invalid pitch limit");
    require(std::isfinite(m.roll_limit) && m.roll_limit > 0 && m.roll_limit < 80, "invalid roll limit");
    require(std::isfinite(m.extruder_speed) && m.extruder_speed > 0, "invalid extruder speed");
    for (size_t i=0; i<3; ++i) {
        finite(m.minimum[i]); finite(m.maximum[i]); finite(m.speed[i]);
        require(m.minimum[i] < m.maximum[i] && m.speed[i] > 0, "invalid XYZ limits");
    }
    std::set<std::string> names;
    for (const auto& a : m.actuators) {
        require(!a.name.empty() && names.insert(a.name).second, "duplicate/empty actuator name");
        require(std::all_of(a.name.begin(),a.name.end(),[](unsigned char c) {
            return std::isalnum(c) || c=='_';
        }), "actuator name must contain only letters, digits or underscore");
        for (double n : {a.x,a.y,a.sign,a.zero,a.minimum,a.maximum,a.speed}) finite(n);
        require(a.sign == 1 || a.sign == -1, "actuator sign must be +1 or -1");
        require(a.minimum < a.maximum && a.speed > 0, "invalid actuator limits");
        bounded(a.zero, a.minimum, a.maximum, a.name + " neutral");
    }
    double span = 1;
    for (const auto& a : m.actuators)
        span = std::max({span, std::abs(a.x-m.actuators[0].x), std::abs(a.y-m.actuators[0].y)});
    double det = determinant(m);
    finite(det);
    require(std::abs(det)/(span*span) > 1e-8, "degenerate actuator triangle");
}
Vec3 inverse(const Machine& m, Pose p) {
    validate(m); check_pose(m,p);
    const double sx=std::tan(p.pitch*pi/180), sy=std::tan(p.roll*pi/180);
    Vec3 result;
    for (size_t i=0; i<3; ++i) {
        const auto& a=m.actuators[i];
        result[i]=a.zero+a.sign*(p.lift+sx*(a.x-m.pivot_x)+sy*(a.y-m.pivot_y));
        bounded(result[i],a.minimum,a.maximum,a.name);
    }
    return result;
}
Pose forward(const Machine& m, Vec3 commands) {
    validate(m);
    Vec3 h;
    for (size_t i=0;i<3;++i) {
        const auto& a=m.actuators[i];
        bounded(commands[i],a.minimum,a.maximum,a.name);
        h[i]=(commands[i]-a.zero)/a.sign;
    }
    const auto& a=m.actuators;
    double det=determinant(m);
    double sx=((h[1]-h[0])*(a[2].y-a[0].y)-(h[2]-h[0])*(a[1].y-a[0].y))/det;
    double sy=((a[1].x-a[0].x)*(h[2]-h[0])-(a[2].x-a[0].x)*(h[1]-h[0]))/det;
    Pose result{h[0]+sx*(m.pivot_x-a[0].x)+sy*(m.pivot_y-a[0].y),
                std::atan(sx)*180/pi,std::atan(sy)*180/pi};
    check_pose(m,result);
    return result;
}
Vec3 transform(const Machine& m, Pose p, Vec3 tip) {
    validate(m); check_pose(m,p);
    for (double n:tip) finite(n);
    // Preserve the zero-pose path exactly, without subtract/add round-off.
    if (p.lift==0 && p.pitch==0 && p.roll==0) return tip;
    double sx=std::tan(p.pitch*pi/180), sy=std::tan(p.roll*pi/180);
    double norm=std::sqrt(1+sx*sx+sy*sy);
    Vec3 v{sy/norm,-sx/norm,0};
    Vec3 local{tip[0]-m.pivot_x,tip[1]-m.pivot_y,tip[2]};
    Vec3 first=cross(v,local), second=cross(v,first), result;
    // Shortest rotation taking +Z to the plane normal (-sx,-sy,1).
    for (size_t i=0;i<3;++i) result[i]=local[i]+first[i]+second[i]/(1+1/norm);
    result[0]+=m.pivot_x; result[1]+=m.pivot_y; result[2]+=p.lift;
    for (double n:result) finite(n);
    return result;
}
std::vector<Frame> simulate(const Machine& m, const std::vector<Sample>& samples, bool enabled) {
    validate(m); require(!samples.empty(), "empty trajectory");
    std::vector<Frame> frames;
    const std::set<std::string> kinds{"travel","extrusion","retract","lift","transition","wipe"};
    for (const auto& s:samples) {
        try {
            finite(s.time); finite(s.extrusion);
            require(s.time>=0, "negative time");
            require(kinds.count(s.kind)!=0, "unsupported move kind");
            // Non-finite source poses are rejected even with motion disabled.
            finite(s.pose.lift); finite(s.pose.pitch); finite(s.pose.roll);
            Pose pose=enabled?s.pose:Pose{};
            if (frames.empty()) require(pose.lift==0 && pose.pitch==0 && pose.roll==0,
                                        "first sample must be neutral (no implicit homing)");
            Frame f{s,transform(m,pose,s.tip),inverse(m,pose),{}};
            f.reconstructed=forward(m,f.actuators);
            for(size_t i=0;i<3;++i) bounded(f.tip[i],m.minimum[i],m.maximum[i],"XYZ tip");
            if(!frames.empty()) {
                const auto& prev=frames.back();
                double dt=s.time-prev.source.time;
                require(std::isfinite(dt) && dt>0,"timestamps must strictly increase");
                for(size_t i=0;i<3;++i) {
                    rate(prev.tip[i],f.tip[i],dt,m.speed[i],"XYZ tip");
                    rate(prev.actuators[i],f.actuators[i],dt,m.actuators[i].speed,m.actuators[i].name);
                }
                rate(prev.source.extrusion,s.extrusion,dt,m.extruder_speed,"extruder");
            }
            frames.push_back(f);
        } catch(const std::exception& e) {
            throw std::invalid_argument("sample "+std::to_string(frames.size()+1)+": "+e.what());
        }
    }
    return frames;
}
Machine read_machine(const std::string& path) {
    std::ifstream in(path); require(bool(in),"cannot open machine fixture");
    Machine m{}; std::string key;
    auto expect=[&](const char* wanted) { in>>key; require(key==wanted,"expected machine field: "+std::string(wanted)); };
    expect("pivot"); in>>m.pivot_x>>m.pivot_y;
    expect("tilt_limit"); in>>m.pitch_limit>>m.roll_limit;
    expect("xyz_min"); for(auto& n:m.minimum) in>>n;
    expect("xyz_max"); for(auto& n:m.maximum) in>>n;
    expect("xyz_speed"); for(auto& n:m.speed) in>>n;
    expect("extruder_speed"); in>>m.extruder_speed;
    for(auto& a:m.actuators) {
        expect("actuator"); in>>a.name>>a.x>>a.y>>a.sign>>a.zero>>a.minimum>>a.maximum>>a.speed;
    }
    require(!in.fail(),"invalid machine number");
    require(!(in>>key),"unexpected machine data");
    validate(m); return m;
}
std::vector<Sample> read_samples(const std::string& path) {
    std::ifstream in(path); require(bool(in),"cannot open trajectory");
    std::string line;
    std::getline(in,line); if(!line.empty() && line.back()=='\r') line.pop_back();
    require(line=="time,kind,x,y,z,e,lift,pitch,roll","unexpected CSV header");
    std::vector<Sample> result;
    while(std::getline(in,line)) {
        if(!line.empty() && line.back()=='\r') line.pop_back();
        std::istringstream row(line); std::vector<std::string> fields; std::string field;
        while(std::getline(row,field,',')) fields.push_back(field);
        require(fields.size()==9 && !line.empty() && line.back()!=',',"CSV row must have 9 fields");
        auto number=[&](size_t i) { size_t used=0; double n=std::stod(fields[i],&used);
            require(used==fields[i].size(),"invalid CSV number"); finite(n); return n; };
        result.push_back({number(0),fields[1],{number(2),number(3),number(4)},number(5),
                          {number(6),number(7),number(8)}});
    }
    return result;
}
}
