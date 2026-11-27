#include "gcode.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        if(argc<7) throw std::invalid_argument("usage: kinexis-gcode-check MACHINE INPUT --home X Y Z [--disabled] [--identity-output NEW_FILE]");
        kinexis::Vec3 home{}; bool home_seen=false, enabled=true; std::string output;
        for(int i=3;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--home" && !home_seen && i+3<argc) {
                home_seen=true;
                for(auto& n:home) {size_t used=0; std::string token=argv[++i]; n=std::stod(token,&used);
                    if(used!=token.size()) throw std::invalid_argument("invalid home coordinate");}
            } else if(arg=="--disabled" && enabled) enabled=false;
            else if(arg=="--identity-output" && output.empty() && i+1<argc) output=argv[++i];
            else throw std::invalid_argument("unknown or repeated option: "+arg);
        }
        if(!home_seen) throw std::invalid_argument("explicit --home fixture is required");
        std::ifstream in(argv[2],std::ios::binary);
        if(!in) throw std::invalid_argument("cannot open G-code input");
        std::string bytes((std::istreambuf_iterator<char>(in)),{});
        if(in.bad()) throw std::runtime_error("G-code read failed");
        auto replay=kinexis::decode_gcode(bytes,home);
        kinexis::verify_planar_replay(kinexis::read_machine(argv[1]),replay,enabled);
        if(!output.empty()) {
            if(std::filesystem::exists(output)) throw std::invalid_argument("identity output must be a new file");
            std::ofstream out(output,std::ios::binary);
            if(!out) throw std::runtime_error("cannot create identity output");
            out.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); out.close();
            if(!out) {std::filesystem::remove(output); throw std::runtime_error("identity output write failed");}
        }
        std::cout<<std::setprecision(17)<<"{\"mode\":\""<<(enabled?"zero-tilt":"disabled")
            <<"\",\"source_bytes\":"<<bytes.size()<<",\"moves\":"<<replay.moves
            <<",\"segments\":"<<replay.segments.size()<<",\"homing_commands\":"<<replay.homing_commands
            <<",\"state_commands\":"<<replay.state_commands<<",\"net_extrusion\":"<<replay.net_extrusion
            <<",\"final_tip\":["<<replay.final_tip[0]<<','<<replay.final_tip[1]<<','<<replay.final_tip[2]
            <<"],\"homing_paths_validated\":false,\"printer_safe\":false}\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"G-code replay rejected: "<<e.what()<<'\n'; return 1;}
}
