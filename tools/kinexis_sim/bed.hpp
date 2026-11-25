#pragma once
#include <array>
#include <string>
#include <vector>

namespace kinexis {
using Vec3 = std::array<double, 3>;
struct Actuator {
    std::string name;
    double x, y, sign, zero, minimum, maximum, speed;
};
struct Machine {
    double pivot_x, pivot_y;
    double pitch_limit, roll_limit;
    Vec3 minimum, maximum, speed;
    double extruder_speed;
    std::array<Actuator, 3> actuators;
};
// Pitch/roll are slope angles of the bed in world XZ/YZ, not Euler angles.
struct Pose { double lift = 0, pitch = 0, roll = 0; };
struct Sample {
    double time;
    std::string kind;
    Vec3 tip;
    double extrusion;
    Pose pose;
};
struct Frame { Sample source; Vec3 tip; Vec3 actuators; Pose reconstructed; };
void validate(const Machine& machine);
Vec3 inverse(const Machine& machine, Pose pose);
Pose forward(const Machine& machine, Vec3 commands);
Vec3 transform(const Machine& machine, Pose pose, Vec3 local_tip);
std::vector<Frame> simulate(const Machine& machine, const std::vector<Sample>& samples,
                            bool enabled = true);
Machine read_machine(const std::string& path);
std::vector<Sample> read_samples(const std::string& path);
}
