#include "fit.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace kinexis {
namespace {
constexpr double pi=3.14159265358979323846,inf=std::numeric_limits<double>::infinity();
void need(bool ok,const std::string& why) {if(!ok) throw std::invalid_argument(why);}
struct Rect {double xmin,xmax,ymin,ymax;};
struct Bead {Vec3 a,b; double height,low_width,high_width,length; Rect box;};
bool overlap(Rect a,Rect b) {return a.xmin<=b.xmax && b.xmin<=a.xmax && a.ymin<=b.ymax && b.ymin<=a.ymax;}
double sq(double x) {return x*x;}
double height(const CouponConfig& c,double x,double y) {return coupon_surface(c,1,x,y)-c.layer_height;}
double nearest(double lo,double hi) {return lo<=0 && hi>=0?0:std::min(std::abs(lo),std::abs(hi));}
double number(const std::string& token) {
    need(!token.empty(),"missing extrusion width/height annotation");
    size_t used=0; double n=std::stod(token,&used);
    need(used==token.size() && std::isfinite(n),"invalid extrusion metadata number"); return n;
}
double point_segment(double x,double y,const Bead& b) {
    double dx=b.b[0]-b.a[0],dy=b.b[1]-b.a[1];
    double t=std::clamp(((x-b.a[0])*dx+(y-b.a[1])*dy)/(b.length*b.length),0.0,1.0);
    return std::hypot(x-b.a[0]-t*dx,y-b.a[1]-t*dy);
}
double point_rect(double x,double y,Rect r) {
    return std::hypot(std::max({r.xmin-x,0.0,x-r.xmax}),std::max({r.ymin-y,0.0,y-r.ymax}));
}
double minimum_distance(const Bead& b,Rect r) {
    double lo=0,hi=1; bool intersection=true;
    for(int i=0;i<2;++i) {
        double a=i?r.ymin:r.xmin,z=i?r.ymax:r.xmax,d=b.b[i]-b.a[i];
        if(d==0) {if(b.a[i]<a || b.a[i]>z) intersection=false;}
        else {
            double u=(a-b.a[i])/d,v=(z-b.a[i])/d; if(u>v) std::swap(u,v);
            lo=std::max(lo,u); hi=std::min(hi,v); if(lo>hi) intersection=false;
        }
    }
    if(intersection) return 0;
    double distance=std::min(point_rect(b.a[0],b.a[1],r),point_rect(b.b[0],b.b[1],r));
    for(double x:{r.xmin,r.xmax}) for(double y:{r.ymin,r.ymax}) distance=std::min(distance,point_segment(x,y,b));
    return distance;
}
double top(double z,double h,double w,double distance) {
    if(distance>w/2) return -inf;
    double side=std::max(0.0,distance-(w-h)/2);
    return z-h/2+std::sqrt(std::max(0.0,h*h/4-side*side));
}
double lower_top(const Bead& b,Rect r) {
    // The rounded-strip formula requires w>=h. Uncertainty outside that
    // domain supplies no guaranteed lower surface, rather than a clamped bead.
    if(b.low_width<b.height) return -inf;
    double dx=(b.b[0]-b.a[0])/b.length,dy=(b.b[1]-b.a[1])/b.length,max_distance=0;
    for(double x:{r.xmin,r.xmax}) for(double y:{r.ymin,r.ymax}) {
        double along=(x-b.a[0])*dx+(y-b.a[1])*dy;
        if(along<0 || along>b.length) return -inf;
        max_distance=std::max(max_distance,std::abs((x-b.a[0])*dy-(y-b.a[1])*dx));
    }
    return top(b.b[2],b.height,b.low_width,max_distance);
}
}
FitConfig read_fit(const std::string& path) {
    std::ifstream in(path); need(bool(in),"cannot open surface-fit config");
    FitConfig f{}; std::string key;
    auto expect=[&](const char* word) {in>>key; need(key==word,"expected fit field: "+std::string(word));};
    expect("translation"); for(auto& n:f.translation) in>>n;
    expect("home"); for(auto& n:f.home) in>>n;
    expect("filament_diameter"); in>>f.filament_diameter;
    expect("e_rounding"); in>>f.e_rounding;
    expect("width_uncertainty"); in>>f.width_uncertainty;
    expect("z_uncertainty"); in>>f.z_uncertainty;
    expect("gap_limits"); in>>f.minimum_gap>>f.maximum_gap;
    expect("max_depth"); in>>f.max_depth;
    expect("cell_budget"); in>>f.cell_budget;
    expect("work_budget"); in>>f.work_budget;
    need(!in.fail(),"invalid fit config number"); need(!(in>>key),"unexpected fit data"); return f;
}
FitResult check_substrate_fit(const CouponConfig& c,const CouponResult& motion,const GcodeReplay& g,const FitConfig& f) {
    for(double n:f.translation) need(std::isfinite(n),"invalid frame translation");
    for(double n:{f.filament_diameter,f.e_rounding,f.width_uncertainty,f.z_uncertainty,f.minimum_gap,f.maximum_gap})
        need(std::isfinite(n),"non-finite fit parameter");
    need(f.filament_diameter>0 && f.e_rounding>0 && f.e_rounding<=0.01 && f.width_uncertainty>=0 &&
         f.width_uncertainty<=0.1 && f.z_uncertainty>=0 && f.z_uncertainty<=0.1 &&
         f.minimum_gap>0 && f.maximum_gap>f.minimum_gap && f.maximum_gap<=1,"invalid fit tolerances");
    need(f.max_depth>=1 && f.max_depth<=24 && f.cell_budget>=1 && f.cell_budget<=200000 &&
         f.work_budget>=1 && f.work_budget<=100000000,"invalid fit budgets");
    need(c.substrate_thickness>0 && !motion.frames.empty(),"fit requires a supported coupon");
    FitResult result{}; result.minimum_gap_bound=inf; result.maximum_gap_bound=-inf;
    auto spend=[&] {need(result.work<static_cast<size_t>(f.work_budget),"surface-fit work budget exhausted"); ++result.work;};
    std::vector<Bead> beads;
    double debt=0,area=pi*f.filament_diameter*f.filament_diameter/4;
    size_t hint=0;
    const std::set<std::string> roles{"Skirt/Brim","Perimeter","External perimeter","Solid infill","Top solid infill","Internal infill","Gap fill"};
    for(size_t segment=0;segment<g.segments.size();++segment) {
        const auto& path=g.segments[segment];
        for(size_t sample=1;sample<path.size();++sample) {
            spend(); const auto& a=path[sample-1]; const auto& b=path[sample];
            double de=b.extrusion-a.extrusion;
            if(de<0) {debt-=de; continue;}
            if(de==0) continue;
            double length=std::hypot(b.tip[0]-a.tip[0],b.tip[1]-a.tip[1]);
            if(length==0 && b.tip[2]==a.tip[2]) {
                need(debt>0 && de<=debt+1e-8,"unmodeled planar priming/blob"); debt=std::max(0.0,debt-de); continue;
            }
            need(debt<1e-8 && length>0 && std::abs(b.tip[2]-a.tip[2])<1e-10,"nonplanar extrusion or moving recovery unsupported");
            need(hint<g.extrusions.size(),"missing extrusion annotation");
            const auto& h=g.extrusions[hint++];
            need(h.segment==segment && h.sample==sample,"extrusion annotation mismatch");
            need(roles.count(h.role)!=0,"unsupported substrate bead role at line "+std::to_string(h.line)+": "+h.role);
            double w=number(h.width),z=number(h.height);
            need(z>0 && z<=1 && w>=z && w<=3,"unsupported bead width/height");
            double inferred=de*area/(length*z)+z*(1-pi/4);
            double rounding=f.e_rounding*area/(length*z);
            need(std::isfinite(inferred) && std::abs(inferred-w)<=rounding+f.width_uncertainty+1e-6,
                 "extrusion dose disagrees with annotated width at line "+std::to_string(h.line));
            double low=std::min(w-f.width_uncertainty,inferred-rounding-f.width_uncertainty);
            double high=std::max(w+f.width_uncertainty,inferred+rounding+f.width_uncertainty);
            need(std::isfinite(high) && high<=4,"bead uncertainty too large");
            Bead bead{a.tip,b.tip,z,low,high,length,{}};
            bead.box={std::min(a.tip[0],b.tip[0])-high/2,std::max(a.tip[0],b.tip[0])+high/2,
                      std::min(a.tip[1],b.tip[1])-high/2,std::max(a.tip[1],b.tip[1])+high/2};
            beads.push_back(bead);
        }
    }
    need(!beads.empty() && hint==g.extrusions.size(),"empty or unmatched substrate deposition");
    result.beads=beads.size();
    // Query sorted candidates from highest bead down. Once a whole-cell lower
    // bound exceeds every remaining bead top, lower layers cannot affect it.
    std::vector<size_t> ordered(beads.size()); std::iota(ordered.begin(),ordered.end(),0);
    std::sort(ordered.begin(),ordered.end(),[&](size_t a,size_t b) {return beads[a].b[2]>beads[b].b[2];});
    double error=0; std::vector<Rect> regions; bool run=false;
    for(size_t i=1;i<motion.frames.size();++i) {
        const auto& a=motion.frames[i-1].source; const auto& b=motion.frames[i].source;
        double layer=b.tip[2]-height(c,b.tip[0],b.tip[1]);
        bool first=b.kind=="extrusion" && (b.layer==1 || (b.layer==0 && std::abs(layer-c.layer_height)<1e-8));
        if(first) {
            if(!run) regions.push_back({std::min(a.tip[0],b.tip[0]),std::max(a.tip[0],b.tip[0]),
                                      std::min(a.tip[1],b.tip[1]),std::max(a.tip[1],b.tip[1])});
            else {auto& r=regions.back(); r.xmin=std::min(r.xmin,b.tip[0]); r.xmax=std::max(r.xmax,b.tip[0]);
                  r.ymin=std::min(r.ymin,b.tip[1]); r.ymax=std::max(r.ymax,b.tip[1]);}
            error=std::max(error,motion.position_bounds.at(i));
        }
        run=first;
    }
    need(!regions.empty(),"no first curved layer found"); result.regions=regions.size();
    double first_fraction=coupon_fraction(c,1);
    double gradient=first_fraction*std::hypot(2*c.curvature_x*std::max(std::abs(c.x_min),std::abs(c.x_max)),
                               2*c.curvature_y*std::max(std::abs(c.y_min),std::abs(c.y_max)));
    double padding=c.bead_width/2+c.layer_height*gradient+error;
    // Include the local-tangent versus quadratic-height residual across the
    // conservative first-layer footprint, in addition to controller chord error.
    double target_error=error+first_fraction*std::max(c.curvature_x,c.curvature_y)*padding*padding;
    std::function<void(Rect,const std::vector<size_t>&,int)> evaluate;
    evaluate=[&](Rect local,const std::vector<size_t>& parent,int depth) {
        spend();
        Rect rect{local.xmin+f.translation[0],local.xmax+f.translation[0],local.ymin+f.translation[1],local.ymax+f.translation[1]};
        double lower=-inf,upper=-inf;
        bool possible_lower=false;
        std::vector<size_t> candidates;
        for(size_t id:parent) {
            spend(); const auto& bead=beads[id];
            if(bead.b[2]+f.z_uncertainty<lower) break;
            if(!overlap(bead.box,rect)) continue;
            double distance=minimum_distance(bead,rect); if(distance>bead.high_width/2) continue;
            candidates.push_back(id);
            possible_lower=possible_lower || bead.low_width>=bead.height;
            upper=std::max(upper,top(bead.b[2],bead.height,bead.high_width,distance)+f.z_uncertainty);
            lower=std::max(lower,lower_top(bead,rect)-f.z_uncertainty);
        }
        double desired_min=height(c,nearest(local.xmin,local.xmax),nearest(local.ymin,local.ymax))+c.layer_height+f.translation[2]-target_error;
        double desired_max=height(c,std::max(std::abs(local.xmin),std::abs(local.xmax)),
                                  std::max(std::abs(local.ymin),std::abs(local.ymax)))+c.layer_height+f.translation[2]+target_error;
        double low_gap=desired_min-upper,high_gap=desired_max-lower;
        bool pass=std::isfinite(lower) && low_gap>=f.minimum_gap && high_gap<=f.maximum_gap;
        bool fail=!std::isfinite(upper) || high_gap<f.minimum_gap || low_gap>f.maximum_gap;
        if(!pass && !fail && possible_lower && depth<f.max_depth) {
            if(local.xmax-local.xmin>=local.ymax-local.ymin) {
                double mid=(local.xmin+local.xmax)/2;
                need(mid>local.xmin && mid<local.xmax,"fit subdivision precision exhausted");
                evaluate({local.xmin,mid,local.ymin,local.ymax},candidates,depth+1);
                evaluate({mid,local.xmax,local.ymin,local.ymax},candidates,depth+1);
            } else {
                double mid=(local.ymin+local.ymax)/2;
                need(mid>local.ymin && mid<local.ymax,"fit subdivision precision exhausted");
                evaluate({local.xmin,local.xmax,local.ymin,mid},candidates,depth+1);
                evaluate({local.xmin,local.xmax,mid,local.ymax},candidates,depth+1);
            }
            return;
        }
        need(result.cells.size()<static_cast<size_t>(f.cell_budget),"fit cell budget exhausted");
        std::string status=pass?"pass":fail?"fail":"uncertain";
        if(pass) ++result.passed; else if(fail) ++result.failed; else ++result.uncertain;
        result.minimum_gap_bound=std::min(result.minimum_gap_bound,low_gap);
        result.maximum_gap_bound=std::max(result.maximum_gap_bound,high_gap);
        result.cells.push_back({local.xmin,local.xmax,local.ymin,local.ymax,lower,upper,low_gap,high_gap,status});
    };
    for(auto region:regions) {
        region.xmin-=padding; region.xmax+=padding; region.ymin-=padding; region.ymax+=padding;
        evaluate(region,ordered,0);
    }
    return result;
}
}
