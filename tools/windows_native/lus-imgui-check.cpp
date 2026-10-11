#include "lus-imgui-store.hpp"
#include <iostream>
#include "cheat_native_state.hpp"
#include <map>
int main(){
    auto root=std::filesystem::temp_directory_path()/std::to_string(GetCurrentProcessId());
    std::filesystem::create_directory(root);auto file=root/"options.json";
    try {
        struct Memory {std::map<uint32_t,unsigned> bytes,halves;
            void byte(uint32_t a,uint8_t v){bytes[a]=v;}
            void half(uint32_t a,uint16_t v){halves[a]=v;}} memory;
        rush::cheats::NativeState cheats;
        rush::cheats::Preferences prefs;
        if(cheats.apply(memory,prefs,1)||!memory.bytes.empty())throw std::runtime_error("Opt-out touched retail state");
        prefs.show=true;
        if(!cheats.apply(memory,prefs,2)||memory.bytes[rush::cheats::native_menu_flag]!=1)throw std::runtime_error("Title enable failed");
        rush::cheats::enable_all(prefs);
        if(!cheats.apply(memory,prefs,3))throw std::runtime_error("Bulk revision ignored");
        for(size_t i=0;i<prefs.enabled.size();++i){auto b=rush::cheats::native_bindings[i];
            if(!prefs.enabled[i]||memory.bytes[rush::cheats::native_unlocks+b.row]!=1||(b.byte_address&&memory.bytes[b.byte_address]!=b.on))throw std::runtime_error("Bulk cheat incomplete");}
        if(cheats.apply(memory,prefs,3))throw std::runtime_error("Repeated frame overwrote native edits");
        memory.bytes[rush::cheats::native_menu_flag]=0;cheats.invalidate();
        if(!cheats.apply(memory,prefs,3)||memory.bytes[rush::cheats::native_menu_flag]!=1)throw std::runtime_error("Menu transition lost cheats");
        rush::cheats::Visibility saved_cheats(root/"cheats.cfg");std::string cheat_error;
        if(!saved_cheats.set(prefs,cheat_error))throw std::runtime_error(cheat_error);
        auto reloaded=rush::cheats::Visibility(root/"cheats.cfg").preferences();
        if(!reloaded.show||reloaded.enabled!=prefs.enabled)throw std::runtime_error("Bulk preferences lost on restart");
        prefs.show=false;cheats.apply(memory,prefs,4);
        for(auto b:rush::cheats::native_bindings)if(memory.bytes[rush::cheats::native_unlocks+b.row]!=0||(b.byte_address&&memory.bytes[b.byte_address]!=b.off))throw std::runtime_error("Master OFF failed restoration");
        rush::options::Store store(file);store.scale=1.5f;store.resolution=1;store.window_size=3;store.save();
        rush::options::Store loaded(file);
        if(loaded.stored_scale()!=1.5f || loaded.resolution!=1 || loaded.window_size!=3)throw std::runtime_error("LUS config reload failed");
        for(int hz:{60,120,144,165,180,200,240}){loaded.refresh=hz;loaded.vsync=hz==60;loaded.save();rush::options::Store next(file);if(next.refresh!=hz || next.vsync!=(hz==60))throw std::runtime_error("Display settings lost on restart");}
        loaded.pak_modes={1,0,1,0};loaded.save();if(rush::options::Store(file).pak_modes!=loaded.pak_modes)throw std::runtime_error("Pak preferences lost on restart");
        loaded.pak_modes[0]=2;bool bad_pak=false;try{loaded.save();}catch(...){bad_pak=true;}if(!bad_pak || rush::options::Store(file).pak_modes[0]!=1)throw std::runtime_error("Invalid Pak changed stored preferences");loaded.pak_modes[0]=1;
        loaded.refresh=119;bool bad_rate=false;try{loaded.save();}catch(...){bad_rate=true;}
        if(!bad_rate || rush::options::Store(file).refresh!=240)throw std::runtime_error("Invalid refresh rate changed saved preferences");
        loaded.refresh=60;
        loaded.window_size=6;
        bool invalid_size=false;try{loaded.save();}catch(...){invalid_size=true;}
        if(!invalid_size || rush::options::Store(file).window_size!=3)throw std::runtime_error("Invalid resolution changed saved preferences");
        std::ofstream(file)<<"{\"schema\":1,\"windowSize\":5}";
        if(rush::options::Store(file).window_size!=5)throw std::runtime_error("Legacy 4K selection changed during migration");
        std::ofstream(file)<<"invalid old configuration";
        bool refused=false;try{rush::options::Store invalid(file);}catch(...){refused=true;}
        if(!refused)throw std::runtime_error("Malformed file not rejected");
        std::ifstream input(file);std::string preserved;std::getline(input,preserved);
        if(preserved!="invalid old configuration")throw std::runtime_error("Malformed existing file changed");
        input.close();
        std::filesystem::remove_all(root);std::cout<<"LUS_CONFIG_CHECK: persistence and malformed-file preservation passed\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
