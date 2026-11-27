#include "coupon.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>

namespace kinexis {
namespace {
constexpr double pi=3.14159265358979323846;
void need(bool ok,const std::string& why) {if(!ok) throw std::invalid_argument(why);}
double norm(Vec3 p) {return std::hypot(p[0],p[1],p[2]);}
double square(double n) {return n*n;}
double slope(double degrees) {return std::tan(degrees*pi/180);}
}
double coupon_fraction(const CouponConfig& c,int layer) {
    need(layer>=0 && (c.transition_layers==0 || c.transition_layers>=2),"invalid surface schedule");
    if(c.transition_layers==0) return 1;
    if(layer<=1) return 0;
    return std::min(1.0,double(layer-1)/(c.transition_layers-1));
}
double coupon_surface(const CouponConfig& c,int layer,double x,double y) {
    return c.substrate_thickness+layer*c.layer_height+
           coupon_fraction(c,layer)*(c.curvature_x*x*x+c.curvature_y*y*y);
}
double coupon_spacing(const CouponConfig& c,int layer,double x,double y) {
    return c.layer_height+(coupon_fraction(c,layer)-coupon_fraction(c,layer-1))*
                          (c.curvature_x*x*x+c.curvature_y*y*y);
}
int coupon_layer(const CouponConfig& c,const Sample& s) {
    need(s.layer>=0,"negative layer metadata");
    if(s.layer>0) {need(s.layer<=c.layers,"invalid layer metadata"); return s.layer;}
    need(c.transition_layers==0,"transition requires explicit layer metadata");
    double k=(s.tip[2]-c.substrate_thickness-c.curvature_x*s.tip[0]*s.tip[0]-c.curvature_y*s.tip[1]*s.tip[1])/c.layer_height;
    need(std::isfinite(k) && k>=1-1e-8 && k<=c.layers+1e-8 && std::abs(k-std::round(k))<1e-8,"invalid deposition layer");
    return static_cast<int>(std::round(k));
}
double coupon_minimum_z(const CouponConfig& c,const Sample& a,const Sample& b) {
    if(b.kind!="extrusion") return std::min(a.tip[2],b.tip[2]);
    auto near=[](double x,double y) {return x*y<=0?0:std::min(std::abs(x),std::abs(y));};
    return coupon_surface(c,coupon_layer(c,b),near(a.tip[0],b.tip[0]),near(a.tip[1],b.tip[1]));
}
CouponConfig read_coupon(const std::string& path) {
    std::ifstream in(path); need(bool(in),"cannot open coupon config");
    CouponConfig c{}; std::string key;
    auto expect=[&](const char* wanted) {in>>key; need(key==wanted,"expected coupon field: "+std::string(wanted));};
    expect("surface"); in>>c.curvature_x>>c.curvature_y;
    expect("bounds"); in>>c.x_min>>c.x_max>>c.y_min>>c.y_max;
    expect("rows"); in>>c.rows;
    expect("layers"); in>>c.layers;
    expect("layer_height"); in>>c.layer_height;
    expect("bead_width"); in>>c.bead_width;
    expect("filament_diameter"); in>>c.filament_diameter;
    expect("bed_lift"); in>>c.bed_lift;
    expect("clearance_z"); in>>c.clearance_z;
    expect("clearance_margin"); in>>c.clearance_margin;
    expect("retract"); in>>c.retract;
    expect("print_speed"); in>>c.print_speed;
    expect("travel_speed"); in>>c.travel_speed;
    expect("position_tolerance"); in>>c.position_tolerance;
    expect("alignment_tolerance_deg"); in>>c.alignment_tolerance_deg;
    expect("pose_step_deg"); in>>c.pose_step_deg;
    expect("volume_error_per_mm"); in>>c.volume_error_per_mm;
    expect("point_budget"); in>>c.point_budget;
    need(!in.fail(),"invalid coupon number");
    std::set<std::string> optional;
    while(in>>key) {
        need(optional.insert(key).second,"duplicate optional coupon field");
        if(key=="substrate_thickness") in>>c.substrate_thickness;
        else if(key=="transition_layers") in>>c.transition_layers;
        else if(key=="layer_gap_limits") in>>c.minimum_layer_gap>>c.maximum_layer_gap;
        else if(key=="material_work_budget") in>>c.material_work_budget;
        else throw std::invalid_argument("unexpected coupon data");
        need(!in.fail(),"invalid optional coupon number");
    }
    return c;
}
CouponResult make_coupon(const Machine& machine,const CouponConfig& c) {
    validate(machine);
    for(double n:{c.curvature_x,c.curvature_y,c.x_min,c.x_max,c.y_min,c.y_max,c.layer_height,
                  c.bead_width,c.filament_diameter,c.bed_lift,c.clearance_z,c.clearance_margin,
                  c.retract,c.print_speed,c.travel_speed,c.position_tolerance,c.alignment_tolerance_deg,
                  c.pose_step_deg,c.volume_error_per_mm,c.substrate_thickness,c.minimum_layer_gap,c.maximum_layer_gap}) need(std::isfinite(n),"non-finite coupon parameter");
    need(c.substrate_thickness>=0,"negative substrate thickness");
    need(c.curvature_x>=0 && c.curvature_y>=0,"coupon requires a convex quadratic height map");
    need(c.x_min<c.x_max && c.y_min<c.y_max,"invalid coupon bounds");
    need(c.rows>=2 && c.rows<=100 && c.layers>=1 && c.layers<=100,"invalid coupon row/layer count");
    need(c.transition_layers==0 || (c.transition_layers>=2 && c.transition_layers<=c.layers),"invalid transition length");
    need(c.material_work_budget>0 && c.material_work_budget<=100000000,"invalid material work budget");
    need((c.minimum_layer_gap==0 && c.maximum_layer_gap==0 && c.transition_layers==0) ||
         (c.minimum_layer_gap>0 && c.maximum_layer_gap>c.minimum_layer_gap),"transition needs explicit layer-gap limits");
    for(double n:{c.layer_height,c.bead_width,c.filament_diameter,c.clearance_z,c.clearance_margin,
                  c.print_speed,c.travel_speed,c.position_tolerance,c.alignment_tolerance_deg,
                  c.pose_step_deg,c.volume_error_per_mm}) need(n>0,"coupon tolerances/dimensions/speeds must be positive");
    need(c.retract>=0 && c.bed_lift>=0,"negative retract or bed lift");
    need(c.point_budget>=10 && c.point_budget<=1000000,"invalid point budget");
    double area=pi*square(c.filament_diameter)/4;
    need(std::isfinite(area) && area>0,"invalid filament area");
    auto orientation=[&](int layer,double x,double y) {
        double fraction=coupon_fraction(c,layer);
        return Pose{c.bed_lift,-std::atan(2*fraction*c.curvature_x*x)*180/pi,
                               -std::atan(2*fraction*c.curvature_y*y)*180/pi};
    };
    auto slope_norm=[&](double x,double y) {return std::hypot(2*c.curvature_x*x,2*c.curvature_y*y);};
    double max_x=std::max(std::abs(c.x_min),std::abs(c.x_max));
    double max_y=std::max(std::abs(c.y_min),std::abs(c.y_max));
    need(slope_norm(max_x,max_y)<=1,"surface gradient exceeds proven bound domain");
    need(c.clearance_z>coupon_surface(c,c.layers,max_x,max_y)+c.clearance_margin,
         "clearance plane is too low");
    CouponResult out;
    out.travel_clearance_lower_bound=std::numeric_limits<double>::infinity();
    out.minimum_interlayer_gap=std::numeric_limits<double>::infinity();
    double largest_depth=coupon_spacing(c,c.transition_layers?2:1,max_x,max_y);
    double footprint=c.bead_width/2+largest_depth*slope_norm(max_x,max_y)+c.position_tolerance;
    double extended_slope=slope_norm(max_x+footprint,max_y+footprint);
    double spacing_error=2*((1+extended_slope)*c.position_tolerance+
                           std::max(c.curvature_x,c.curvature_y)*footprint*footprint);
    for(int k=2;k<=c.layers;++k) {
        double low=c.layer_height-spacing_error;
        double high=coupon_spacing(c,k,max_x+footprint,max_y+footprint)+spacing_error;
        if(c.maximum_layer_gap>0) need(low>=c.minimum_layer_gap && high<=c.maximum_layer_gap,
                                       "transition interlayer spacing is outside limits");
        out.minimum_interlayer_gap=std::min(out.minimum_interlayer_gap,low);
        out.maximum_interlayer_gap=std::max(out.maximum_interlayer_gap,high);
    }
    if(c.layers==1) out.minimum_interlayer_gap=out.maximum_interlayer_gap=c.layer_height;
    out.samples.push_back({0,"travel",{machine.pivot_x,machine.pivot_y,c.clearance_z},0,{}});
    out.position_bounds.push_back(0);
    // Subdivision follows the actual controller model: linear world XYZ and
    // actuator commands. Quadratic gradients make interpolated bed slopes exact.
    auto move=[&](Sample destination,bool deposit,bool clearance,int layer) {
        const Sample start=out.samples.back();
        auto at=[&](double t) {
            Sample s=destination;
            for(size_t i=0;i<3;++i) s.tip[i]=start.tip[i]+t*(destination.tip[i]-start.tip[i]);
            if(deposit) s.tip[2]=coupon_surface(c,layer,s.tip[0],s.tip[1]);
            s.pose={start.pose.lift+t*(destination.pose.lift-start.pose.lift),
                    std::atan(slope(start.pose.pitch)+t*(slope(destination.pose.pitch)-slope(start.pose.pitch)))*180/pi,
                    std::atan(slope(start.pose.roll)+t*(slope(destination.pose.roll)-slope(start.pose.roll)))*180/pi};
            if(!deposit) s.extrusion=start.extrusion+t*(destination.extrusion-start.extrusion);
            return s;
        };
        std::function<void(double,double,int)> subdivide;
        subdivide=[&](double u0,double u1,int depth) {
            Sample a=at(u0),b=at(u1);
            double dx=b.tip[0]-a.tip[0],dy=b.tip[1]-a.tip[1];
            double length=std::hypot(dx,dy);
            double dsx=slope(b.pose.pitch)-slope(a.pose.pitch);
            double dsy=slope(b.pose.roll)-slope(a.pose.roll);
            double d=std::hypot(dsx,dsy);
            double s=std::max(std::hypot(slope(a.pose.pitch),slope(a.pose.roll)),
                              std::hypot(slope(b.pose.pitch),slope(b.pose.roll)));
            need(s<=1+1e-12,"pose gradient exceeds proven bound domain");
            Vec3 extent;
            for(size_t i=0;i<3;++i) {
                double pivot=i==0?machine.pivot_x:i==1?machine.pivot_y:0;
                extent[i]=std::max(std::abs(a.tip[i]-pivot),std::abs(b.tip[i]-pivot));
            }
            double p=norm(extent);
            double first=deposit?length*std::sqrt(1+s*s):
                norm({dx,dy,b.tip[2]-a.tip[2]});
            double fraction=coupon_fraction(c,layer);
            double second=deposit?fraction*(2*c.curvature_x*dx*dx+2*c.curvature_y*dy*dy):0;
            // For |slope|<=1: ||R'||<=7D, ||R''||<=40D^2. The
            // interpolation remainder is max||world_tip''||/8 (unit interval).
            double position_bound=(40*d*d*p+14*d*first+second)/8;
            double alignment_bound=deposit?4*fraction*std::max(c.curvature_x,c.curvature_y)*position_bound*180/pi:0;
            // Lipschitz bound for midpoint integration of local normal bead
            // area * surface path length, using vertical layer spacing.
            double gap_max=deposit?std::max(coupon_spacing(c,layer,a.tip[0],a.tip[1]),coupon_spacing(c,layer,b.tip[0],b.tip[1])):0;
            double delta_fraction=deposit?fraction-coupon_fraction(c,layer-1):0;
            double full_gradient=std::max(slope_norm(a.tip[0],a.tip[1]),slope_norm(b.tip[0],b.tip[1]));
            double volume_bound=deposit?c.bead_width*(delta_fraction*full_gradient*length*length*std::sqrt(2.0)+
                gap_max*length*d*(1+std::sqrt(2.0)))/4:0;
            for(double n:{position_bound,alignment_bound,volume_bound}) need(std::isfinite(n),"coupon bound overflow");
            if(position_bound>c.position_tolerance || alignment_bound>c.alignment_tolerance_deg ||
               d*180/pi>c.pose_step_deg || (deposit && volume_bound>c.volume_error_per_mm*length)) {
                need(depth<24,"subdivision depth exhausted");
                double mid=(u0+u1)/2; need(mid>u0 && mid<u1,"subdivision precision exhausted");
                subdivide(u0,mid,depth+1); subdivide(mid,u1,depth+1); return;
            }
            need(out.samples.size()<static_cast<size_t>(c.point_budget),"coupon point budget exhausted");
            double minimum_z=deposit?c.substrate_thickness+layer*c.layer_height:std::min(a.tip[2],b.tip[2]);
            need(minimum_z-position_bound>=0,"possible bed-plane penetration");
            if(clearance) {
                double x=std::max(std::abs(a.tip[0]),std::abs(b.tip[0]))+position_bound;
                double y=std::max(std::abs(a.tip[1]),std::abs(b.tip[1]))+position_bound;
                double gap=minimum_z-position_bound-coupon_surface(c,c.layers,x,y);
                need(gap>=c.clearance_margin,"travel/transition clearance cannot be established");
                out.travel_clearance_lower_bound=std::min(out.travel_clearance_lower_bound,gap);
            }
            if(deposit) {
                double x=(a.tip[0]+b.tip[0])/2,y=(a.tip[1]+b.tip[1])/2;
                double gx=2*fraction*c.curvature_x*x,gy=2*fraction*c.curvature_y*y;
                double volume=c.bead_width*coupon_spacing(c,layer,x,y)*
                    std::hypot(length,gx*dx+gy*dy)/std::sqrt(1+gx*gx+gy*gy);
                need(std::isfinite(volume) && volume>=0,"invalid deposited volume");
                out.volume+=volume; out.volume_error_bound+=volume_bound;
                b.extrusion=out.samples.back().extrusion+volume/area;
                out.max_alignment_error_bound_deg=std::max(out.max_alignment_error_bound_deg,alignment_bound);
            }
            const auto& previous=out.samples.back();
            auto old_tip=transform(machine,previous.pose,previous.tip),new_tip=transform(machine,b.pose,b.tip);
            auto old_axes=inverse(machine,previous.pose),new_axes=inverse(machine,b.pose);
            Vec3 delta;
            for(size_t i=0;i<3;++i) delta[i]=new_tip[i]-old_tip[i];
            double dt=norm(delta)/(deposit?c.print_speed:c.travel_speed);
            for(size_t i=0;i<3;++i) {
                dt=std::max(dt,std::abs(delta[i])/machine.speed[i]);
                dt=std::max(dt,std::abs(new_axes[i]-old_axes[i])/machine.actuators[i].speed);
            }
            dt=std::max(dt,std::abs(b.extrusion-previous.extrusion)/machine.extruder_speed);
            b.time=previous.time+std::max(1e-6,dt*1.01);
            need(std::isfinite(b.time) && b.time>previous.time,"invalid retimed coupon");
            out.samples.push_back(b); out.position_bounds.push_back(position_bound);
            out.max_position_error_bound=std::max(out.max_position_error_bound,position_bound);
        };
        subdivide(0,1,0);
    };
    int planner_layer=0;
    auto waypoint=[&](Vec3 tip,Pose pose,const std::string& kind,double e,bool clear) {
        move({0,kind,tip,e,pose,planner_layer},false,clear,planner_layer);
    };
    // Park above the first track before changing orientation.
    Vec3 tip{c.x_min,c.y_min,c.clearance_z};
    waypoint(tip,{},"travel",0,true);
    waypoint(tip,orientation(1,tip[0],tip[1]),"transition",0,true);
    bool first=true;
    for(int layer=1;layer<=c.layers;++layer) for(int row=0;row<c.rows;++row) {
        planner_layer=layer;
        double y=c.y_min+(c.y_max-c.y_min)*row/(c.rows-1);
        bool reverse=row%2!=0;
        double x=reverse?c.x_max:c.x_min, end_x=reverse?c.x_min:c.x_max;
        if(!first) {
            auto last=out.samples.back();
            waypoint(last.tip,last.pose,"retract",last.extrusion-c.retract,false);
            tip=last.tip; tip[2]=c.clearance_z;
            waypoint(tip,last.pose,"lift",out.samples.back().extrusion,false);
            tip={x,y,c.clearance_z};
            waypoint(tip,orientation(layer,x,y),"travel",out.samples.back().extrusion,true);
        }
        tip={x,y,coupon_surface(c,layer,x,y)};
        waypoint(tip,orientation(layer,x,y),"lift",out.samples.back().extrusion,false);
        if(!first) waypoint(tip,orientation(layer,x,y),"transition",out.samples.back().extrusion+c.retract,false);
        move({0,"extrusion",{end_x,y,coupon_surface(c,layer,end_x,y)},0,orientation(layer,end_x,y),layer},true,false,layer);
        first=false;
    }
    auto last=out.samples.back();
    waypoint(last.tip,last.pose,"retract",last.extrusion-c.retract,false);
    tip=last.tip; tip[2]=c.clearance_z;
    waypoint(tip,last.pose,"lift",out.samples.back().extrusion,false);
    waypoint(tip,{},"transition",out.samples.back().extrusion,true);
    waypoint({machine.pivot_x,machine.pivot_y,c.clearance_z},{},"travel",out.samples.back().extrusion,true);
    out.frames=simulate(machine,out.samples);
    out.deposited_filament=out.volume/area;
    for(double n:{out.volume,out.volume_error_bound,out.travel_clearance_lower_bound,out.deposited_filament})
        need(std::isfinite(n),"non-finite coupon report");
    return out;
}
}
