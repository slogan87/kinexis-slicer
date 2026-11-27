#pragma once
#include "gcode.hpp"
#include "coupon.hpp"

namespace kinexis {
struct FitConfig {
    Vec3 translation,home;
    double filament_diameter,e_rounding,width_uncertainty,z_uncertainty;
    double minimum_gap,maximum_gap;
    int max_depth,cell_budget,work_budget;
};
struct FitCell {
    double xmin,xmax,ymin,ymax,lower_top,upper_top,lower_gap,upper_gap;
    std::string status;
};
struct FitResult {
    size_t beads=0,regions=0,passed=0,failed=0,uncertain=0,work=0;
    double minimum_gap_bound,maximum_gap_bound;
    std::vector<FitCell> cells;
    bool passed_fit() const {return passed>0 && failed==0 && uncertain==0;}
};
FitConfig read_fit(const std::string& path);
FitResult check_substrate_fit(const CouponConfig& coupon,const CouponResult& motion,
                             const GcodeReplay& planar,const FitConfig& fit);
}
