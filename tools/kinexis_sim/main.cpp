#include "bed.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        if(argc<3 || argc>4 || (argc==4 && std::string(argv[3])!="--disabled"))
            throw std::invalid_argument("usage: kinexis-sim MACHINE TRAJECTORY [--disabled]");
        auto machine=kinexis::read_machine(argv[1]);
        auto frames=kinexis::simulate(machine,kinexis::read_samples(argv[2]),argc==3);
        // Validate everything before publishing any report; never emit partial motion.
        std::ostringstream report;
        report<<std::setprecision(17)<<"time,kind,x,y,z,e";
        for(const auto& a:machine.actuators) report<<','<<a.name;
        report<<",reconstructed_lift,reconstructed_pitch,reconstructed_roll\n";
        for(const auto& f:frames) {
            report<<f.source.time<<','<<f.source.kind;
            for(double n:f.tip) report<<','<<n;
            report<<','<<f.source.extrusion;
            for(double n:f.actuators) report<<','<<n;
            report<<','<<f.reconstructed.lift<<','<<f.reconstructed.pitch<<','<<f.reconstructed.roll<<'\n';
        }
        std::cout<<report.str();
        return std::cout?0:2;
    } catch(const std::exception& e) { std::cerr<<"Simulation rejected: "<<e.what()<<'\n'; return 1; }
}
