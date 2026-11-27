#include "gcode.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cctype>
#include <map>
#include <sstream>
#include <stdexcept>

namespace kinexis {
namespace {
void need(bool ok,const std::string& why) { if(!ok) throw std::invalid_argument(why); }
struct Command { char family; int code; std::map<char,double> words; };
Command command(const std::string& text) {
    size_t p=0;
    auto spaces=[&] { while(p<text.size() && std::isspace(static_cast<unsigned char>(text[p]))) ++p; };
    spaces();
    need(p<text.size() && (text[p]=='G'||text[p]=='M'),"only explicit G/M commands supported");
    Command c{text[p++],0,{}};
    size_t start=p;
    while(p<text.size() && std::isdigit(static_cast<unsigned char>(text[p]))) ++p;
    need(p>start,"missing command number");
    c.code=std::stoi(text.substr(start,p-start));
    need(p==text.size() || text[p]!='.',"fractional commands unsupported");
    while(true) {
        spaces(); if(p==text.size()) break;
        char letter=text[p++];
        need(letter>='A' && letter<='Z',"invalid parameter letter");
        start=p;
        if(p<text.size() && (text[p]=='+'||text[p]=='-')) ++p;
        bool digit=false;
        while(p<text.size() && std::isdigit(static_cast<unsigned char>(text[p]))) {++p; digit=true;}
        if(p<text.size() && text[p]=='.') {
            ++p;
            while(p<text.size() && std::isdigit(static_cast<unsigned char>(text[p]))) {++p; digit=true;}
        }
        need(digit,"missing or invalid parameter value");
        double value=std::stod(text.substr(start,p-start));
        need(std::isfinite(value),"non-finite parameter");
        need(c.words.emplace(letter,value).second,"duplicate parameter");
    }
    return c;
}
void allowed(const Command& c,const std::string& letters) {
    for(const auto& w:c.words) need(letters.find(w.first)!=std::string::npos,
                                   "unsupported parameter "+std::string(1,w.first));
}
}
GcodeReplay decode_gcode(const std::string& bytes,Vec3 home) {
    need(bytes.find('\0')==std::string::npos,"binary NUL in G-code");
    for(double n:home) need(std::isfinite(n),"non-finite home fixture");
    GcodeReplay replay;
    Vec3 tip=home, logical=home, offset{};
    double e_logical=0, e_total=0, feed=0, time=0;
    bool absolute_xyz=true, absolute_e=true, e_mode_known=false;
    auto begin_segment=[&] {
        time=0;
        replay.segments.push_back({Sample{0,"travel",tip,e_total,{}}});
    };
    begin_segment();
    std::istringstream input(bytes); std::string line; size_t line_number=0;
    while(std::getline(input,line)) {
        ++line_number;
        try {
            line=line.substr(0,line.find(';'));
            if(line.find_first_not_of(" \t\r")==std::string::npos) continue;
            Command c=command(line);
            auto has=[&](char word) { return c.words.count(word)!=0; };
            auto value=[&](char word) { return c.words.at(word); };
            if(c.family=='G' && (c.code==0 || c.code==1)) {
                allowed(c,"XYZEF");
                if(has('F')) {feed=value('F'); need(feed>0,"feed must be positive");}
                Vec3 next=tip, next_logical=logical;
                for(size_t i=0;i<3;++i) if(has("XYZ"[i])) {
                    double v=value("XYZ"[i]);
                    next_logical[i]=absolute_xyz?v:logical[i]+v;
                    next[i]=next_logical[i]+offset[i];
                    need(std::isfinite(next[i]) && std::isfinite(next_logical[i]),"coordinate overflow");
                }
                double next_e=e_logical, de=0;
                if(has('E')) {
                    need(e_mode_known,"explicit M82/M83 required before E (and after G90/G91)");
                    next_e=absolute_e?value('E'):e_logical+value('E');
                    de=next_e-e_logical;
                    need(std::isfinite(next_e) && std::isfinite(de),"extrusion overflow");
                }
                double length=std::hypot(next[0]-tip[0],next[1]-tip[1],next[2]-tip[2]);
                need(std::isfinite(length),"move length overflow");
                if(length>0 || de!=0) {
                    need(feed>0,"motion requires a positive modal feed");
                    double dt=(length>0?length:std::abs(de))*60/feed;
                    need(std::isfinite(dt) && dt>0 && std::isfinite(time+dt) && time+dt>time,
                         "unrepresentable move duration");
                    time+=dt; e_total+=de;
                    need(std::isfinite(e_total),"cumulative extrusion overflow");
                    std::string kind=de<0?"retract":de>0?"extrusion":
                        (next[0]==tip[0] && next[1]==tip[1] && next[2]>tip[2]?"lift":"travel");
                    replay.segments.back().push_back({time,kind,next,e_total,{}});
                    ++replay.moves;
                }
                logical=next_logical; tip=next; e_logical=next_e;
            } else if(c.family=='G' && c.code==92) {
                allowed(c,"XYZE"); need(!c.words.empty(),"G92 needs explicit coordinates");
                for(size_t i=0;i<3;++i) if(has("XYZ"[i])) {
                    logical[i]=value("XYZ"[i]); offset[i]=tip[i]-logical[i];
                    need(std::isfinite(offset[i]),"coordinate offset overflow");
                }
                if(has('E')) e_logical=value('E');
                ++replay.state_commands;
            } else if(c.family=='G' && c.code==28) {
                allowed(c,"XYZ");
                for(const auto& w:c.words) need(w.second==0,"homing axis words must be zero");
                for(size_t i=0;i<3;++i) if(c.words.empty() || has("XYZ"[i])) {
                    tip[i]=logical[i]=home[i]; offset[i]=0;
                }
                ++replay.homing_commands; begin_segment();
            } else if(c.family=='G' && (c.code==90 || c.code==91)) {
                allowed(c,""); absolute_xyz=c.code==90;
                // Avoid assuming firmware-specific G90/G91 interactions with E.
                e_mode_known=false; ++replay.state_commands;
            } else if(c.family=='M' && (c.code==82 || c.code==83)) {
                allowed(c,""); absolute_e=c.code==82; e_mode_known=true; ++replay.state_commands;
            } else if(c.family=='G' && c.code==21) {
                allowed(c,""); ++replay.state_commands;
            } else if(c.family=='G' && c.code==4) {
                allowed(c,"PS"); need(has('P')!=has('S'),"dwell needs exactly one of P or S");
                double dt=has('P')?value('P')/1000:value('S');
                need(dt>=0 && std::isfinite(dt),"invalid dwell");
                if(dt>0) {
                    need(std::isfinite(time+dt) && time+dt>time,"unrepresentable dwell"); time+=dt;
                    replay.segments.back().push_back({time,"travel",tip,e_total,{}});
                }
                ++replay.state_commands;
            } else if(c.family=='M' && (c.code==104 || c.code==109 || c.code==140 || c.code==190)) {
                allowed(c,"SRT");
                need(!has('T') || value('T')==0,"multiple tools unsupported");
                ++replay.state_commands; // Temperatures/wait durations are not simulated.
            } else if(c.family=='M' && c.code==106) {
                allowed(c,"SP"); ++replay.state_commands;
            } else if(c.family=='M' && c.code==107) {
                allowed(c,"P"); ++replay.state_commands;
            } else if(c.family=='M' && c.code==84) {
                allowed(c,"S");
                // This subset permits motor release only as the last command.
                ++replay.state_commands;
                std::string tail((std::istreambuf_iterator<char>(input)),{});
                std::istringstream remaining(tail); std::string rest;
                while(std::getline(remaining,rest)) {
                    rest=rest.substr(0,rest.find(';'));
                    need(rest.find_first_not_of(" \t\r")==std::string::npos,"commands after M84 unsupported");
                }
            } else {
                throw std::invalid_argument("unsupported command "+std::string(1,c.family)+std::to_string(c.code));
            }
        } catch(const std::exception& e) {
            throw std::invalid_argument("G-code line "+std::to_string(line_number)+": "+e.what());
        }
    }
    need(replay.moves>0,"G-code contains no replayable moves");
    replay.final_tip=tip; replay.net_extrusion=e_total;
    return replay;
}
void verify_planar_replay(const Machine& machine,const GcodeReplay& replay,bool enabled) {
    for(const auto& segment:replay.segments) {
        auto frames=simulate(machine,segment,enabled);
        for(size_t i=0;i<frames.size();++i) {
            const auto& f=frames[i]; const auto& s=segment[i];
            need(std::memcmp(f.tip.data(),s.tip.data(),sizeof(Vec3))==0,"planar XYZ identity failed");
            need(f.source.extrusion==s.extrusion,"planar extrusion identity failed");
            for(size_t j=0;j<3;++j) need(f.actuators[j]==machine.actuators[j].zero,"neutral actuator identity failed");
        }
    }
}
}
