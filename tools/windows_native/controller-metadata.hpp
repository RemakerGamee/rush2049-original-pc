#pragma once
#include <string>
#include <string_view>
#include <algorithm>
namespace rush::input {
inline constexpr const char* controller_families[]={"ps3","ps4","xbox360","xbox-one-wired","xbox-one-wireless","n64-wired","n64-wireless"};
inline constexpr const char* controller_family_labels[]={"PS3","PS4","Xbox 360","Xbox One wired","Xbox One wireless","N64 wired","N64 wireless / USB dongle"};
inline bool valid_controller_family(std::string_view value){for(auto* family:controller_families)if(value==family)return true;return false;}
inline bool valid_controller_name(std::string_view value){
    if(value.empty() || value.size()>128 || value.front()==' ' || value.back()==' ')return false;
    return std::none_of(value.begin(),value.end(),[](unsigned char c){return c<32 || c==127;});
}
inline std::string name_hex(std::string_view value){const char* hex="0123456789abcdef";std::string out;for(unsigned char c:value){out+=hex[c>>4];out+=hex[c&15];}return out;}
inline bool name_unhex(std::string_view value,std::string& out){
    if(value.empty() || value.size()>256 || value.size()%2)return false;
    out.clear();auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(size_t i=0;i<value.size();i+=2){int a=digit(value[i]),b=digit(value[i+1]);if(a<0 || b<0)return false;out+=char((a<<4)|b);}return valid_controller_name(out);
}
}
