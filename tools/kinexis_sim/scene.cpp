#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace kinexis {
namespace {
constexpr double pi=3.14159265358979323846;
void need(bool ok,const std::string& message) {if(!ok) throw std::invalid_argument(message);}
double sq(double n) {return n*n;}
double slope(double a) {return std::tan(a*pi/180);}
double nearest_zero(double a,double b) {return a<=0 && b>=0?0:std::min(std::abs(a),std::abs(b));}
bool overlap(const Box& a,const Box& b) {
    for(size_t i=0;i<3;++i) if(a.maximum[i]<b.minimum[i] || b.maximum[i]<a.minimum[i]) return false;
    return true;
}
bool segment_box(Vec3 a,Vec3 b,Box box) {
    double lo=0,hi=1;
    for(size_t i=0;i<3;++i) {
        double d=b[i]-a[i];
        if(d==0) {if(a[i]<box.minimum[i] || a[i]>box.maximum[i]) return false;}
        else {
            double x=(box.minimum[i]-a[i])/d,y=(box.maximum[i]-a[i])/d;
            if(x>y) std::swap(x,y);
            lo=std::max(lo,x); hi=std::min(hi,y);
            if(lo>hi) return false;
        }
    }
    return true;
}
Vec3 triangle_normal(const Mesh& mesh,const std::array<size_t,3>& triangle) {
    Vec3 a=mesh.vertices.at(triangle[0]),b=mesh.vertices.at(triangle[1]),c=mesh.vertices.at(triangle[2]);
    Vec3 u{b[0]-a[0],b[1]-a[1],b[2]-a[2]},v{c[0]-a[0],c[1]-a[1],c[2]-a[2]};
    Vec3 n{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
    double length=std::hypot(n[0],n[1],n[2]); need(length>0 && std::isfinite(length),"degenerate substrate triangle");
    for(double& value:n) value/=length;
    return n;
}
void validate(const Scene& s) {
    for(double n:{s.bed_x_min,s.bed_x_max,s.bed_y_min,s.bed_y_max,s.substrate_margin,s.clearance_margin,s.tip_radius})
        need(std::isfinite(n),"non-finite scene parameter");
    need(s.bed_x_min<s.bed_x_max && s.bed_y_min<s.bed_y_max,"invalid bed bounds");
    need(s.substrate_margin>0 && s.clearance_margin>=1e-5 && s.clearance_margin<=1 && s.tip_radius>0,
         "invalid scene margin/tip radius");
    need(s.grid_x>=2 && s.grid_x<=256 && s.grid_y>=2 && s.grid_y<=256,"invalid substrate grid");
    need(!s.capsules.empty() && s.capsules.size()<=16,"scene needs 1-16 tool capsules");
    need(s.world_boxes.size()+s.bed_boxes.size()<=32,"too many scene boxes");
    std::set<std::string> names;
    auto name=[&](const std::string& n) {need(!n.empty() && names.insert(n).second,"duplicate/empty scene name");};
    for(const auto& c:s.capsules) {
        name(c.name);
        need(std::isfinite(c.radius) && std::isfinite(c.bottom) && std::isfinite(c.top),"invalid capsule number");
        need(c.radius>0 && c.bottom>=0 && c.top>=c.bottom,"invalid vertical capsule");
    }
    for(const auto& boxes:{s.world_boxes,s.bed_boxes}) for(const auto& b:boxes) {
        name(b.name);
        for(size_t i=0;i<3;++i) need(std::isfinite(b.minimum[i]) && std::isfinite(b.maximum[i]) &&
                                   b.minimum[i]<b.maximum[i],"invalid scene box");
    }
}
Mesh make_substrate(const CouponConfig& c,const Scene& s) {
    Mesh mesh; int nx=s.grid_x,ny=s.grid_y;
    double xmin=c.x_min-s.substrate_margin,xmax=c.x_max+s.substrate_margin;
    double ymin=c.y_min-s.substrate_margin,ymax=c.y_max+s.substrate_margin;
    need(xmin>=s.bed_x_min && xmax<=s.bed_x_max && ymin>=s.bed_y_min && ymax<=s.bed_y_max,
         "substrate extends outside bed fixture");
    auto index=[&](int i,int j,int top) {return static_cast<size_t>(top*(nx+1)*(ny+1)+j*(nx+1)+i);};
    for(int top=0;top<=1;++top) for(int j=0;j<=ny;++j) for(int i=0;i<=nx;++i) {
        double x=xmin+(xmax-xmin)*i/nx,y=ymin+(ymax-ymin)*j/ny;
        double z=top?c.substrate_thickness+c.curvature_x*x*x+c.curvature_y*y*y:0;
        need(std::isfinite(z) && z>=0,"invalid substrate geometry");
        mesh.vertices.push_back({x,y,z});
    }
    auto tri=[&](size_t a,size_t b,size_t d) {mesh.triangles.push_back({a,b,d});};
    for(int j=0;j<ny;++j) for(int i=0;i<nx;++i) {
        auto a=index(i,j,1),b=index(i+1,j,1),d=index(i+1,j+1,1),e=index(i,j+1,1);
        tri(a,b,d); tri(a,d,e);
        a=index(i,j,0); b=index(i+1,j,0); d=index(i+1,j+1,0); e=index(i,j+1,0);
        tri(a,d,b); tri(a,e,d);
    }
    auto wall=[&](size_t a,size_t b,size_t top_a,size_t top_b) {tri(a,b,top_b); tri(a,top_b,top_a);};
    for(int i=0;i<nx;++i) {
        wall(index(i,0,0),index(i+1,0,0),index(i,0,1),index(i+1,0,1));
        wall(index(i+1,ny,0),index(i,ny,0),index(i+1,ny,1),index(i,ny,1));
    }
    for(int j=0;j<ny;++j) {
        wall(index(0,j+1,0),index(0,j,0),index(0,j+1,1),index(0,j,1));
        wall(index(nx,j,0),index(nx,j+1,0),index(nx,j,1),index(nx,j+1,1));
    }
    for(const auto& triangle:mesh.triangles) triangle_normal(mesh,triangle);
    return mesh;
}
}
Scene read_scene(const std::string& path) {
    std::ifstream in(path); need(bool(in),"cannot open collision scene");
    Scene s{}; std::set<std::string> fields; std::string line;
    while(std::getline(in,line)) {
        std::istringstream row(line); std::string key; if(!(row>>key)) continue;
        if(key=="capsule") {
            Capsule c{}; row>>c.name>>c.radius>>c.bottom>>c.top; s.capsules.push_back(c);
        } else if(key=="world_box" || key=="bed_box") {
            Box b{}; row>>b.name; for(double& n:b.minimum) row>>n; for(double& n:b.maximum) row>>n;
            (key=="world_box"?s.world_boxes:s.bed_boxes).push_back(b);
        } else {
            need(fields.insert(key).second,"duplicate scene field");
            if(key=="bed_bounds") row>>s.bed_x_min>>s.bed_x_max>>s.bed_y_min>>s.bed_y_max;
            else if(key=="substrate_margin") row>>s.substrate_margin;
            else if(key=="substrate_grid") row>>s.grid_x>>s.grid_y;
            else if(key=="clearance_margin") row>>s.clearance_margin;
            else if(key=="tip_radius") row>>s.tip_radius;
            else throw std::invalid_argument("unknown scene field: "+key);
        }
        need(!row.fail(),"invalid collision scene row"); std::string extra; need(!(row>>extra),"extra collision scene data");
    }
    need(fields==std::set<std::string>{"bed_bounds","substrate_margin","substrate_grid","clearance_margin","tip_radius"},
         "missing scene field");
    validate(s); return s;
}
SceneResult check_scene(const Machine& machine,const CouponConfig& c,const CouponResult& motion,const Scene& s) {
    kinexis::validate(machine);
    validate(s);
    need(c.substrate_thickness>0,"scene check requires positive matched substrate thickness");
    need(!motion.frames.empty() && motion.frames.size()==motion.samples.size() &&
         motion.frames.size()==motion.position_bounds.size(),"invalid coupon motion");
    need(s.substrate_margin>c.bead_width/2+motion.max_position_error_bound,
         "substrate margin does not cover the deposition footprint");
    SceneResult result{make_substrate(c,s),0,motion.frames.size()};
    double dx=(c.x_max-c.x_min+2*s.substrate_margin)/s.grid_x;
    double dy=(c.y_max-c.y_min+2*s.substrate_margin)/s.grid_y;
    result.substrate_error_bound=(c.curvature_x*dx*dx+c.curvature_y*dy*dy)/4;
    need(std::isfinite(result.substrate_error_bound),"substrate error overflow");
    double surface_slope=std::hypot(2*c.curvature_x*std::max(std::abs(c.x_min),std::abs(c.x_max)),
                                    2*c.curvature_y*std::max(std::abs(c.y_min),std::abs(c.y_max)));
    double printed_padding=c.bead_width*(surface_slope+2*std::max(c.curvature_x,c.curvature_y)*motion.max_position_error_bound)+
                           motion.max_position_error_bound*(1+surface_slope);
    Box footprint{"substrate",{c.x_min-s.substrate_margin,c.y_min-s.substrate_margin,0},
                             {c.x_max+s.substrate_margin,c.y_max+s.substrate_margin,0}};
    for(size_t i=0;i<motion.frames.size();++i) {
        const auto& a=motion.frames[i?i-1:0]; const auto& b=motion.frames[i];
        const auto& p=a.source; const auto& q=b.source;
        double error=motion.position_bounds[i];
        double sx=std::max(std::abs(slope(p.pose.pitch)),std::abs(slope(q.pose.pitch)));
        double sy=std::max(std::abs(slope(p.pose.roll)),std::abs(slope(q.pose.roll)));
        double sn=std::max(std::hypot(slope(p.pose.pitch),slope(p.pose.roll)),
                           std::hypot(slope(q.pose.pitch),slope(q.pose.roll)));
        double cosine=1/std::sqrt(1+sn*sn);
        double minz=std::min(p.tip[2],q.tip[2]);
        if(q.kind=="extrusion") {
            double layer=q.tip[2]-c.curvature_x*q.tip[0]*q.tip[0]-c.curvature_y*q.tip[1]*q.tip[1];
            minz=layer+c.curvature_x*sq(nearest_zero(std::min(p.tip[0],q.tip[0]),std::max(p.tip[0],q.tip[0])))+
                       c.curvature_y*sq(nearest_zero(std::min(p.tip[1],q.tip[1]),std::max(p.tip[1],q.tip[1])));
        }
        auto local_box=[&](double radius,double bottom,double top,bool disk) {
            double r=radius+s.clearance_margin;
            Box bound{"tool",{}, {}};
            bound.minimum[0]=std::min(p.tip[0],q.tip[0])-error-top*sx-r;
            bound.maximum[0]=std::max(p.tip[0],q.tip[0])+error+top*sx+r;
            bound.minimum[1]=std::min(p.tip[1],q.tip[1])-error-top*sy-r;
            bound.maximum[1]=std::max(p.tip[1],q.tip[1])+error+top*sy+r;
            bound.minimum[2]=minz-error+bottom*cosine-(disk?radius*sn*cosine+s.clearance_margin:r);
            bound.maximum[2]=std::max(p.tip[2],q.tip[2])+error+top+r;
            for(double n:bound.minimum) need(std::isfinite(n),"tool bound overflow");
            for(double n:bound.maximum) need(std::isfinite(n),"tool bound overflow");
            return bound;
        };
        auto check=[&](const std::string& name,double radius,double bottom,double top,bool disk) {
            std::string where="interval "+std::to_string(i)+", "+name+": ";
            Box local=local_box(radius,bottom,top,disk);
            need(local.minimum[2]>0,where+"bed-plane clearance not established");
            for(const auto& obstacle:s.bed_boxes)
                need(!overlap(local,obstacle),where+"possible collision with bed box "+obstacle.name);
            if(local.maximum[0]>=footprint.minimum[0] && local.minimum[0]<=footprint.maximum[0] &&
               local.maximum[1]>=footprint.minimum[1] && local.minimum[1]<=footprint.maximum[1]) {
                double x=std::max(std::abs(std::max(local.minimum[0],footprint.minimum[0])),
                                  std::abs(std::min(local.maximum[0],footprint.maximum[0])));
                double y=std::max(std::abs(std::max(local.minimum[1],footprint.minimum[1])),
                                  std::abs(std::min(local.maximum[1],footprint.maximum[1])));
                double ceiling=c.substrate_thickness+c.curvature_x*x*x+c.curvature_y*y*y+
                    (disk?result.substrate_error_bound:c.layers*c.layer_height+printed_padding);
                need(local.minimum[2]>ceiling,where+"substrate/material envelope clearance not established");
            }
            for(auto box:s.world_boxes) {
                double r=radius+s.clearance_margin;
                box.minimum[0]-=r; box.maximum[0]+=r;
                box.minimum[1]-=r; box.maximum[1]+=r;
                box.minimum[2]-=top+(disk?s.clearance_margin:r);
                box.maximum[2]-=bottom-(disk?s.clearance_margin:r);
                need(!segment_box(a.tip,b.tip,box),where+"possible swept collision with world box "+box.name);
            }
        };
        check("tip_disk",s.tip_radius,0,0,true);
        for(const auto& capsule:s.capsules) check(capsule.name,capsule.radius,capsule.bottom,capsule.top,false);
    }
    return result;
}
std::string substrate_stl(const Mesh& mesh) {
    std::ostringstream out; out<<std::setprecision(17)<<"solid kinexis_virtual_substrate\n";
    for(const auto& triangle:mesh.triangles) {
        Vec3 n=triangle_normal(mesh,triangle);
        out<<" facet normal "<<n[0]<<' '<<n[1]<<' '<<n[2]<<"\n  outer loop\n";
        for(auto index:triangle) {const auto& p=mesh.vertices[index]; out<<"   vertex "<<p[0]<<' '<<p[1]<<' '<<p[2]<<'\n';}
        out<<"  endloop\n endfacet\n";
    }
    out<<"endsolid kinexis_virtual_substrate\n"; return out.str();
}
}
