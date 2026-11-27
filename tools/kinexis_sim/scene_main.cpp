#include "scene.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        if(argc!=4 && (argc!=6 || std::string(argv[4])!="--substrate-stl"))
            throw std::invalid_argument("usage: kinexis-scene-check MACHINE COUPON SCENE [--substrate-stl NEW_FILE]");
        auto machine=kinexis::read_machine(argv[1]); auto coupon=kinexis::read_coupon(argv[2]);
        auto motion=kinexis::make_coupon(machine,coupon);
        auto checked=kinexis::check_scene(machine,coupon,motion,kinexis::read_scene(argv[3]));
        if(argc==6) {
            if(std::filesystem::exists(argv[5])) throw std::invalid_argument("substrate output must be a new file");
            std::string mesh=kinexis::substrate_stl(checked.substrate);
            std::ofstream out(argv[5]); if(!out) throw std::runtime_error("cannot create substrate output");
            out<<mesh; out.close();
            if(!out) {std::filesystem::remove(argv[5]); throw std::runtime_error("substrate write failed");}
        }
        std::cout<<std::setprecision(17)<<"{\"intervals_checked\":"<<checked.intervals_checked
            <<",\"substrate_triangles\":"<<checked.substrate.triangles.size()
            <<",\"substrate_error_bound_mm\":"<<checked.substrate_error_bound
            <<",\"scene_collision_checked\":true,\"substrate_geometry_generated\":true"
            <<",\"substrate_manufacturing_planned\":false,\"tip_vs_prior_tracks_checked\":false"
            <<",\"printer_safe\":false}\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"Scene rejected: "<<e.what()<<'\n'; return 1;}
}
