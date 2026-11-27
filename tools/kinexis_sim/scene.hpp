#pragma once
#include "coupon.hpp"

namespace kinexis {
struct Box {std::string name; Vec3 minimum,maximum;};
struct Capsule {std::string name; double radius,bottom,top;};
struct Scene {
    double bed_x_min,bed_x_max,bed_y_min,bed_y_max;
    double substrate_margin,clearance_margin,tip_radius;
    int grid_x,grid_y;
    std::vector<Capsule> capsules;
    std::vector<Box> world_boxes,bed_boxes;
};
struct Mesh {std::vector<Vec3> vertices; std::vector<std::array<size_t,3>> triangles;};
struct SceneResult {
    Mesh substrate;
    double substrate_error_bound;
    size_t intervals_checked;
};
Scene read_scene(const std::string& path);
SceneResult check_scene(const Machine& machine,const CouponConfig& coupon,
                        const CouponResult& motion,const Scene& scene);
std::string substrate_stl(const Mesh& mesh);
}
