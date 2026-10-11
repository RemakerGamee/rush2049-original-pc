#include "lus-imgui-store.hpp"
#include <iostream>
#include "cheat_native_state.hpp"
#include <map>
#include "rush_fullscreen_fill.h"
int main(){
    auto root=std::filesystem::temp_directory_path()/std::to_string(GetCurrentProcessId());
    std::filesystem::create_directory(root);auto file=root/"options.json";
    try {
        struct Viewport{float x,y,width,height,minDepth=0,maxDepth=1;};
        struct Crop{int left,top,right,bottom;};
        for(auto extent:{std::array<int,2>{1280,720},{1920,1080},{3440,1440},{3840,2160},{1080,1920}}){
            Viewport v{240,0,1440,1080};Crop c{240,0,1680,1080};
            if(!rush::presentation::fill_surface(v,c,extent[0],extent[1])||v.x!=0||v.y!=0||std::abs(v.width-extent[0])>0.01f||std::abs(v.height-extent[1])>0.01f||c.left!=0||c.top!=0||c.right!=extent[0]||c.bottom!=extent[1]||v.minDepth!=0||v.maxDepth!=1)throw std::runtime_error("Fullscreen image does not cover surface");
        }
        Viewport sampled{-10,-20,340,280};Crop active{0,0,320,240};
        rush::presentation::fill_surface(sampled,active,1920,1080);
        if(sampled.x!=-60||sampled.y!=-90||sampled.width!=2040||sampled.height!=1260)throw std::runtime_error("VI sampling/crop relationship changed");
        Viewport unchanged{0,0,320,240};Crop empty{0,0,0,0};
        if(rush::presentation::fill_surface(unchanged,empty,1920,1080)||unchanged.width!=320)throw std::runtime_error("Empty crop must preserve viewport");
        struct Memory {std::map<uint32_t,unsigned> bytes,halves;
            uint8_t read_byte(uint32_t a){return uint8_t(bytes[a]);}
            uint16_t read_half(uint32_t a){return uint16_t(halves[a]);}
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
        auto baseline=rush::cheats::NativeState::read(memory);
        memory.bytes[0x80142B00]=0; // Native INVINCIBLE OFF.
        memory.bytes[0x801427A0]=23; // Native fifth fog palette, not BLACK.
        memory.bytes[0x80143F1C]=3; // Native mass choice must survive next load.
        auto [native_prefs,expected]=saved_cheats.snapshot();
        if(!rush::cheats::NativeState::capture_changes(native_prefs,baseline,rush::cheats::NativeState::read(memory))||native_prefs.enabled[13]||native_prefs.native_raw[6]!=23||native_prefs.native_raw[18]!=3)throw std::runtime_error("Native changes not captured");
        if(!saved_cheats.set_if_revision(native_prefs,expected,cheat_error))throw std::runtime_error("Native sync save failed");
        cheats.adopt(expected+1);
        if(cheats.apply(memory,native_prefs,expected+1))throw std::runtime_error("Native edit was reapplied unnecessarily");
        auto exact=rush::cheats::Visibility(root/"cheats.cfg").preferences();
        if(exact.enabled[13]||exact.native_raw[6]!=23||exact.native_raw[18]!=3)throw std::runtime_error("Native values lost on restart");
        cheats.invalidate();cheats.apply(memory,exact,expected+1);
        if(memory.bytes[0x801427A0]!=23||memory.halves[rush::cheats::native_values+40]!=5||memory.bytes[0x80143F1C]!=3)throw std::runtime_error("Native values changed on menu reload");
        if(!saved_cheats.set_enabled(13,true,cheat_error))throw std::runtime_error(cheat_error);
        if(saved_cheats.set_if_revision(native_prefs,expected+1,cheat_error)||!saved_cheats.preferences().enabled[13])throw std::runtime_error("Stale guest update overwrote newer launcher choice");
        auto nochange=exact;
        if(rush::cheats::NativeState::capture_changes(nochange,baseline,baseline))throw std::runtime_error("Unchanged menu produced spurious save");
        prefs.show=false;cheats.apply(memory,prefs,4);
        for(auto b:rush::cheats::native_bindings)if(memory.bytes[rush::cheats::native_unlocks+b.row]!=0||(b.byte_address&&memory.bytes[b.byte_address]!=b.off))throw std::runtime_error("Master OFF failed restoration");
        rush::options::Store store(file);store.scale=1.5f;store.resolution=1;store.window_size=3;store.save();
        rush::options::Store loaded(file);
        if(!loaded.fill_fullscreen)throw std::runtime_error("Fullscreen fill must default ON for existing preferences");
        loaded.fill_fullscreen=false;loaded.save();if(rush::options::Store(file).fill_fullscreen)throw std::runtime_error("Fullscreen fit choice lost on restart");
        loaded.fill_fullscreen=true;loaded.save();if(!rush::options::Store(file).fill_fullscreen)throw std::runtime_error("Fullscreen fill choice lost on restart");
        if(loaded.stored_scale()!=1.5f || loaded.resolution!=1 || loaded.window_size!=3)throw std::runtime_error("LUS config reload failed");
        for(int hz:{60,120,144,165,180,200,240}){loaded.refresh=hz;loaded.vsync=hz==60;loaded.save();rush::options::Store next(file);if(next.refresh!=hz || next.vsync!=(hz==60))throw std::runtime_error("Display settings lost on restart");}
        loaded.pak_modes={1,0,1,0};loaded.save();if(rush::options::Store(file).pak_modes!=loaded.pak_modes)throw std::runtime_error("Pak preferences lost on restart");
        loaded.pak_modes[0]=2;bool bad_pak=false;try{loaded.save();}catch(...){bad_pak=true;}if(!bad_pak || rush::options::Store(file).pak_modes[0]!=1)throw std::runtime_error("Invalid Pak changed stored preferences");loaded.pak_modes[0]=1;
        loaded.refresh=119;bool bad_rate=false;try{loaded.save();}catch(...){bad_rate=true;}
        if(!bad_rate || rush::options::Store(file).refresh!=240)throw std::runtime_error("Invalid refresh rate changed saved preferences");
        loaded.refresh=60;
        loaded.window_size=7;
        bool invalid_size=false;try{loaded.save();}catch(...){invalid_size=true;}
        if(!invalid_size || rush::options::Store(file).window_size!=3)throw std::runtime_error("Invalid resolution changed saved preferences");
        loaded.window_size=6;loaded.window_width=960;loaded.window_height=640;loaded.save();
        auto custom=rush::options::Store(file);
        if(custom.window_dimensions()!=std::array<int,2>{960,640})throw std::runtime_error("Custom window dimensions lost on restart");
        loaded.window_width=0;bool bad_width=false;try{loaded.save();}catch(...){bad_width=true;}
        if(!bad_width || rush::options::Store(file).window_dimensions()!=std::array<int,2>{960,640})throw std::runtime_error("Invalid custom width changed saved preferences");
        rush::options::Store defaults({});
        if(defaults.fullscreen || defaults.window_dimensions()!=std::array<int,2>{800,600})throw std::runtime_error("Fresh window default is not compact");
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
