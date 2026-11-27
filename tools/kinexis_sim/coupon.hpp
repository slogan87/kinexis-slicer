#pragma once
#include "bed.hpp"

namespace kinexis {
struct CouponConfig {
    double curvature_x, curvature_y;
    double x_min, x_max, y_min, y_max;
    int rows, layers;
    double layer_height, bead_width, filament_diameter;
    double bed_lift, clearance_z, clearance_margin, retract;
    double print_speed, travel_speed;
    double position_tolerance, alignment_tolerance_deg, pose_step_deg, volume_error_per_mm;
    int point_budget;
    double substrate_thickness=0;
    int transition_layers=0;
    double minimum_layer_gap=0,maximum_layer_gap=0;
    size_t material_work_budget=2000000;
};
struct CouponResult {
    std::vector<Sample> samples;
    std::vector<Frame> frames;
    // One incoming segment bound per frame; first frame has bound zero.
    std::vector<double> position_bounds;
    double volume=0, volume_error_bound=0, max_position_error_bound=0;
    double max_alignment_error_bound_deg=0, travel_clearance_lower_bound=0;
    double deposited_filament=0;
    double minimum_interlayer_gap=0,maximum_interlayer_gap=0;
};
double coupon_fraction(const CouponConfig& config,int layer);
double coupon_surface(const CouponConfig& config,int layer,double x,double y);
double coupon_spacing(const CouponConfig& config,int layer,double x,double y);
int coupon_layer(const CouponConfig& config,const Sample& sample);
double coupon_minimum_z(const CouponConfig& config,const Sample& a,const Sample& b);
CouponConfig read_coupon(const std::string& path);
CouponResult make_coupon(const Machine& machine,const CouponConfig& config);
}
