#pragma once
#include "controller_manager.hpp"
#include "input_remap.hpp"
#include "config/Config.h"
namespace rush::options {
inline constexpr const char* controller_review_url="https://github.com/RemakerGamee/rush2049-original-pc/issues/new?template=controller-submission.yml";
struct ReviewRequest {rush::input::Profile profile;rush::input::DeviceInfo device;};
inline nlohmann::json controller_submission(const ReviewRequest& request){
    using namespace rush::input;
    const auto& p=request.profile;
    if(!valid_controller_name(p.display_name) || !valid_controller_family(p.controller_family))throw std::runtime_error("Enter a controller name and select its type first.");
    if(!p.enabled || !p.stick_x.set || !p.stick_y.set)throw std::runtime_error("Map both stick axes and your controller buttons before submitting.");
    if(p.guid!=request.device.guid || p.game_controller!=request.device.game_controller)throw std::runtime_error("Apply your device mapping before submitting it.");
    if(auto error=validate_profile(p,request.device.caps))throw std::runtime_error(*error);
    Config only;only.keyboard_port=-1;only.profiles={p};
    auto text=serialize_input_config(only);auto parsed=parse_config(text);
    if(!parsed.ok())throw std::runtime_error(parsed.errors.front());
    nlohmann::json buttons=nlohmann::json::object();
    for(size_t i=0;i<button_count;++i){auto row=nlohmann::json::array();for(const auto& b:p.bindings[i])row.push_back({{"kind",b.kind==BindKind::Button?"button":b.kind==BindKind::Hat?"hat":"axis"},{"index",b.index},{"hatMask",b.hat_mask},{"axisSign",b.axis_sign},{"threshold",b.threshold}});buttons[button_name(Button(i))]=row;}
    return {{"schema",1},{"reviewStatus","pending"},{"controllerName",p.display_name},{"controllerType",p.controller_family},{"deviceGuid",p.guid},{"inputBackend",p.game_controller?"SDL game controller":"SDL raw joystick"},{"capabilities",{{"buttons",request.device.caps.buttons},{"axes",request.device.caps.axes},{"hats",request.device.caps.hats}}},{"buttons",buttons},{"stickX",{{"axis",p.stick_x.axis},{"invert",p.stick_x.invert}}},{"stickY",{{"axis",p.stick_y.axis},{"invert",p.stick_y.invert}}},{"deadzone",p.deadzone},{"sensitivity",p.sensitivity},{"mappingConfig",text}};
}
inline std::string review_encode(const std::string& text){
    std::string out;const char* hex="0123456789ABCDEF";
    for(unsigned char c:text){if((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-' || c=='_' || c=='.' || c=='~')out+=char(c);else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;
}
inline std::string review_link(const ReviewRequest& request,const std::string& package){
    int family=0;for(int i=0;i<7;++i)if(request.profile.controller_family==rush::input::controller_families[i])family=i;
    return std::string(controller_review_url)+"&title="+review_encode("[Controller submission] "+request.profile.display_name)+"&controller-name="+review_encode(request.profile.display_name)+"&controller-type="+review_encode(rush::input::controller_family_labels[family])+"&mapping="+review_encode(package);
}
inline std::filesystem::path save_controller_submission(const std::filesystem::path& directory,const std::string& bytes){
    if(!directory.is_absolute())throw std::runtime_error("Controller submission folder must be absolute.");
    std::filesystem::create_directories(directory);
    auto target=directory/("controller-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".json");
    HANDLE file=CreateFileW(target.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create controller review package.");
    DWORD written=0;bool ok=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size() && FlushFileBuffers(file);CloseHandle(file);
    if(!ok){DeleteFileW(target.c_str());throw std::runtime_error("Controller review package could not be saved.");}return target;
}
}
