#include "bed.hpp"
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks=0;
void check(bool ok,const char* label) {
    ++checks;
    if(!ok) throw std::runtime_error(label);
}
void near(double a,double b) { check(std::abs(a-b)<1e-9,"numeric expectation failed"); }
void rejects(const std::function<void()>& fn) {
    bool rejected=false;
    try {fn();} catch(const std::invalid_argument&) {rejected=true;}
    check(rejected,"invalid motion/configuration was accepted");
}
}
int main() {
    using namespace kinexis;
    try {
        auto m=read_machine(std::string(FIXTURE_DIR)+"/virtual-bed.machine");
        // Independent plane z=0.1*x+0.1*y: support heights -20,0,+10.
        const double angle=5.710593137499642;
        auto h=inverse(m,{0,angle,angle});
        near(h[0],80); near(h[1],100); near(h[2],110);
        auto p=forward(m,{80,100,110});
        near(p.lift,0); near(p.pitch,angle); near(p.roll,angle);
        for(double sign:{-1.0,1.0}) {
            h=inverse(m,{2,sign*angle,0});
            near(h[0],102-sign*10); near(h[1],102+sign*10); near(h[2],102);
            h=inverse(m,{2,0,sign*angle});
            near(h[0],102-sign*10); near(h[1],102-sign*10); near(h[2],102+sign*10);
        }
        // Shifted pivot and reversed motor sign test the fixture coordinate convention.
        auto shifted=m; shifted.pivot_x=10; shifted.pivot_y=20;
        shifted.actuators[1].sign=-1;
        h=inverse(shifted,{3,angle,angle});
        near(h[0],80); near(h[1],100); near(h[2],110);
        p=forward(shifted,h); near(p.lift,3); near(p.pitch,angle); near(p.roll,angle);
        // Exact boundary poses must remain valid through reconstruction.
        for(double boundary:{-15.0,15.0}) {
            p=forward(m,inverse(m,{0,boundary,0})); near(p.pitch,boundary);
            p=forward(m,inverse(m,{0,0,boundary})); near(p.roll,boundary);
        }
        h=inverse(m,{50,0,0});
        for(double value:h) near(value,150);
        // Known 30-degree rigid rotation, with wider virtual actuator ranges.
        auto wide=m; wide.pitch_limit=45; wide.roll_limit=45;
        for(auto& a:wide.actuators) {a.minimum=-1000; a.maximum=1000;}
        auto point=transform(wide,{0,30,0},{10,0,0});
        near(point[0],8.660254037844386); near(point[1],0); near(point[2],5);
        point=transform(wide,{0,0,30},{0,10,0});
        near(point[0],0); near(point[1],8.660254037844386); near(point[2],5);
        for(int i=-10;i<=10;++i) {
            Pose pose{0,double(i),double(i)/2};
            p=forward(m,inverse(m,pose));
            near(p.lift,0); near(p.pitch,pose.pitch); near(p.roll,pose.roll);
            point=transform(m,pose,{17,-23,0});
            // Bed-local Z=0 must lie on the reconstructed world plane.
            near(point[2],std::tan(pose.pitch*3.14159265358979323846/180)*point[0]
                         +std::tan(pose.roll*3.14159265358979323846/180)*point[1]);
            near(point[0]*point[0]+point[1]*point[1]+point[2]*point[2],17*17+23*23);
        }
        auto samples=read_samples(std::string(FIXTURE_DIR)+"/motion.csv");
        auto disabled=simulate(m,samples,false);
        auto zero=samples;
        for(auto& s:zero) s.pose={};
        auto flat=simulate(m,zero);
        check(flat.size()==samples.size(),"lost trajectory samples");
        for(size_t i=0;i<samples.size();++i) {
            check(std::memcmp(disabled[i].tip.data(),samples[i].tip.data(),sizeof(Vec3))==0,
                  "disabled transform changed planar XYZ");
            check(std::memcmp(flat[i].tip.data(),samples[i].tip.data(),sizeof(Vec3))==0,
                  "zero transform changed planar XYZ");
            check(flat[i].source.extrusion==samples[i].extrusion,"changed extrusion");
            check(flat[i].source.kind==samples[i].kind,"changed move kind");
            for(auto v:flat[i].actuators) near(v,100);
        }
        auto tilted=simulate(m,samples);
        check(tilted.size()==samples.size(),"lost nonzero trajectory samples");
        Vec3 signed_zero{-0.0,0.0,10};
        point=transform(m,{},signed_zero);
        check(std::memcmp(point.data(),signed_zero.data(),sizeof(Vec3))==0,"lost signed zero");
        auto bad=m; bad.actuators[2].y=-100;
        rejects([&]{validate(bad);});
        bad=m; bad.actuators[2].name="U"; rejects([&]{validate(bad);});
        bad=m; bad.actuators[0].sign=0; rejects([&]{validate(bad);});
        bad=m; bad.speed[0]=0; rejects([&]{validate(bad);});
        rejects([&]{inverse(m,{0,16,0});});
        rejects([&]{inverse(m,{0,15.000001,0});});
        rejects([&]{inverse(m,{0,0,-16});});
        rejects([&]{inverse(m,{100,0,0});});
        rejects([&]{forward(m,{200,100,100});});
        rejects([&]{simulate(m,{});});
        auto altered=samples; altered[0].pose.pitch=1;
        rejects([&]{simulate(m,altered);});
        altered=samples; altered[1].time=0;
        rejects([&]{simulate(m,altered);});
        altered=samples; altered[1].time=0.01;
        rejects([&]{simulate(m,altered);});
        // Isolate actuator-rate and XYZ-rate failures, rather than masking one
        // behind the other. Small tip movement, large bed actuator movement.
        altered=zero; altered[1].time=0.1; altered[1].tip={0,0,10};
        altered[1].pose.pitch=10;
        rejects([&]{simulate(m,altered);});
        altered=zero; altered[1].time=0.1;
        rejects([&]{simulate(m,altered);});
        altered=zero; altered[1].tip[0]=151;
        rejects([&]{simulate(m,altered);});
        altered=zero; altered[1].tip[2]=-1;
        rejects([&]{simulate(m,altered);});
        altered=zero; altered[1].extrusion=1000;
        rejects([&]{simulate(m,altered);});
        altered=zero; altered[1].kind="arc";
        rejects([&]{simulate(m,altered);});
        for(double invalid:{std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity()}) {
            altered=zero; altered.back().tip[0]=invalid;
            rejects([&]{simulate(m,altered);});
            altered=zero; altered.back().pose.pitch=invalid;
            rejects([&]{simulate(m,altered,false);});
            altered=zero; altered.back().extrusion=invalid;
            rejects([&]{simulate(m,altered);});
        }
        // Each move kind must be checked, including non-extruding transitions.
        for(const auto& kind:{"travel","extrusion","retract","lift","transition","wipe"}) {
            altered=zero; altered[1].kind=kind; altered[1].tip[0]=151;
            rejects([&]{simulate(m,altered);});
        }
        std::cout<<checks<<" checks passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
