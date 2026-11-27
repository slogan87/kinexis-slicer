#pragma once
#include "coupon.hpp"

namespace kinexis {
struct MaterialResult {
    size_t intervals_checked=0, deposited_cells=0, tracks=0;
    size_t candidate_pairs=0, permitted_contact_pairs=0, work=0;
};
// Generated quadratic-coupon trajectories only. Bounded local-tangent beads,
// chronological insertion, monotone straight-track contact policy.
MaterialResult check_material(const CouponConfig& coupon,const CouponResult& motion,
                              double tip_radius,double separation_margin,
                              size_t work_budget=2000000);
}
