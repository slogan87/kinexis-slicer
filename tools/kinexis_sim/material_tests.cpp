#include "material.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void check(bool ok,const char* label) {++checks; if(!ok) throw std::runtime_error(label);}
void reject(const std::function<void()>& fn,const std::string& message) {
    try {fn();} catch(const std::invalid_argument& e) {
        check(std::string(e.what()).find(message)!=std::string::npos,"wrong rejection reason"); return;
    }
    throw std::runtime_error("unsafe or unsupported material motion was accepted");
}
}
int main() {
    using namespace kinexis;
    try {
        const std::string fixtures=FIXTURE_DIR;
        auto machine=read_machine(fixtures+"/virtual-bed.machine");
        auto c=read_coupon(fixtures+"/supported-coupon.cfg");
        auto actual=make_coupon(machine,c);
        auto stats=check_material(c,actual,0.2,0.02);
        check(stats.tracks==10,"default coupon track count");
        check(stats.deposited_cells==2560,"default coupon material cell count");
        check(stats.intervals_checked==actual.frames.size(),"not every interval checked");
        check(stats.permitted_contact_pairs>0,"intended fresh contact not exercised");
        reject([&]{check_material(c,actual,0.2,0.02,1);},"work budget");
        auto transition=read_coupon(fixtures+"/transition-coupon.cfg");
        auto changed=make_coupon(machine,transition);
        size_t first=0;
        while(changed.frames.at(first).source.kind!="extrusion") ++first;
        changed.frames[first].source.layer=0;
        reject([&]{check_material(transition,changed,0.2,0.02,transition.material_work_budget);},"explicit layer metadata");
        changed.frames[first].source.layer=2;
        reject([&]{check_material(transition,changed,0.2,0.02,transition.material_work_budget);},"configured curved layer");
        c.curvature_x=c.curvature_y=0; c.bed_lift=0;
        double e_per_mm=c.bead_width*c.layer_height/(3.14159265358979323846*c.filament_diameter*c.filament_diameter/4);
        std::vector<Sample> path;
        auto add=[&](const std::string& kind,Vec3 point,double e,Pose pose=Pose{}) {
            path.push_back({10.0*path.size(),kind,point,e,pose});
        };
        auto motion=[&] {
            CouponResult result; result.samples=path; result.frames=simulate(machine,path);
            result.position_bounds.assign(path.size(),0);
            return result;
        };
        auto run=[&] {return check_material(c,motion(),0.2,0.02);};
        // Contiguous monotone deposition, stationary retraction and vertical departure.
        add("travel",{-2,0,2.2},0);
        add("extrusion",{0,0,2.2},2*e_per_mm);
        add("extrusion",{2,0,2.2},4*e_per_mm);
        add("retract",{2,0,2.2},4*e_per_mm-0.8);
        add("lift",{2,0,4},4*e_per_mm-0.8);
        add("travel",{0,2,4},4*e_per_mm-0.8);
        stats=run(); check(stats.tracks==1 && stats.deposited_cells==2,"valid fresh-track chronology");
        path.resize(4);
        add("travel",{1,0,2.2},4*e_per_mm-0.8);
        reject(run,"prior material"); // retraction does not erase the previous bead
        path.resize(3);
        add("transition",{2,0,2.2},4*e_per_mm,{0,1,0});
        auto tilted=motion(); tilted.position_bounds.back()=0.01;
        reject([&]{check_material(c,tilted,0.2,0.02);},"prior material");
        path.resize(2);
        add("extrusion",{-1,0,2.2},3*e_per_mm);
        reject(run,"reversal/turn");
        path.resize(2);
        add("extrusion",{0,1,2.2},3*e_per_mm);
        reject(run,"reversal/turn");
        // Same geometric crossing before the track exists passes; after deposition fails.
        path.clear();
        add("travel",{0,-2,2.1},0);
        add("travel",{0,2,2.1},0);
        add("lift",{0,2,4},0);
        add("travel",{-2,0,4},0);
        add("lift",{-2,0,2.2},0);
        add("extrusion",{2,0,2.2},4*e_per_mm);
        add("retract",{2,0,2.2},4*e_per_mm-0.8);
        add("lift",{2,0,4},4*e_per_mm-0.8);
        add("travel",{0,-2,4},4*e_per_mm-0.8);
        add("lift",{0,-2,2.1},4*e_per_mm-0.8);
        stats=run(); check(stats.tracks==1,"future track affected earlier empty-space travel");
        add("travel",{0,2,2.1},4*e_per_mm-0.8);
        reject(run,"prior material"); // both endpoints clear; interior crosses the bead
        // An unrelated perpendicular extrusion crossing the old track is not exempt.
        path.resize(9); path.back().tip={0,-2,4};
        add("lift",{0,-2,2.2},4*e_per_mm-0.8);
        add("transition",{0,-2,2.2},4*e_per_mm);
        add("extrusion",{0,2,2.2},8*e_per_mm);
        reject(run,"prior material");
        // Next layer clears the prior layer without any blanket contact exemption.
        path.clear();
        add("travel",{-2,0,2.2},0); add("extrusion",{2,0,2.2},4*e_per_mm);
        add("retract",{2,0,2.2},4*e_per_mm-0.8); add("lift",{2,0,4},4*e_per_mm-0.8);
        add("travel",{-2,0,4},4*e_per_mm-0.8); add("lift",{-2,0,2.4},4*e_per_mm-0.8);
        add("transition",{-2,0,2.4},4*e_per_mm); add("extrusion",{2,0,2.4},8*e_per_mm);
        stats=run(); check(stats.tracks==2,"valid layer-to-layer clearance rejected");
        path[5].tip[2]=path[6].tip[2]=path[7].tip[2]=2.2;
        reject(run,"prior material"); // returning to a completed layer is not fresh contact
        path.clear(); add("travel",{0,0,2.2},0); add("transition",{0,0,2.2},0.1);
        reject(run,"priming/blob");
        path.clear(); add("travel",{-2,0,2.2},0); add("extrusion",{2,0,2.2},1);
        reject(run,"E disagrees");
        path.clear(); add("travel",{-2,0,2.2},0); add("retract",{-2,0,2.2},-0.8);
        add("extrusion",{2,0,2.2},4*e_per_mm-0.8); reject(run,"recovered E");
        path.clear(); add("travel",{0,0,4},0); add("retract",{0,0,4},-0.8);
        add("travel",{1,0,4},0); reject(run,"moving recovery");
        std::cout<<checks<<" chronological material checks passed\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1;}
}
