#include "fit.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {void value(std::ostream& out,double n) {if(std::isfinite(n)) out<<n; else out<<"null";}}
int main(int argc,char** argv) {
    try {
        if(argc!=5 && (argc!=7 || std::string(argv[5])!="--map"))
            throw std::invalid_argument("usage: kinexis-substrate-fit MACHINE COUPON FIT_CONFIG GCODE [--map NEW_CSV]");
        auto machine=kinexis::read_machine(argv[1]); auto coupon=kinexis::read_coupon(argv[2]); auto fit=kinexis::read_fit(argv[3]);
        std::ifstream in(argv[4],std::ios::binary); if(!in) throw std::invalid_argument("cannot open planar substrate G-code");
        std::string bytes((std::istreambuf_iterator<char>(in)),{});
        if(in.bad()) throw std::runtime_error("substrate read failed");
        auto replay=kinexis::decode_gcode(bytes,fit.home);
        auto result=kinexis::check_substrate_fit(coupon,kinexis::make_coupon(machine,coupon),replay,fit);
        if(argc==7) {
            if(std::filesystem::exists(argv[6])) throw std::invalid_argument("fit map must be a new file");
            std::ofstream out(argv[6]); if(!out) throw std::runtime_error("cannot create fit map");
            out<<std::setprecision(17)<<"xmin,xmax,ymin,ymax,lower_top,upper_top,lower_gap,upper_gap,status\n";
            for(const auto& c:result.cells) {
                out<<c.xmin<<','<<c.xmax<<','<<c.ymin<<','<<c.ymax;
                for(double n:{c.lower_top,c.upper_top,c.lower_gap,c.upper_gap}) {out<<','; if(std::isfinite(n)) out<<n;}
                out<<','<<c.status<<'\n';
            }
            out.close(); if(!out) {std::filesystem::remove(argv[6]); throw std::runtime_error("fit map write failed");}
        }
        std::cout<<std::setprecision(17)<<"{\"fit_passed\":"<<(result.passed_fit()?"true":"false")
            <<",\"planar_beads\":"<<result.beads<<",\"first_layer_regions\":"<<result.regions
            <<",\"passed_cells\":"<<result.passed<<",\"failed_cells\":"<<result.failed<<",\"uncertain_cells\":"<<result.uncertain
            <<",\"minimum_gap_bound_mm\":"; value(std::cout,result.minimum_gap_bound);
        std::cout<<",\"maximum_gap_bound_mm\":"; value(std::cout,result.maximum_gap_bound);
        std::cout<<",\"work\":"<<result.work<<",\"surface_model\":\"bounded_planar_rounded_strips\""
            <<",\"planar_build_support_checked\":false,\"planar_motion_limits_checked\":false"
            <<",\"registration_calibrated\":false,\"complete_curved_job_validated\":false,\"printer_safe\":false}\n";
        return result.passed_fit()?0:1;
    } catch(const std::exception& e) {std::cerr<<"Surface fit rejected: "<<e.what()<<'\n'; return 2;}
}
