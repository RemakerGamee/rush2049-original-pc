// Isolated native menu -> preferences -> guest memory regression. No ROM/game.
#define SDL_MAIN_HANDLED
#include <SDL_syswm.h>
#include "pc_menu.hpp"
#include "cheat_native_state.hpp"
#include <map>
#include <iostream>
struct Memory {
    std::map<uint32_t,unsigned> bytes,halves;
    void byte(uint32_t a,uint8_t v){bytes[a]=v;}
    void half(uint32_t a,uint16_t v){halves[a]=v;}
};
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(int argc,char** argv) {
    try {
        require(argc==2,"fresh test directory required");
        auto path=std::filesystem::path(argv[1]);require(!std::filesystem::exists(path),"preserve existing test data");
        std::filesystem::create_directories(path);
        require(SDL_Init(SDL_INIT_VIDEO)==0,"SDL initialization failed");
        auto window=SDL_CreateWindow("Hidden cheat settings regression",0,0,640,480,SDL_WINDOW_HIDDEN);
        require(window!=nullptr,"hidden window missing");
        SDL_SysWMinfo info{};SDL_VERSION(&info.version);require(SDL_GetWindowWMInfo(window,&info),"native handle missing");
        rush::pc::attach(window,info.info.win.window,path);
        Memory memory; rush::cheats::NativeState state;
        auto apply=[&]{auto [prefs,rev]=rush::pc::cheatSettings->snapshot();return state.apply(memory,prefs,rev);};
        require(!apply()&&memory.bytes.empty(),"default off must leave retail RAM untouched");
        rush::pc::command(102);require(apply(),"Enable must publish native update");
        rush::pc::open_cheats(false);
        require(rush::pc::cheatWindow&&!IsWindowVisible(rush::pc::cheatWindow),"test settings panel must stay hidden");
        SendMessageW(rush::pc::cheatWindow,WM_COMMAND,MAKEWPARAM(103,BN_CLICKED),0);apply();
        for(unsigned row=0;row<21;++row)require(memory.bytes[0x8011648C+row]==1,"All Cheats must activate all native rows");
        require(SendMessageW(GetDlgItem(rush::pc::cheatWindow,103),BM_GETCHECK,0,0)==BST_CHECKED,"All Cheats control must be checked");
        SendMessageW(rush::pc::cheatWindow,WM_COMMAND,MAKEWPARAM(103,BN_CLICKED),0);apply();
        for(unsigned row=0;row<21;++row)require(memory.bytes[0x8011648C+row]==0,"All Cheats OFF must restore native rows");
        require(memory.bytes[0x801164A1]==1,"Enable must show native menu");
        for(size_t i=0;i<21;++i) {
            rush::pc::command(110+int(i));require(apply(),"selection must publish native update");
            const auto binding=rush::cheats::native_bindings[i];
            require(memory.bytes[0x8011648C+binding.row]==1,"selection must unlock correct native row");
            if(binding.byte_address)require(memory.bytes[binding.byte_address]==binding.on,"selection must activate gameplay byte");
            require(GetMenuState(rush::pc::cheatMenu,110+int(i),MF_BYCOMMAND)&MF_CHECKED,"selected menu must be checked");
            require(SendMessageW(GetDlgItem(rush::pc::cheatWindow,110+int(i)),BM_GETCHECK,0,0)==BST_CHECKED,"persistent panel must show selected state");
            rush::pc::command(110+int(i));require(apply(),"OFF must publish update");
            require(memory.bytes[0x8011648C+binding.row]==0,"OFF must clear native row");
            if(binding.byte_address)require(memory.bytes[binding.byte_address]==binding.off,"OFF must restore gameplay default");
        }
        rush::pc::command(110);apply();
        const auto saved=rush::cheats::Visibility(path/"cheats.cfg").preferences();
        require(saved.show&&saved.enabled[0],"checked selections must survive restart");
        rush::pc::command(102);apply();require(memory.bytes[0x801164A1]==0,"master OFF must hide native menu");
        require(memory.bytes[0x8017A4D8]==1&&memory.bytes[0x8013F2FC]==1,"restore normal collisions/brakes");
        const auto pane=rush::pc::cheatWindow;SendMessageW(pane,WM_CLOSE,0,0);require(IsWindow(pane),"closing must preserve reusable panel");
        rush::pc::detach();require(!IsWindow(pane),"shutdown must destroy panel");SDL_DestroyWindow(window);SDL_Quit();
        std::cout<<"PASS actual native menu commands: Enable, all 21 independent ON/OFF transfers, checked states, defaults and saved preferences. Gameplay effects require live testing.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
