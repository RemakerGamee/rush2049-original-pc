#include "controller-submission.hpp"
#include "controller-presets.hpp"
#include <iostream>
using namespace rush::input;
int main(){try{
    auto p=n64_controller_preset(false);p.display_name="My N64 pad / \"Test\"";p.controller_family="n64-wired";
    Config c;c.profiles={p};auto parsed=parse_config(serialize_input_config(c));
    if(!parsed.ok() || parsed.config.profiles[0].display_name!=p.display_name || parsed.config.profiles[0].controller_family!=p.controller_family)throw std::runtime_error("controller metadata roundtrip");
    if(parse_config("rush-input 1\nprofile test guid "+p.guid+"\nenabled 0\ndisplay-name-hex 0a").ok())throw std::runtime_error("unsafe controller name accepted");
    if(parse_config("rush-input 1\nprofile test guid "+p.guid+"\nenabled 0\ncontroller-type unsupported").ok())throw std::runtime_error("unknown family accepted");
    rush::input::DeviceInfo device;device.guid=p.guid;device.caps={13,2,1};device.serial="MUST_NOT_LEAVE_PC";
    rush::options::ReviewRequest request{p,device};auto package=rush::options::controller_submission(request);auto bytes=package.dump(2);
    if(bytes.find(device.serial)!=std::string::npos || package["reviewStatus"]!="pending" || package["buttons"].size()!=14)throw std::runtime_error("review package privacy or completeness");
    auto mapping=parse_config(package["mappingConfig"].get<std::string>());if(!mapping.ok() || mapping.config.profiles.size()!=1 || !mapping.config.assignments.empty() || mapping.config.keyboard_port!=-1)throw std::runtime_error("unrelated settings exported");
    auto url=rush::options::review_link(request,bytes);if(url.find("controller-submission.yml")==std::string::npos || url.find("&controller-name=My%20N64%20pad") ==std::string::npos)throw std::runtime_error("submission form encoding");
    request.profile.controller_family="";bool rejected=false;try{rush::options::controller_submission(request);}catch(...){rejected=true;}if(!rejected)throw std::runtime_error("missing controller type accepted");
    request.profile=p;request.device.guid=wireless_n64_guid;rejected=false;try{rush::options::controller_submission(request);}catch(...){rejected=true;}if(!rejected)throw std::runtime_error("mismatched device accepted");
    auto folder=std::filesystem::temp_directory_path()/("rush-review-check-"+std::to_string(GetCurrentProcessId()));
    auto saved=rush::options::save_controller_submission(folder,bytes);std::ifstream f(saved);auto loaded=nlohmann::json::parse(f);f.close();if(loaded!=package)throw std::runtime_error("review package save/reload");std::filesystem::remove(saved);std::filesystem::remove(folder);
    std::cout<<"PASS controller submission: metadata, required fields, privacy, device validation, URL encoding and export reload\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
