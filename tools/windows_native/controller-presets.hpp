#pragma once
#include "input_profile.hpp"
namespace rush::input {
inline constexpr const char* wired_n64_guid="0300550d632500007505000000000000";
inline constexpr const char* wireless_n64_guid="03003d486f0e00001311000000000000";
// These physical indices are the previously verified Kiwitata and SAFFUN
// profiles. Match their exact SDL GUIDs; unrelated adapters are not guessed.
inline Profile n64_controller_preset(bool wireless){
    Profile p;p.id=wireless?"saffun-wireless":"kiwitata-wired";
    p.guid=wireless?wireless_n64_guid:wired_n64_guid;
    p.stick_x={true,0,false};p.stick_y={true,1,true};
    const int buttons[]={2,1,wireless?9:12,6,4,5,-1,-1,-1,-1,wireless?11:9,3,0,wireless?10:8};
    for(int row=0;row<14;++row)if(buttons[row]>=0)p.bindings[row].push_back({BindKind::Button,buttons[row]});
    const uint8_t directions[]={hat_up,hat_down,hat_left,hat_right};
    for(int row=0;row<4;++row)p.bindings[6+row].push_back({BindKind::Hat,0,directions[row]});
    return p;
}
inline Profile xbox_controller_preset(const std::string& guid){
    auto p=default_game_controller_profile();p.id="xbox-one-"+guid;p.guid=guid;return p;
}
inline const char* controller_preset_name(const std::string& guid){
    if(guid==wired_n64_guid)return "Wired N64 controller";
    if(guid==wireless_n64_guid)return "Wireless / USB dongle N64 controller";
    return nullptr;
}
}
