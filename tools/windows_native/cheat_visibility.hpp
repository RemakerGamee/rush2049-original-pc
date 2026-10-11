#pragma once
#include "cheat_codes.hpp"
#include <atomic>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <deque>
#ifdef _WIN32
#include <Windows.h>
#endif
namespace rush::cheats {
struct Setting {const char* label; bool normal_on; const char* value;};
inline const std::array<Setting,21>& settings_catalog() {
 static const std::array<Setting,21> rows{{
 {"ALL TRACKS",false,nullptr},{"ALL CARS",false,nullptr},{"ALL PARTS",false,nullptr},
 {"RESURRECT IN PLACE",false,nullptr},{"FRAME SCALE",false,"1"},{"TIRE SCALING",false,nullptr},
 {"FOG COLOR",false,"BLACK"},{"CAR COLLISIONS",true,nullptr},{"CONE MINES",false,nullptr},
 {"CAR MINES",false,nullptr},{"TRACK ORIENTATION",false,"UPSIDE-DOWN"},{"AUTO ABORT",false,nullptr},
 {"SUPER SPEED",false,nullptr},{"INVINCIBLE",false,nullptr},{"INVISIBLE CAR",false,nullptr},
 {"INVISIBLE TRACK",false,nullptr},{"BRAKES",true,nullptr},{"SUPER TIRES",false,nullptr},
 {"MASS",false,"HEAVY"},{"SUICIDE MODE",false,nullptr},{"BATTLE PAINT SHOP",false,nullptr}}};
 return rows;
}
struct Preferences {
 bool show=false;
 std::array<bool,21> enabled{};
 // These are opt-in modifiers, not raw native values. Unchecked preserves retail defaults.
 Preferences()=default;
};
inline void enable_all(Preferences& p) {p.show=true;p.enabled.fill(true);}
// Saved host settings are consumed on the guest game thread, including the title screen.
// No controller callback, synthetic buttons, codes or navigation are involved.
class Visibility {
 std::filesystem::path path_;
 mutable std::mutex settings_mutex_;
 Preferences prefs_;
 uint64_t revision_=1;
public:
 explicit Visibility(std::filesystem::path path):path_(std::move(path)) {
  if(!std::filesystem::exists(path_))return;
  if(std::filesystem::is_symlink(path_)||!std::filesystem::is_regular_file(path_))throw std::runtime_error("cheat preference is not a regular file");
  std::ifstream file(path_);std::string line;
  bool modifiers=false;
  if(std::getline(file,line)&&line=="cheat-preferences=2") {modifiers=true;std::getline(file,line);}
  if(line!="show-cheat-menu=0"&&line!="show-cheat-menu=1")throw std::runtime_error("invalid cheat preference");
  prefs_.show=line.back()=='1';
  size_t count=0;
  while(std::getline(file,line)) {
   if(line.empty())continue;
   if(count>=21)throw std::runtime_error("extra cheat preferences");
   std::string expected="cheat-"+std::to_string(count)+"=";
   if(line!=expected+"0"&&line!=expected+"1")throw std::runtime_error("invalid cheat setting");
   const bool raw=line.back()=='1';
   prefs_.enabled[count]=modifiers?raw:(raw!=settings_catalog()[count].normal_on);++count;
  }
  if(count!=0&&count!=21)throw std::runtime_error("incomplete cheat preferences");
 }
 Preferences preferences()const{std::lock_guard<std::mutex> lock(settings_mutex_);return prefs_;}
 bool desired()const{return preferences().show;}
 std::pair<Preferences,uint64_t> snapshot()const {
  std::lock_guard<std::mutex> lock(settings_mutex_);return {prefs_,revision_};
 }
 bool set(bool value,std::string& error){auto p=preferences();p.show=value;return set(p,error);}
 bool set(const Preferences& value,std::string& error) {
  std::lock_guard<std::mutex> lock(settings_mutex_);
  try {
   if(path_.empty())throw std::runtime_error("no cheat preference path");
   if(std::filesystem::exists(path_)&&(std::filesystem::is_symlink(path_)||!std::filesystem::is_regular_file(path_)))throw std::runtime_error("cheat preference is not a regular file");
   std::filesystem::create_directories(path_.parent_path());auto temp=path_;temp+=".pending";
   if(std::filesystem::exists(temp))throw std::runtime_error("pending cheat preference already exists");
   {std::ofstream out(temp,std::ios::binary);out<<"cheat-preferences=2"<<char(10)<<"show-cheat-menu="<<value.show<<char(10);
    for(size_t i=0;i<21;++i)out<<"cheat-"<<i<<"="<<value.enabled[i]<<char(10);
    out.close();if(!out){std::filesystem::remove(temp);throw std::runtime_error("cannot write cheat preference");}}
#ifdef _WIN32
   if(!MoveFileExW(temp.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::filesystem::remove(temp);throw std::runtime_error("cannot replace cheat preference");}
#else
   try{std::filesystem::rename(temp,path_);}catch(...){std::filesystem::remove(temp);throw;}
#endif
   prefs_=value;++revision_;return true;
  }catch(const std::exception& e){error=e.what();return false;}
 }
};
}
