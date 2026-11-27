#pragma once
#include "bed.hpp"
#include <string>

namespace kinexis {
struct ExtrusionAnnotation {
    size_t segment,sample,line;
    std::string role,width,height;
};
struct GcodeReplay {
    std::vector<std::vector<Sample>> segments;
    size_t moves=0, homing_commands=0, state_commands=0;
    Vec3 final_tip{};
    double net_extrusion=0;
    std::vector<ExtrusionAnnotation> extrusions;
};
// Explicit test-fixture home position, mm/absolute XYZ initial state, no E mode
// until M82/M83. Homing starts a new replay segment; its path is not modeled.
GcodeReplay decode_gcode(const std::string& bytes, Vec3 home);
void verify_planar_replay(const Machine& machine, const GcodeReplay& replay, bool enabled);
}
