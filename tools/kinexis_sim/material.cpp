#include "material.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace kinexis {
namespace {
constexpr double pi=3.14159265358979323846, epsilon=1e-9;
struct Bounds {Vec3 minimum,maximum;};
struct Cell {Bounds box; size_t track,interval;};
void need(bool ok,const std::string& why) {if(!ok) throw std::invalid_argument(why);}
double square(double n) {return n*n;}
double slope(double n) {return std::tan(n*pi/180);}
bool close(double a,double b) {return std::abs(a-b)<=epsilon;}
bool same_pose(Pose a,Pose b) {return close(a.lift,b.lift) && close(a.pitch,b.pitch) && close(a.roll,b.roll);}
double min_z(const CouponConfig& c,const Sample& a,const Sample& b,bool deposition) {
    if(!deposition) return std::min(a.tip[2],b.tip[2]);
    return coupon_minimum_z(c,a,b);
}
bool overlap(const Bounds& a,const Bounds& b) {
    for(size_t j=0;j<3;++j) if(a.maximum[j]<b.minimum[j] || b.maximum[j]<a.minimum[j]) return false;
    return true;
}
void finite(const Bounds& b) {
    for(size_t j=0;j<3;++j) need(std::isfinite(b.minimum[j]) && std::isfinite(b.maximum[j]) &&
                               b.minimum[j]<=b.maximum[j],"invalid material interval bound");
}
}
MaterialResult check_material(const CouponConfig& c,const CouponResult& motion,double radius,double margin,size_t budget) {
    for(double n:{c.curvature_x,c.curvature_y,c.layer_height,c.bead_width,c.filament_diameter,
                  c.substrate_thickness,c.bed_lift,radius,margin}) need(std::isfinite(n),"non-finite material parameter");
    need(c.curvature_x>=0 && c.curvature_y>=0 && c.layer_height>0 && c.bead_width>0 &&
         c.filament_diameter>0 && c.substrate_thickness>=0 && radius>0 && margin>=1e-5 && c.layers>0,
         "invalid material parameters");
    need(budget>0,"material work budget exhausted");
    need(!motion.frames.empty() && motion.frames.size()==motion.position_bounds.size(),"invalid material replay");
    need(close(motion.frames.front().source.extrusion,0),"material replay requires initial E=0");
    MaterialResult result;
    auto spend=[&] {need(result.work<budget,"material work budget exhausted"); ++result.work;};
    std::vector<Cell> history;
    std::vector<size_t> seen;
    std::map<std::array<int,3>,std::vector<size_t>> grid;
    const double bin_size=std::max({0.5,c.bead_width,2*radius});
    need(std::isfinite(bin_size),"material index scale overflow");
    auto bins=[&](const Bounds& box,const auto& visit) {
        finite(box);
        int lo[3],hi[3]; long long cells=1;
        for(size_t j=0;j<3;++j) {
            double a=std::floor(box.minimum[j]/bin_size),b=std::floor(box.maximum[j]/bin_size);
            need(std::isfinite(a) && std::isfinite(b) && a>=-1000000 && b<=1000000,"material index range exhausted");
            lo[j]=static_cast<int>(a); hi[j]=static_cast<int>(b);
            cells*=hi[j]-lo[j]+1; need(cells<=4096,"material index query too large");
        }
        for(int x=lo[0];x<=hi[0];++x) for(int y=lo[1];y<=hi[1];++y) for(int z=lo[2];z<=hi[2];++z)
            {spend(); visit(std::array<int,3>{x,y,z});}
    };
    bool in_track=false,departure=false;
    size_t track=0,last_track=0;
    Vec3 direction{},last_tip{}; Pose last_pose{};
    double track_layer=0,debt=0;
    for(size_t i=0;i<motion.frames.size();++i) {
        spend(); ++result.intervals_checked;
        const auto& a=motion.frames[i?i-1:0].source; const auto& b=motion.frames[i].source;
        std::string where="material interval "+std::to_string(i)+": ";
        for(const auto* sample:{&a,&b}) {
            for(double n:sample->tip) need(std::isfinite(n),where+"non-finite tip");
            for(double n:{sample->time,sample->extrusion,sample->pose.lift,sample->pose.pitch,sample->pose.roll})
                need(std::isfinite(n),where+"non-finite material state");
        }
        need(b.time>=0 && (i==0 || b.time>a.time),where+"invalid chronology");
        double error=motion.position_bounds[i]; need(std::isfinite(error) && error>=0,where+"invalid position bound");
        bool deposit=i>0 && b.kind=="extrusion";
        double dx=b.tip[0]-a.tip[0],dy=b.tip[1]-a.tip[1],length=std::hypot(dx,dy);
        double de=b.extrusion-a.extrusion;
        need(std::isfinite(length) && std::isfinite(de),where+"non-finite material motion");
        bool stationary=close(dx,0) && close(dy,0) && close(a.tip[2],b.tip[2]) && same_pose(a.pose,b.pose);
        int layer_id=deposit?coupon_layer(c,b):0;
        double fraction=coupon_fraction(c,layer_id);
        double gap_max=deposit?std::max(coupon_spacing(c,layer_id,a.tip[0],a.tip[1]),
                                      coupon_spacing(c,layer_id,b.tip[0],b.tip[1])):0;
        if(deposit) {
            need(de>0 && length>0 && debt<=epsilon,where+"deposition requires recovered E and nonzero XY motion");
            double layer=layer_id*c.layer_height;
            need(close(a.tip[2],coupon_surface(c,layer_id,a.tip[0],a.tip[1])) &&
                 close(b.tip[2],coupon_surface(c,layer_id,b.tip[0],b.tip[1])),
                 where+"deposition leaves the configured curved layer");
            for(const auto* s:{&a,&b}) {
                need(close(slope(s->pose.pitch),-2*fraction*c.curvature_x*s->tip[0]) &&
                     close(slope(s->pose.roll),-2*fraction*c.curvature_y*s->tip[1]) && close(s->pose.lift,c.bed_lift),
                     where+"deposition pose does not match the surface normal");
            }
            if(!in_track) {
                track=++result.tracks; direction={dx/length,dy/length,0}; track_layer=layer;
            } else {
                need(close(layer,track_layer) && dx*direction[0]+dy*direction[1]>0 &&
                     std::abs(dx*direction[1]-dy*direction[0])<=epsilon,
                     where+"track reversal/turn or layer change is unsupported");
            }
            double gx=fraction*c.curvature_x*(a.tip[0]+b.tip[0]),gy=fraction*c.curvature_y*(a.tip[1]+b.tip[1]);
            double midpoint_gap=coupon_spacing(c,layer_id,(a.tip[0]+b.tip[0])/2,(a.tip[1]+b.tip[1])/2);
            double estimate=c.bead_width*midpoint_gap*std::hypot(length,gx*dx+gy*dy)/std::sqrt(1+gx*gx+gy*gy);
            double grad_delta=fraction*std::hypot(2*c.curvature_x*dx,2*c.curvature_y*dy);
            double full_gradient=std::max(std::hypot(2*c.curvature_x*a.tip[0],2*c.curvature_y*a.tip[1]),
                                          std::hypot(2*c.curvature_x*b.tip[0],2*c.curvature_y*b.tip[1]));
            double delta_fraction=fraction-coupon_fraction(c,layer_id-1);
            double uncertainty=c.bead_width*(delta_fraction*full_gradient*length*length*std::sqrt(2.0)+
                gap_max*length*grad_delta*(1+std::sqrt(2.0)))/4;
            double volume=de*pi*square(c.filament_diameter)/4;
            need(std::isfinite(volume) && std::isfinite(uncertainty) &&
                 std::abs(volume-estimate)<=uncertainty+1e-9*std::max(1.0,estimate),where+"E disagrees with bead envelope");
        } else if(de<0) debt-=de;
        else if(de>0) {
            need(stationary && debt>0 && de<=debt+epsilon,where+"unmodeled priming/blob or moving recovery");
            debt=std::max(0.0,debt-de);
        }
        need(std::isfinite(debt),where+"retraction ledger overflow");
        bool endpoint_departure=!deposit && departure && close(a.tip[0],last_tip[0]) && close(a.tip[1],last_tip[1]) &&
            close(b.tip[0],last_tip[0]) && close(b.tip[1],last_tip[1]) && a.tip[2]>=last_tip[2]-epsilon &&
            b.tip[2]>=a.tip[2]-epsilon && same_pose(a.pose,last_pose) && same_pose(b.pose,last_pose);
        if(!deposit && !endpoint_departure) departure=false;
        size_t contact_track=deposit?track:endpoint_departure?last_track:0;
        double sn=std::max(std::hypot(slope(a.pose.pitch),slope(a.pose.roll)),
                           std::hypot(slope(b.pose.pitch),slope(b.pose.roll)));
        need(std::isfinite(sn) && sn<=1+epsilon,where+"pose leaves supported slope domain");
        double zspread=radius*sn/std::sqrt(1+sn*sn);
        Bounds tip;
        for(size_t j=0;j<2;++j) {
            tip.minimum[j]=std::min(a.tip[j],b.tip[j])-error-radius-margin;
            tip.maximum[j]=std::max(a.tip[j],b.tip[j])+error+radius+margin;
        }
        tip.minimum[2]=min_z(c,a,b,deposit)-error-zspread-margin;
        tip.maximum[2]=std::max(a.tip[2],b.tip[2])+error+zspread+margin;
        std::vector<size_t> candidates;
        bins(tip,[&](const auto& key) {
            auto found=grid.find(key); if(found==grid.end()) return;
            for(size_t id:found->second) {
                spend();
                if(seen[id]!=i+1) {seen[id]=i+1; candidates.push_back(id);}
            }
        });
        for(size_t id:candidates) {
            spend(); ++result.candidate_pairs;
            const auto& old=history[id]; if(!overlap(tip,old.box)) continue;
            if(contact_track!=0 && old.track==contact_track) {++result.permitted_contact_pairs; continue;}
            throw std::invalid_argument(where+"tip clearance from prior material not established (track "+
                                        std::to_string(old.track)+", interval "+std::to_string(old.interval)+")");
        }
        if(deposit) {
            double gs=fraction*std::max(std::hypot(2*c.curvature_x*a.tip[0],2*c.curvature_y*a.tip[1]),
                                        std::hypot(2*c.curvature_x*b.tip[0],2*c.curvature_y*b.tip[1]));
            need(std::isfinite(gs) && gs<=1+epsilon,where+"bead leaves supported gradient domain");
            double pad=c.bead_width/2+gap_max*gs+error;
            Bounds bead;
            for(size_t j=0;j<2;++j) {
                bead.minimum[j]=std::min(a.tip[j],b.tip[j])-pad;
                bead.maximum[j]=std::max(a.tip[j],b.tip[j])+pad;
            }
            bead.minimum[2]=min_z(c,a,b,true)-gap_max-c.bead_width*gs/2-error;
            bead.maximum[2]=std::max(a.tip[2],b.tip[2])+c.bead_width*gs/2+error;
            finite(bead);
            need(history.size()<100000,"material history size exhausted");
            size_t id=history.size(); history.push_back({bead,track,i}); seen.push_back(0);
            bins(bead,[&](const auto& key) {grid[key].push_back(id);});
            ++result.deposited_cells;
            in_track=true; departure=true; last_track=track; last_tip=b.tip; last_pose=b.pose;
        } else in_track=false;
    }
    return result;
}
}
