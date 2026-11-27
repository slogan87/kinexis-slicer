#include "coupon.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        if(argc!=3 && (argc!=5 || std::string(argv[3])!="--csv"))
            throw std::invalid_argument("usage: kinexis-coupon MACHINE CONFIG [--csv NEW_FILE]");
        auto machine=kinexis::read_machine(argv[1]);
        auto result=kinexis::make_coupon(machine,kinexis::read_coupon(argv[2]));
        if(argc==5) {
            std::filesystem::path path=argv[4];
            if(std::filesystem::exists(path)) throw std::invalid_argument("coupon CSV must be a new file");
            std::ofstream out(path);
            if(!out) throw std::runtime_error("cannot create coupon CSV");
            out<<std::setprecision(17)<<"time,kind,x,y,z,e,lift,pitch,roll,world_x,world_y,world_z";
            for(const auto& axis:machine.actuators) out<<",axis_"<<axis.name;
            out<<",position_bound\n";
            for(size_t i=0;i<result.frames.size();++i) {
                const auto& f=result.frames[i]; const auto& s=f.source;
                out<<s.time<<','<<s.kind;
                for(double n:s.tip) out<<','<<n;
                out<<','<<s.extrusion<<','<<s.pose.lift<<','<<s.pose.pitch<<','<<s.pose.roll;
                for(double n:f.tip) out<<','<<n;
                for(double n:f.actuators) out<<','<<n;
                out<<','<<result.position_bounds[i]<<'\n';
            }
            out.close();
            if(!out) {std::filesystem::remove(path); throw std::runtime_error("coupon CSV write failed");}
        }
        std::cout<<std::setprecision(17)<<"{\"samples\":"<<result.samples.size()
            <<",\"volume_mm3\":"<<result.volume<<",\"volume_error_bound_mm3\":"<<result.volume_error_bound
            <<",\"deposited_filament_mm\":"<<result.deposited_filament
            <<",\"position_error_bound_mm\":"<<result.max_position_error_bound
            <<",\"alignment_error_bound_deg\":"<<result.max_alignment_error_bound_deg
            <<",\"travel_clearance_lower_bound_mm\":"<<result.travel_clearance_lower_bound
            <<",\"deposition_model\":\"local_tangent_rectangular\""
            <<",\"support_generated\":false,\"collision_checked\":false,\"printer_safe\":false}\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"Coupon rejected: "<<e.what()<<'\n'; return 1;}
}
