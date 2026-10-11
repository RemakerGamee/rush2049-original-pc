#pragma once
#include "config/Config.h"
#include <filesystem>
#include <fstream>
#include <cmath>
#include <array>
#include <windows.h>
namespace rush::options {
class Store {
    Ship::Config config{"None"};
    std::filesystem::path path;
public:
    float scale=1.0f;
    int resolution=2, window_size=6, window_width=800, window_height=600, refresh=60;
    bool vsync=true;
    std::array<int,4> pak_modes{};
    bool fullscreen=false, diagnostics=false;
    bool fill_fullscreen=true;
    explicit Store(const std::filesystem::path& file):path(file){
        if(path.empty())return;
        if(!path.is_absolute())throw std::runtime_error("Options path must be absolute");
        if(std::filesystem::exists(path)) {
            std::ifstream input(path);
            auto value=nlohmann::json::parse(input);
            const int schema=value.is_object()?value.value("schema",0):0;
            if(schema<1 || schema>5)throw std::runtime_error("Unsupported options file; preserved unchanged");
            if(value.contains("uiScale"))scale=value.at("uiScale").get<float>();
            if(value.contains("resolution"))resolution=value.at("resolution").get<int>();
            if(value.contains("windowSize")){
                window_size=value.at("windowSize").get<int>();
                if(schema==1){
                    if(window_size<0 || window_size>5||(refresh!=60 && refresh!=120 && refresh!=144 && refresh!=165 && refresh!=180 && refresh!=200 && refresh!=240))throw std::runtime_error("Invalid legacy window size; preserved unchanged");
                    constexpr int migrated[]={0,0,1,2,3,5};window_size=migrated[window_size];
                }
            }
            if(value.contains("fullscreen"))fullscreen=value.at("fullscreen").get<bool>();
            if(value.contains("fillFullscreen"))fill_fullscreen=value.at("fillFullscreen").get<bool>();
            if(value.contains("windowWidth"))window_width=value.at("windowWidth").get<int>();
            if(value.contains("windowHeight"))window_height=value.at("windowHeight").get<int>();
            if(value.contains("diagnostics"))diagnostics=value.at("diagnostics").get<bool>();
            if(value.contains("refreshRate"))refresh=value.at("refreshRate").get<int>();
            if(value.contains("vsync"))vsync=value.at("vsync").get<bool>();
            if(value.contains("controllerPaks"))pak_modes=value.at("controllerPaks").get<std::array<int,4>>();
            check();
        }
        sync();
    }
    void check()const{for(int mode:pak_modes)if(mode<0 || mode>1)throw std::runtime_error("Invalid controller Pak preference; saved configuration preserved");if(!std::isfinite(scale)||scale<0.75f||scale>2.0f||resolution<0||resolution>2||window_size<0||window_size>6||window_width<640||window_width>7680||window_height<360||window_height>4320||(refresh!=60 && refresh!=120 && refresh!=144 && refresh!=165 && refresh!=180 && refresh!=200 && refresh!=240))throw std::runtime_error("Invalid options; custom windows require width 640–7680 and height 360–4320; saved configuration preserved");}
    std::array<int,2> window_dimensions()const{
        check();constexpr int sizes[][2]={{640,480},{1280,720},{1920,1080},{2560,1440},{3440,1440},{3840,2160}};
        if(window_size==6)return {window_width,window_height};
        return {sizes[window_size][0],sizes[window_size][1]};
    }
    void sync(){check();config.SetInt("schema",5);for(int port=0;port<4;++port)config.SetInt("controllerPaks."+std::to_string(port),pak_modes[port]);config.SetInt("refreshRate",refresh);config.SetBool("vsync",vsync);config.SetFloat("uiScale",scale);config.SetInt("resolution",resolution);config.SetInt("windowSize",window_size);config.SetInt("windowWidth",window_width);config.SetInt("windowHeight",window_height);config.SetBool("fullscreen",fullscreen);config.SetBool("fillFullscreen",fill_fullscreen);config.SetBool("diagnostics",diagnostics);}
    void save(){
        sync();if(path.empty())return;
        if(!std::filesystem::is_directory(path.parent_path()))throw std::runtime_error("Options folder must already exist");
        auto temporary=path;temporary+=L".new";
        HANDLE output=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(output==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create options staging file; existing files preserved");
        auto document=config.GetFlattenedJson().unflatten();document["controllerPaks"]=pak_modes;
        const auto bytes=document.dump(2)+"\n";
        DWORD written=0;
        bool ok=WriteFile(output,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(output);
        CloseHandle(output);
        if(!ok || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str());throw std::runtime_error("Options save failed; previous configuration retained");
        }
    }
    float stored_scale(){return config.GetFloat("uiScale",1.0f);}
};
}
